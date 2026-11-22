#include "mfc/ir.h"

#include <algorithm>
#include <cstdio>
#include <functional>
#include <map>
#include <optional>
#include <queue>
#include <set>
#include <sstream>
#include <vector>

#include "mfc/events.h"

namespace mfc::ir {

const char* opName(Op op) {
  switch (op) {
    case Op::Nop: return "nop";
    case Op::IntConst: return "int";
    case Op::FloatConst: return "float";
    case Op::StrConst: return "str";
    case Op::BoolConst: return "bool";
    case Op::LoadVar: return "load";
    case Op::StoreVar: return "store";
    case Op::Add: return "add";
    case Op::Sub: return "sub";
    case Op::Mul: return "mul";
    case Op::Div: return "div";
    case Op::Mod: return "mod";
    case Op::Neg: return "neg";
    case Op::Eq: return "eq";
    case Op::Ne: return "ne";
    case Op::Lt: return "lt";
    case Op::Le: return "le";
    case Op::Gt: return "gt";
    case Op::Ge: return "ge";
    case Op::Not: return "not";
    case Op::Call: return "call";
    case Op::Emit: return "emit";
    case Op::EventField: return "event_field";
    case Op::Phi: return "phi";
  }
  return "?";
}

bool isPure(Op op) {
  switch (op) {
    case Op::Call:
    case Op::Emit:
    case Op::StoreVar:
    case Op::LoadVar:
    case Op::Nop:
      return false;
    default:
      return true;
  }
}

namespace {

// ---------------------------------------------------------------------------
// AST -> CFG builder
// ---------------------------------------------------------------------------

class Builder {
 public:
  Builder(Function& fn, DiagnosticSink& sink, const std::string& file)
      : fn_(fn), sink_(sink), file_(file) {
    fn_.entry = 0;
    Block entry;
    entry.id = 0;
    fn_.blocks.push_back(std::move(entry));
  }

  void build(const Handler& handler) {
    if (!handler.isStart) {
      buildListenerPrelude(handler);
    }
    if (handler.body) {
      lowerStmt(*handler.body);
    }
    if (!fn_.blocks[cur_].term.hasTerm) {
      Terminator term;
      term.kind = TermKind::End;
      term.loc = handler.loc;
      setTerm(term);
    }
  }

 private:
  int newBlock() {
    Block block;
    block.id = static_cast<int>(fn_.blocks.size());
    fn_.blocks.push_back(std::move(block));
    return fn_.blocks.back().id;
  }

  int newValue() { return fn_.valueCount++; }

  int addVar(const std::string& name, Type type, bool readOnly = false) {
    for (size_t i = 0; i < fn_.vars.size(); ++i) {
      if (fn_.vars[i].name == name) return static_cast<int>(i);
    }
    VarInfo info;
    info.name = name;
    info.type = type;
    info.readOnly = readOnly;
    fn_.vars.push_back(std::move(info));
    return static_cast<int>(fn_.vars.size()) - 1;
  }

  int findVar(const std::string& name) const {
    for (size_t i = 0; i < fn_.vars.size(); ++i) {
      if (fn_.vars[i].name == name) return static_cast<int>(i);
    }
    return -1;
  }

  void link(int from, int to) {
    if (to < 0) return;
    fn_.blocks[from].succ.push_back(to);
    fn_.blocks[to].pred.push_back(from);
  }

  void setTerm(Terminator term) {
    Block& block = fn_.blocks[cur_];
    term.hasTerm = true;
    block.term = std::move(term);
    switch (block.term.kind) {
      case TermKind::Jump:
        link(cur_, block.term.target);
        break;
      case TermKind::Branch:
        link(cur_, block.term.target);
        if (block.term.falseTarget != block.term.target) link(cur_, block.term.falseTarget);
        break;
      case TermKind::WaitTime:
      case TermKind::WaitEvent:
        link(cur_, block.term.target);
        break;
      case TermKind::End:
        break;
    }
  }

  void jumpTo(int target) {
    if (fn_.blocks[cur_].term.hasTerm) return;
    Terminator term;
    term.kind = TermKind::Jump;
    term.target = target;
    term.loc = curLoc_;
    term.nodeId = nodeId_;
    setTerm(term);
  }

  int emitValue(Inst inst) {
    inst.dst = newValue();
    if (inst.nodeId.empty()) inst.nodeId = nodeId_;
    fn_.blocks[cur_].insts.push_back(std::move(inst));
    return fn_.blocks[cur_].insts.back().dst;
  }

  void emitVoid(Inst inst) {
    inst.dst = -1;
    if (inst.nodeId.empty()) inst.nodeId = nodeId_;
    fn_.blocks[cur_].insts.push_back(std::move(inst));
  }

  void buildListenerPrelude(const Handler& handler) {
    int predIndex = 0;
    if (handler.filter) {
      Predicate predicate;
      if (compilePredicate(*handler.filter, predicate, sink_, file_)) {
        fn_.predicates.push_back(std::move(predicate));
        predIndex = static_cast<int>(fn_.predicates.size());
      }
    }
    const int resume = newBlock();
    Terminator term;
    term.kind = TermKind::WaitEvent;
    term.eventName = handler.eventName;
    term.predicate = predIndex;
    term.target = resume;
    term.loc = handler.loc;
    setTerm(term);
    cur_ = resume;

    const EventSchema* schema = findEventSchema(handler.eventName);
    if (schema != nullptr) {
      for (const auto& field : schema->fields) {
        const int var = addVar(field.name, field.type, true);
        Inst fieldInst;
        fieldInst.op = Op::EventField;
        fieldInst.text = field.name;
        fieldInst.loc = handler.loc;
        const int value = emitValue(std::move(fieldInst));
        Inst store;
        store.op = Op::StoreVar;
        store.var = var;
        store.a = value;
        store.loc = handler.loc;
        emitVoid(std::move(store));
      }
    }
  }

  int lowerExpr(const Expr& expr) {
    switch (expr.kind) {
      case ExprKind::IntLit: {
        Inst inst;
        inst.op = Op::IntConst;
        inst.intValue = expr.intValue;
        inst.loc = expr.loc;
        return emitValue(std::move(inst));
      }
      case ExprKind::FloatLit: {
        Inst inst;
        inst.op = Op::FloatConst;
        inst.floatValue = expr.floatValue;
        inst.loc = expr.loc;
        return emitValue(std::move(inst));
      }
      case ExprKind::StringLit: {
        Inst inst;
        inst.op = Op::StrConst;
        inst.text = expr.text;
        inst.loc = expr.loc;
        return emitValue(std::move(inst));
      }
      case ExprKind::BoolLit: {
        Inst inst;
        inst.op = Op::BoolConst;
        inst.intValue = expr.boolValue ? 1 : 0;
        inst.loc = expr.loc;
        return emitValue(std::move(inst));
      }
      case ExprKind::Ident: {
        const int var = findVar(expr.text);
        if (var < 0) {
          Inst inst;
          inst.op = Op::IntConst;
          inst.intValue = 0;
          inst.loc = expr.loc;
          return emitValue(std::move(inst));
        }
        Inst inst;
        inst.op = Op::LoadVar;
        inst.var = var;
        inst.loc = expr.loc;
        return emitValue(std::move(inst));
      }
      case ExprKind::Unary: {
        const int operand = lowerExpr(*expr.lhs);
        Inst inst;
        inst.op = expr.op == "-" ? Op::Neg : Op::Not;
        inst.a = operand;
        inst.loc = expr.loc;
        return emitValue(std::move(inst));
      }
      case ExprKind::Binary: {
        if (expr.op == "&&" || expr.op == "||") {
          return lowerLogical(expr);
        }
        const int lhs = lowerExpr(*expr.lhs);
        const int rhs = lowerExpr(*expr.rhs);
        Inst inst;
        if (expr.op == "+") inst.op = Op::Add;
        else if (expr.op == "-") inst.op = Op::Sub;
        else if (expr.op == "*") inst.op = Op::Mul;
        else if (expr.op == "/") inst.op = Op::Div;
        else if (expr.op == "%") inst.op = Op::Mod;
        else if (expr.op == "==") inst.op = Op::Eq;
        else if (expr.op == "!=") inst.op = Op::Ne;
        else if (expr.op == "<") inst.op = Op::Lt;
        else if (expr.op == "<=") inst.op = Op::Le;
        else if (expr.op == ">") inst.op = Op::Gt;
        else if (expr.op == ">=") inst.op = Op::Ge;
        else inst.op = Op::Nop;
        inst.a = lhs;
        inst.b = rhs;
        inst.loc = expr.loc;
        return emitValue(std::move(inst));
      }
      case ExprKind::Call: {
        Inst inst;
        inst.op = Op::Call;
        inst.text = expr.text;
        for (const auto& arg : expr.args) {
          inst.args.push_back(lowerExpr(*arg));
        }
        inst.loc = expr.loc;
        return emitValue(std::move(inst));
      }
    }
    Inst inst;
    inst.op = Op::IntConst;
    inst.loc = expr.loc;
    return emitValue(std::move(inst));
  }

  int lowerLogical(const Expr& expr) {
    const bool isAnd = expr.op == "&&";
    const std::string name = "$short" + std::to_string(++tempCounter_);
    const int var = addVar(name, Type::Bool);
    const int lhs = lowerExpr(*expr.lhs);
    Inst store;
    store.op = Op::StoreVar;
    store.var = var;
    store.a = lhs;
    store.loc = expr.loc;
    emitVoid(std::move(store));

    const int testBlock = newBlock();
    const int rhsBlock = newBlock();
    const int joinBlock = newBlock();

    jumpTo(testBlock);
    cur_ = testBlock;
    Inst load;
    load.op = Op::LoadVar;
    load.var = var;
    load.loc = expr.loc;
    const int cond = emitValue(std::move(load));
    Terminator branch;
    branch.kind = TermKind::Branch;
    branch.cond = cond;
    // `a && b` evaluates b only when a is true; `a || b` only when a is false.
    branch.target = isAnd ? rhsBlock : joinBlock;
    branch.falseTarget = isAnd ? joinBlock : rhsBlock;
    branch.loc = expr.loc;
    setTerm(branch);

    cur_ = rhsBlock;
    const int rhs = lowerExpr(*expr.rhs);
    Inst store2;
    store2.op = Op::StoreVar;
    store2.var = var;
    store2.a = rhs;
    store2.loc = expr.loc;
    emitVoid(std::move(store2));
    jumpTo(joinBlock);

    cur_ = joinBlock;
    Inst load2;
    load2.op = Op::LoadVar;
    load2.var = var;
    load2.loc = expr.loc;
    return emitValue(std::move(load2));
  }

  void lowerStmt(const Stmt& stmt) {
    if (!stmt.nodeId.empty()) nodeId_ = stmt.nodeId;
    curLoc_ = stmt.loc;
    switch (stmt.kind) {
      case StmtKind::Block: {
        for (const auto& child : stmt.body) lowerStmt(*child);
        break;
      }
      case StmtKind::Let:
      case StmtKind::Assign: {
        int var = findVar(stmt.name);
        if (var < 0) var = addVar(stmt.name, Type::Error);
        const int value = lowerExpr(*stmt.expr);
        Inst store;
        store.op = Op::StoreVar;
        store.var = var;
        store.a = value;
        store.loc = stmt.loc;
        store.nodeId = stmt.nodeId;
        emitVoid(std::move(store));
        break;
      }
      case StmtKind::If: {
        const int cond = lowerExpr(*stmt.expr);
        const int thenBlock = newBlock();
        const int elseBlock = stmt.elseBody.empty() ? -1 : newBlock();
        const int joinBlock = newBlock();
        Terminator branch;
        branch.kind = TermKind::Branch;
        branch.cond = cond;
        branch.target = thenBlock;
        branch.falseTarget = elseBlock >= 0 ? elseBlock : joinBlock;
        branch.loc = stmt.loc;
        branch.nodeId = stmt.nodeId;
        setTerm(branch);

        cur_ = thenBlock;
        lowerStmt(*stmt.body[0]);
        if (!fn_.blocks[cur_].term.hasTerm) jumpTo(joinBlock);

        if (elseBlock >= 0) {
          cur_ = elseBlock;
          lowerStmt(*stmt.elseBody[0]);
          if (!fn_.blocks[cur_].term.hasTerm) jumpTo(joinBlock);
        }
        cur_ = joinBlock;
        break;
      }
      case StmtKind::While: {
        const int header = newBlock();
        jumpTo(header);
        cur_ = header;
        const int cond = lowerExpr(*stmt.expr);
        const int bodyBlock = newBlock();
        const int exitBlock = newBlock();
        Terminator branch;
        branch.kind = TermKind::Branch;
        branch.cond = cond;
        branch.target = bodyBlock;
        branch.falseTarget = exitBlock;
        branch.loc = stmt.loc;
        branch.nodeId = stmt.nodeId;
        setTerm(branch);

        cur_ = bodyBlock;
        lowerStmt(*stmt.body[0]);
        if (!fn_.blocks[cur_].term.hasTerm) jumpTo(header);
        cur_ = exitBlock;
        break;
      }
      case StmtKind::WaitEvent: {
        int predIndex = 0;
        if (stmt.filter) {
          Predicate predicate;
          if (compilePredicate(*stmt.filter, predicate, sink_, file_)) {
            fn_.predicates.push_back(std::move(predicate));
            predIndex = static_cast<int>(fn_.predicates.size());
          }
        }
        const int resume = newBlock();
        Terminator term;
        term.kind = TermKind::WaitEvent;
        term.eventName = stmt.eventName;
        term.predicate = predIndex;
        term.target = resume;
        term.loc = stmt.loc;
        term.nodeId = stmt.nodeId;
        setTerm(term);
        cur_ = resume;
        break;
      }
      case StmtKind::WaitTime: {
        const int value = lowerExpr(*stmt.expr);
        const int resume = newBlock();
        Terminator term;
        term.kind = TermKind::WaitTime;
        term.value = value;
        term.target = resume;
        term.loc = stmt.loc;
        term.nodeId = stmt.nodeId;
        setTerm(term);
        cur_ = resume;
        break;
      }
      case StmtKind::Emit:
      case StmtKind::Call: {
        Inst inst;
        inst.op = stmt.kind == StmtKind::Emit ? Op::Emit : Op::Call;
        inst.text = stmt.callee;
        for (const auto& arg : stmt.args) inst.args.push_back(lowerExpr(*arg));
        inst.loc = stmt.loc;
        inst.nodeId = stmt.nodeId;
        emitVoid(std::move(inst));
        break;
      }
    }
  }

  Function& fn_;
  DiagnosticSink& sink_;
  std::string file_;
  int cur_ = 0;
  int tempCounter_ = 0;
  Loc curLoc_;
  std::string nodeId_;
};

// ---------------------------------------------------------------------------
// CFG utilities
// ---------------------------------------------------------------------------

void rebuildCFG(Function& fn) {
  for (auto& block : fn.blocks) {
    block.succ.clear();
    block.pred.clear();
  }
  for (auto& block : fn.blocks) {
    auto link = [&](int to) {
      if (to < 0) return;
      block.succ.push_back(to);
      fn.blocks[to].pred.push_back(block.id);
    };
    switch (block.term.kind) {
      case TermKind::Jump:
        link(block.term.target);
        break;
      case TermKind::Branch:
        link(block.term.target);
        if (block.term.falseTarget != block.term.target) link(block.term.falseTarget);
        break;
      case TermKind::WaitTime:
      case TermKind::WaitEvent:
        link(block.term.target);
        break;
      case TermKind::End:
        break;
    }
  }
}

bool removeUnreachable(Function& fn) {
  rebuildCFG(fn);
  const int n = static_cast<int>(fn.blocks.size());
  if (n == 0) return false;
  std::vector<char> reachable(static_cast<size_t>(n), 0);
  std::vector<int> stack;
  stack.push_back(fn.entry);
  reachable[static_cast<size_t>(fn.entry)] = 1;
  while (!stack.empty()) {
    const int block = stack.back();
    stack.pop_back();
    for (int next : fn.blocks[static_cast<size_t>(block)].succ) {
      if (next >= 0 && next < n && !reachable[static_cast<size_t>(next)]) {
        reachable[static_cast<size_t>(next)] = 1;
        stack.push_back(next);
      }
    }
  }
  bool removed = false;
  for (int i = 0; i < n; ++i) {
    if (!reachable[static_cast<size_t>(i)]) {
      removed = true;
      break;
    }
  }
  if (!removed) return false;

  std::vector<int> remap(static_cast<size_t>(n), -1);
  std::vector<Block> kept;
  for (int i = 0; i < n; ++i) {
    if (!reachable[static_cast<size_t>(i)]) continue;
    remap[static_cast<size_t>(i)] = static_cast<int>(kept.size());
    Block block = std::move(fn.blocks[static_cast<size_t>(i)]);
    block.id = static_cast<int>(kept.size());
    kept.push_back(std::move(block));
  }
  for (auto& block : kept) {
    Terminator& term = block.term;
    switch (term.kind) {
      case TermKind::Jump:
        term.target = remap[static_cast<size_t>(term.target)];
        break;
      case TermKind::Branch:
        term.target = remap[static_cast<size_t>(term.target)];
        term.falseTarget = remap[static_cast<size_t>(term.falseTarget)];
        break;
      case TermKind::WaitTime:
      case TermKind::WaitEvent:
        term.target = remap[static_cast<size_t>(term.target)];
        break;
      case TermKind::End:
        break;
    }
    for (auto& inst : block.insts) {
      if (inst.op != Op::Phi) continue;
      std::vector<int> operands;
      std::vector<int> operandBlocks;
      for (size_t i = 0; i < inst.operands.size(); ++i) {
        const int oldBlock = inst.operandBlocks[i];
        if (oldBlock < 0 || oldBlock >= n) continue;
        const int mapped = remap[static_cast<size_t>(oldBlock)];
        if (mapped < 0) continue;
        operands.push_back(inst.operands[i]);
        operandBlocks.push_back(mapped);
      }
      inst.operands = std::move(operands);
      inst.operandBlocks = std::move(operandBlocks);
    }
  }
  fn.entry = remap[static_cast<size_t>(fn.entry)];
  fn.blocks = std::move(kept);
  rebuildCFG(fn);
  return true;
}

struct DomInfo {
  std::vector<int> idom;
  std::vector<std::vector<int>> children;
  std::vector<std::vector<int>> frontier;
};

DomInfo computeDominators(const Function& fn) {
  const int n = static_cast<int>(fn.blocks.size());
  DomInfo info;
  info.idom.assign(static_cast<size_t>(n), -1);
  info.children.resize(static_cast<size_t>(n));
  info.frontier.resize(static_cast<size_t>(n));
  if (n == 0) return info;

  std::vector<std::vector<char>> dom(
      static_cast<size_t>(n), std::vector<char>(static_cast<size_t>(n), 1));
  for (int b = 0; b < n; ++b) {
    for (int d = 0; d < n; ++d) {
      dom[static_cast<size_t>(b)][static_cast<size_t>(d)] =
          (b == fn.entry) ? (d == fn.entry ? 1 : 0) : 1;
    }
    if (b == fn.entry) dom[static_cast<size_t>(b)][static_cast<size_t>(b)] = 1;
  }
  bool changed = true;
  int guard = 0;
  while (changed && guard++ < 256) {
    changed = false;
    for (int b = 0; b < n; ++b) {
      if (b == fn.entry) continue;
      std::vector<char> next(static_cast<size_t>(n), 1);
      for (int p : fn.blocks[static_cast<size_t>(b)].pred) {
        if (p < 0 || p >= n) continue;
        for (int d = 0; d < n; ++d) {
          next[static_cast<size_t>(d)] &= dom[static_cast<size_t>(p)][static_cast<size_t>(d)];
        }
      }
      next[static_cast<size_t>(b)] = 1;
      if (next != dom[static_cast<size_t>(b)]) {
        dom[static_cast<size_t>(b)] = std::move(next);
        changed = true;
      }
    }
  }
  for (int b = 0; b < n; ++b) {
    if (b == fn.entry) continue;
    int best = -1;
    int bestCount = -1;
    for (int d = 0; d < n; ++d) {
      if (d == b || !dom[static_cast<size_t>(b)][static_cast<size_t>(d)]) continue;
      int count = 0;
      for (int x = 0; x < n; ++x) count += dom[static_cast<size_t>(d)][static_cast<size_t>(x)];
      if (count > bestCount) {
        bestCount = count;
        best = d;
      }
    }
    info.idom[static_cast<size_t>(b)] = best;
    if (best >= 0) info.children[static_cast<size_t>(best)].push_back(b);
  }
  for (int b = 0; b < n; ++b) {
    if (fn.blocks[static_cast<size_t>(b)].pred.size() < 2) continue;
    for (int p : fn.blocks[static_cast<size_t>(b)].pred) {
      int runner = p;
      int guardThread = 0;
      while (runner >= 0 && runner != info.idom[static_cast<size_t>(b)] && guardThread++ < 1024) {
        info.frontier[static_cast<size_t>(runner)].push_back(b);
        runner = info.idom[static_cast<size_t>(runner)];
      }
    }
  }
  return info;
}

void insertPhis(Function& fn, const DomInfo& dom) {
  const size_t varCount = fn.vars.size();
  const size_t blockCount = fn.blocks.size();
  std::vector<std::vector<int>> defs(varCount);
  for (auto& block : fn.blocks) {
    for (auto& inst : block.insts) {
      if (inst.op == Op::StoreVar) {
        if (inst.var >= 0 && static_cast<size_t>(inst.var) < varCount) {
          defs[static_cast<size_t>(inst.var)].push_back(block.id);
        }
      }
    }
  }
  for (size_t var = 0; var < varCount; ++var) {
    std::vector<int> work = defs[var];
    std::vector<char> inWork(blockCount, 0);
    std::vector<char> hasPhi(blockCount, 0);
    for (int b : work) inWork[static_cast<size_t>(b)] = 1;
    while (!work.empty()) {
      const int block = work.back();
      work.pop_back();
      for (int target : dom.frontier[static_cast<size_t>(block)]) {
        if (hasPhi[static_cast<size_t>(target)]) continue;
        hasPhi[static_cast<size_t>(target)] = 1;
        Inst phi;
        phi.op = Op::Phi;
        phi.var = static_cast<int>(var);
        phi.dst = fn.valueCount++;
        fn.blocks[static_cast<size_t>(target)].insts.insert(
            fn.blocks[static_cast<size_t>(target)].insts.begin(), std::move(phi));
        if (!inWork[static_cast<size_t>(target)]) {
          inWork[static_cast<size_t>(target)] = 1;
          work.push_back(target);
        }
      }
    }
  }
}

void renameSSA(Function& fn, const DomInfo& dom) {
  const size_t varCount = fn.vars.size();
  std::vector<std::vector<int>> stacks(varCount);
  std::vector<int> alias(static_cast<size_t>(fn.valueCount), -1);

  std::function<int(int)> resolve = [&](int value) {
    int guard = 0;
    while (value >= 0 && value < fn.valueCount && alias[static_cast<size_t>(value)] >= 0 &&
           guard++ < fn.valueCount + 1) {
      value = alias[static_cast<size_t>(value)];
    }
    return value;
  };

  std::function<void(int)> dfs = [&](int blockIndex) {
    std::vector<int> pushedVars;
    Block& block = fn.blocks[static_cast<size_t>(blockIndex)];
    for (auto& inst : block.insts) {
      if (inst.op == Op::Phi) {
        stacks[static_cast<size_t>(inst.var)].push_back(inst.dst);
        pushedVars.push_back(inst.var);
      }
    }
    for (auto& inst : block.insts) {
      if (inst.op == Op::Phi) continue;
      if (inst.op == Op::LoadVar) {
        const int def =
            stacks[static_cast<size_t>(inst.var)].empty() ? -1
                                                          : stacks[static_cast<size_t>(inst.var)].back();
        alias[static_cast<size_t>(inst.dst)] = resolve(def);
        inst.op = Op::Nop;
        continue;
      }
      if (inst.op == Op::StoreVar) {
        stacks[static_cast<size_t>(inst.var)].push_back(inst.a);
        pushedVars.push_back(inst.var);
        inst.op = Op::Nop;
        continue;
      }
      inst.a = resolve(inst.a);
      inst.b = resolve(inst.b);
      for (auto& arg : inst.args) arg = resolve(arg);
    }
    block.term.cond = resolve(block.term.cond);
    block.term.value = resolve(block.term.value);

    for (int succ : block.succ) {
      for (auto& inst : fn.blocks[static_cast<size_t>(succ)].insts) {
        if (inst.op != Op::Phi) break;
        const int top = stacks[static_cast<size_t>(inst.var)].empty()
                            ? -1
                            : stacks[static_cast<size_t>(inst.var)].back();
        inst.operands.push_back(resolve(top));
        inst.operandBlocks.push_back(blockIndex);
      }
    }

    for (int child : dom.children[static_cast<size_t>(blockIndex)]) dfs(child);

    for (auto it = pushedVars.rbegin(); it != pushedVars.rend(); ++it) {
      stacks[static_cast<size_t>(*it)].pop_back();
    }
  };

  dfs(fn.entry);

  for (auto& block : fn.blocks) {
    block.insts.erase(std::remove_if(block.insts.begin(), block.insts.end(),
                                     [](const Inst& inst) { return inst.op == Op::Nop; }),
                      block.insts.end());
  }
}

void replaceAllUses(Function& fn, int from, int to) {
  auto replace = [&](int& value) {
    if (value == from) value = to;
  };
  for (auto& block : fn.blocks) {
    for (auto& inst : block.insts) {
      replace(inst.a);
      replace(inst.b);
      for (auto& arg : inst.args) replace(arg);
      for (auto& operand : inst.operands) replace(operand);
    }
    replace(block.term.cond);
    replace(block.term.value);
  }
}

bool eliminateTrivialPhis(Function& fn) {
  bool changed = false;
  bool again = true;
  int guard = 0;
  while (again && guard++ < 32) {
    again = false;
    for (auto& block : fn.blocks) {
      for (auto it = block.insts.begin(); it != block.insts.end();) {
        if (it->op != Op::Phi) {
          ++it;
          continue;
        }
        int chosen = -1;
        bool allSame = true;
        for (size_t i = 0; i < it->operands.size(); ++i) {
          const int operand = it->operands[i];
          if (chosen == -1) {
            chosen = operand;
          } else if (chosen != operand) {
            allSame = false;
            break;
          }
        }
        const bool deadPhi = it->operands.empty();
        if (deadPhi) {
          it = block.insts.erase(it);
          changed = true;
          again = true;
          continue;
        }
        if (allSame && chosen >= 0 && chosen != it->dst) {
          replaceAllUses(fn, it->dst, chosen);
          it = block.insts.erase(it);
          changed = true;
          again = true;
          continue;
        }
        ++it;
      }
    }
  }
  return changed;
}

struct ConstVal {
  Type type = Type::Error;
  int64_t i = 0;
  double f = 0.0;
  std::string s;
  bool b = false;
};

std::optional<ConstVal> constOfInst(const Inst& inst) {
  ConstVal value;
  switch (inst.op) {
    case Op::IntConst:
      value.type = Type::Int;
      value.i = inst.intValue;
      return value;
    case Op::FloatConst:
      value.type = Type::Float;
      value.f = inst.floatValue;
      return value;
    case Op::StrConst:
      value.type = Type::String;
      value.s = inst.text;
      return value;
    case Op::BoolConst:
      value.type = Type::Bool;
      value.b = inst.intValue != 0;
      return value;
    default:
      return std::nullopt;
  }
}

void setConstInst(Inst& inst, const ConstVal& value) {
  inst.a = -1;
  inst.b = -1;
  inst.args.clear();
  switch (value.type) {
    case Type::Int:
      inst.op = Op::IntConst;
      inst.intValue = value.i;
      break;
    case Type::Float:
      inst.op = Op::FloatConst;
      inst.floatValue = value.f;
      break;
    case Type::String:
      inst.op = Op::StrConst;
      inst.text = value.s;
      break;
    case Type::Bool:
      inst.op = Op::BoolConst;
      inst.intValue = value.b ? 1 : 0;
      break;
    default:
      break;
  }
}

std::optional<ConstVal> foldInst(const Inst& inst, const std::vector<std::optional<ConstVal>>& values) {
  auto get = [&](int value) -> const std::optional<ConstVal>& {
    static const std::optional<ConstVal> empty;
    if (value < 0 || static_cast<size_t>(value) >= values.size()) return empty;
    return values[static_cast<size_t>(value)];
  };
  auto numeric = [](const ConstVal& a, const ConstVal& b) {
    return (a.type == Type::Int || a.type == Type::Float) &&
           (b.type == Type::Int || b.type == Type::Float);
  };
  auto toDouble = [](const ConstVal& v) -> double {
    return v.type == Type::Int ? static_cast<double>(v.i) : v.f;
  };

  if (inst.op == Op::Neg) {
    const auto& a = get(inst.a);
    if (!a) return std::nullopt;
    if (a->type == Type::Int) {
      ConstVal out;
      out.type = Type::Int;
      out.i = -a->i;
      return out;
    }
    if (a->type == Type::Float) {
      ConstVal out;
      out.type = Type::Float;
      out.f = -a->f;
      return out;
    }
    return std::nullopt;
  }
  if (inst.op == Op::Not) {
    const auto& a = get(inst.a);
    if (!a || a->type != Type::Bool) return std::nullopt;
    ConstVal out;
    out.type = Type::Bool;
    out.b = !a->b;
    return out;
  }
  if (inst.op == Op::Add || inst.op == Op::Sub || inst.op == Op::Mul || inst.op == Op::Div ||
      inst.op == Op::Mod) {
    const auto& a = get(inst.a);
    const auto& b = get(inst.b);
    if (!a || !b || !numeric(*a, *b)) return std::nullopt;
    const bool floatOp = a->type == Type::Float || b->type == Type::Float;
    if (inst.op == Op::Mod) {
      if (floatOp || b->i == 0) return std::nullopt;
      ConstVal out;
      out.type = Type::Int;
      out.i = a->i % b->i;
      return out;
    }
    if (floatOp) {
      const double lhs = toDouble(*a);
      const double rhs = toDouble(*b);
      if (inst.op == Op::Div && rhs == 0.0) return std::nullopt;
      ConstVal out;
      out.type = Type::Float;
      if (inst.op == Op::Add) out.f = lhs + rhs;
      else if (inst.op == Op::Sub) out.f = lhs - rhs;
      else if (inst.op == Op::Mul) out.f = lhs * rhs;
      else out.f = lhs / rhs;
      return out;
    }
    if (inst.op == Op::Div && b->i == 0) return std::nullopt;
    ConstVal out;
    out.type = Type::Int;
    if (inst.op == Op::Add) out.i = a->i + b->i;
    else if (inst.op == Op::Sub) out.i = a->i - b->i;
    else if (inst.op == Op::Mul) out.i = a->i * b->i;
    else out.i = a->i / b->i;
    return out;
  }
  if (inst.op == Op::Eq || inst.op == Op::Ne || inst.op == Op::Lt || inst.op == Op::Le ||
      inst.op == Op::Gt || inst.op == Op::Ge) {
    const auto& a = get(inst.a);
    const auto& b = get(inst.b);
    if (!a || !b) return std::nullopt;
    bool result = false;
    if (numeric(*a, *b)) {
      const double lhs = toDouble(*a);
      const double rhs = toDouble(*b);
      if (inst.op == Op::Eq) result = lhs == rhs;
      else if (inst.op == Op::Ne) result = lhs != rhs;
      else if (inst.op == Op::Lt) result = lhs < rhs;
      else if (inst.op == Op::Le) result = lhs <= rhs;
      else if (inst.op == Op::Gt) result = lhs > rhs;
      else result = lhs >= rhs;
    } else if (a->type == Type::String && b->type == Type::String) {
      if (inst.op == Op::Eq) result = a->s == b->s;
      else if (inst.op == Op::Ne) result = a->s != b->s;
      else if (inst.op == Op::Lt) result = a->s < b->s;
      else if (inst.op == Op::Le) result = a->s <= b->s;
      else if (inst.op == Op::Gt) result = a->s > b->s;
      else result = a->s >= b->s;
    } else if (a->type == Type::Bool && b->type == Type::Bool) {
      if (inst.op == Op::Eq) result = a->b == b->b;
      else if (inst.op == Op::Ne) result = a->b != b->b;
      else return std::nullopt;
    } else {
      return std::nullopt;
    }
    ConstVal out;
    out.type = Type::Bool;
    out.b = result;
    return out;
  }
  return std::nullopt;
}

void foldConstants(Function& fn) {
  bool changed = true;
  int rounds = 0;
  while (changed && rounds++ < 8) {
    changed = false;
    std::vector<std::optional<ConstVal>> values(static_cast<size_t>(fn.valueCount));
    for (auto& block : fn.blocks) {
      for (auto& inst : block.insts) {
        if (inst.op == Op::Phi) {
          if (inst.operands.empty()) continue;
          bool allSame = true;
          std::optional<ConstVal> first;
          for (size_t i = 0; i < inst.operands.size(); ++i) {
            const int operand = inst.operands[i];
            if (operand < 0 || static_cast<size_t>(operand) >= values.size()) {
              allSame = false;
              break;
            }
            const auto& candidate = values[static_cast<size_t>(operand)];
            if (!candidate) {
              allSame = false;
              break;
            }
            if (i == 0) {
              first = candidate;
            } else if (first && (candidate->type != first->type || candidate->i != first->i ||
                                candidate->f != first->f || candidate->s != first->s ||
                                candidate->b != first->b)) {
              allSame = false;
              break;
            }
          }
          if (allSame && first && inst.dst >= 0) {
            values[static_cast<size_t>(inst.dst)] = first;
          }
          continue;
        }
        const auto constant = constOfInst(inst);
        if (constant && inst.dst >= 0) {
          values[static_cast<size_t>(inst.dst)] = constant;
        }
      }
    }
    for (auto& block : fn.blocks) {
      for (auto& inst : block.insts) {
        if (inst.op == Op::Phi) continue;
        if (constOfInst(inst)) continue;
        const auto folded = foldInst(inst, values);
        if (folded && inst.dst >= 0) {
          setConstInst(inst, *folded);
          values[static_cast<size_t>(inst.dst)] = folded;
          changed = true;
        }
      }
    }
  }
}

std::string cseKey(const Inst& inst) {
  std::ostringstream os;
  os << static_cast<int>(inst.op) << '|' << inst.a << '|' << inst.b << '|' << inst.text << '|'
     << inst.intValue << '|';
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%.17g", inst.floatValue);
  os << buffer;
  return os.str();
}

void localCSE(Function& fn) {
  std::vector<std::pair<int, int>> replacements;
  for (auto& block : fn.blocks) {
    std::map<std::string, int> table;
    for (auto& inst : block.insts) {
      if (!isPure(inst.op) || inst.op == Op::Phi) continue;
      if (inst.dst < 0) continue;
      const std::string key = cseKey(inst);
      auto it = table.find(key);
      if (it != table.end()) {
        replacements.push_back({inst.dst, it->second});
        inst.op = Op::Nop;
      } else {
        table[key] = inst.dst;
      }
    }
  }
  for (const auto& pair : replacements) replaceAllUses(fn, pair.first, pair.second);
  for (auto& block : fn.blocks) {
    block.insts.erase(std::remove_if(block.insts.begin(), block.insts.end(),
                                     [](const Inst& inst) { return inst.op == Op::Nop; }),
                      block.insts.end());
  }
}

void deadCodeElimination(Function& fn) {
  const size_t blockCount = fn.blocks.size();
  std::vector<int> defBlock(static_cast<size_t>(fn.valueCount), -1);
  std::vector<int> defIndex(static_cast<size_t>(fn.valueCount), -1);
  for (size_t b = 0; b < blockCount; ++b) {
    for (size_t i = 0; i < fn.blocks[b].insts.size(); ++i) {
      const Inst& inst = fn.blocks[b].insts[i];
      if (inst.dst >= 0 && static_cast<size_t>(inst.dst) < defBlock.size()) {
        defBlock[static_cast<size_t>(inst.dst)] = static_cast<int>(b);
        defIndex[static_cast<size_t>(inst.dst)] = static_cast<int>(i);
      }
    }
  }

  std::vector<char> liveValue(static_cast<size_t>(fn.valueCount), 0);
  std::vector<std::vector<char>> liveInst(blockCount);
  for (size_t b = 0; b < blockCount; ++b) liveInst[b].assign(fn.blocks[b].insts.size(), 0);

  std::vector<int> work;
  auto markUse = [&](int value) {
    if (value >= 0 && static_cast<size_t>(value) < liveValue.size() &&
        !liveValue[static_cast<size_t>(value)]) {
      liveValue[static_cast<size_t>(value)] = 1;
      work.push_back(value);
    }
  };

  for (size_t b = 0; b < blockCount; ++b) {
    for (size_t i = 0; i < fn.blocks[b].insts.size(); ++i) {
      const Inst& inst = fn.blocks[b].insts[i];
      if (inst.op == Op::Call || inst.op == Op::Emit) {
        liveInst[b][i] = 1;
        for (int arg : inst.args) markUse(arg);
      }
    }
    markUse(fn.blocks[b].term.cond);
    markUse(fn.blocks[b].term.value);
  }

  while (!work.empty()) {
    const int value = work.back();
    work.pop_back();
    if (value < 0 || static_cast<size_t>(value) >= defBlock.size()) continue;
    const int b = defBlock[static_cast<size_t>(value)];
    const int i = defIndex[static_cast<size_t>(value)];
    if (b < 0 || i < 0) continue;
    if (liveInst[static_cast<size_t>(b)][static_cast<size_t>(i)]) continue;
    liveInst[static_cast<size_t>(b)][static_cast<size_t>(i)] = 1;
    const Inst& inst = fn.blocks[static_cast<size_t>(b)].insts[static_cast<size_t>(i)];
    markUse(inst.a);
    markUse(inst.b);
    for (int arg : inst.args) markUse(arg);
    if (inst.op == Op::Phi) {
      for (int operand : inst.operands) markUse(operand);
    }
  }

  for (size_t b = 0; b < blockCount; ++b) {
    auto& insts = fn.blocks[b].insts;
    std::vector<Inst> kept;
    kept.reserve(insts.size());
    for (size_t i = 0; i < insts.size(); ++i) {
      const bool keep = liveInst[b][i] || insts[i].op == Op::Call || insts[i].op == Op::Emit;
      if (keep) kept.push_back(std::move(insts[i]));
    }
    insts = std::move(kept);
  }
}

std::optional<ConstVal> constantOfValue(const Function& fn, int value) {
  if (value < 0) return std::nullopt;
  for (const auto& block : fn.blocks) {
    for (const auto& inst : block.insts) {
      if (inst.dst == value) return constOfInst(inst);
    }
  }
  return std::nullopt;
}

void simplifyCFG(Function& fn) {
  bool changed = true;
  int guard = 0;
  while (changed && guard++ < 64) {
    changed = false;
    rebuildCFG(fn);
    for (auto& block : fn.blocks) {
      if (block.term.kind != TermKind::Branch) continue;
      const auto constant = constantOfValue(fn, block.term.cond);
      if (constant && constant->type == Type::Bool) {
        const int taken = constant->b ? block.term.target : block.term.falseTarget;
        block.term.kind = TermKind::Jump;
        block.term.target = taken;
        block.term.falseTarget = -1;
        changed = true;
      } else if (block.term.target == block.term.falseTarget) {
        block.term.kind = TermKind::Jump;
        block.term.falseTarget = -1;
        changed = true;
      }
    }
    for (auto& block : fn.blocks) {
      if (block.term.kind != TermKind::Jump) continue;
      int target = block.term.target;
      int threadGuard = 0;
      while (target >= 0 && threadGuard++ < 64 &&
             fn.blocks[static_cast<size_t>(target)].insts.empty() &&
             fn.blocks[static_cast<size_t>(target)].term.kind == TermKind::Jump) {
        const int next = fn.blocks[static_cast<size_t>(target)].term.target;
        if (next == target) break;
        target = next;
      }
      if (target != block.term.target) {
        block.term.target = target;
        changed = true;
      }
    }
    if (removeUnreachable(fn)) changed = true;
  }
}

}  // namespace

Function buildFunction(const Handler& handler, const std::string& name, DiagnosticSink& sink,
                       const std::string& file) {
  Function fn;
  fn.name = name;
  Builder builder(fn, sink, file);
  builder.build(handler);
  removeUnreachable(fn);
  buildSSA(fn);
  optimize(fn);
  return fn;
}

void buildSSA(Function& fn) {
  rebuildCFG(fn);
  const DomInfo dom = computeDominators(fn);
  insertPhis(fn, dom);
  rebuildCFG(fn);
  renameSSA(fn, dom);
  rebuildCFG(fn);
}

void optimize(Function& fn) {
  removeUnreachable(fn);
  foldConstants(fn);
  bool changed = true;
  int guard = 0;
  while (changed && guard++ < 8) {
    changed = false;
    changed = eliminateTrivialPhis(fn) || changed;
    foldConstants(fn);
    localCSE(fn);
    deadCodeElimination(fn);
  }
  simplifyCFG(fn);
  deadCodeElimination(fn);
}

std::string dumpFunction(const Function& fn) {
  std::ostringstream os;
  os << "function " << fn.name << " {\n";
  os << "  vars:";
  for (size_t i = 0; i < fn.vars.size(); ++i) {
    os << " v" << i << ":" << fn.vars[i].name << ":" << typeName(fn.vars[i].type);
    if (fn.vars[i].readOnly) os << "(ro)";
  }
  os << "\n";
  for (const auto& block : fn.blocks) {
    os << "  b" << block.id << ":";
    if (!block.pred.empty()) {
      os << " preds=[";
      for (size_t i = 0; i < block.pred.size(); ++i) {
        if (i) os << ",";
        os << "b" << block.pred[i];
      }
      os << "]";
    }
    os << "\n";
    for (const auto& inst : block.insts) {
      os << "    ";
      if (inst.dst >= 0) os << "v" << inst.dst << " = ";
      os << opName(inst.op);
      if (inst.op == Op::IntConst) os << " " << inst.intValue;
      if (inst.op == Op::FloatConst) os << " " << inst.floatValue;
      if (inst.op == Op::StrConst) os << " \"" << inst.text << "\"";
      if (inst.op == Op::BoolConst) os << " " << (inst.intValue ? "true" : "false");
      if (inst.op == Op::Call || inst.op == Op::Emit) os << " @" << inst.text;
      if (inst.op == Op::EventField) os << " @" << inst.text;
      if (inst.op == Op::LoadVar || inst.op == Op::StoreVar) os << " v" << inst.var;
      if (inst.op == Op::Phi) {
        os << " v" << inst.var << " [";
        for (size_t i = 0; i < inst.operands.size(); ++i) {
          if (i) os << ", ";
          os << "b" << inst.operandBlocks[i] << ":v" << inst.operands[i];
        }
        os << "]";
      }
      if (inst.a >= 0) os << " v" << inst.a;
      if (inst.b >= 0) os << ", v" << inst.b;
      for (int arg : inst.args) os << " v" << arg;
      if (inst.dst >= 0) os << "   ;" << inst.loc.line << ":" << inst.loc.col;
      os << "\n";
    }
    os << "    ";
    switch (block.term.kind) {
      case TermKind::End: os << "end"; break;
      case TermKind::Jump: os << "jump b" << block.term.target; break;
      case TermKind::Branch:
        os << "branch v" << block.term.cond << " ? b" << block.term.target << " : b"
           << block.term.falseTarget;
        break;
      case TermKind::WaitTime: os << "wait_time v" << block.term.value << " -> b" << block.term.target; break;
      case TermKind::WaitEvent:
        os << "wait_event " << block.term.eventName << " pred=" << block.term.predicate << " -> b"
           << block.term.target;
        break;
    }
    os << "\n";
  }
  os << "}\n";
  return os.str();
}

}  // namespace mfc::ir
