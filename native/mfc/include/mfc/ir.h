#pragma once

#include <string>
#include <vector>

#include "mfc/ast.h"
#include "mfc/common.h"
#include "mfc/predicate.h"

namespace mfc::ir {

enum class Op {
  Nop,
  IntConst,
  FloatConst,
  StrConst,
  BoolConst,
  LoadVar,
  StoreVar,
  Add,
  Sub,
  Mul,
  Div,
  Mod,
  Neg,
  Eq,
  Ne,
  Lt,
  Le,
  Gt,
  Ge,
  Not,
  Call,
  Emit,
  EventField,
  Phi,
};

struct Inst {
  Op op = Op::Nop;
  int dst = -1;
  int a = -1;
  int b = -1;
  int var = -1;                 // LoadVar / StoreVar / Phi variable slot
  int64_t intValue = 0;
  double floatValue = 0.0;
  std::string text;             // call/emit callee, field name, string constant
  std::vector<int> args;        // call / emit arguments
  std::vector<int> operands;    // phi operands, parallel to operandBlocks
  std::vector<int> operandBlocks;
  Loc loc;
  std::string nodeId;
};

enum class TermKind { End, Jump, Branch, WaitTime, WaitEvent };

struct Terminator {
  TermKind kind = TermKind::End;
  bool hasTerm = false;
  int cond = -1;
  int target = -1;         // jump / branch-true / wait resume
  int falseTarget = -1;    // branch-false
  int value = -1;          // wait time (seconds)
  std::string eventName;   // wait event
  int predicate = 0;       // 1-based index into Function::predicates
  Loc loc;
  std::string nodeId;
};

struct Block {
  int id = 0;
  std::vector<Inst> insts;
  Terminator term;
  std::vector<int> succ;
  std::vector<int> pred;
};

struct VarInfo {
  std::string name;
  Type type = Type::Error;
  bool readOnly = false;
};

struct Function {
  std::string name;
  std::vector<VarInfo> vars;
  std::vector<Block> blocks;
  int entry = 0;
  int valueCount = 0;
  std::vector<Predicate> predicates;
};

// AST -> CFG with mutable locals.
Function buildFunction(const Handler& handler, const std::string& name, DiagnosticSink& sink,
                       const std::string& file);

// CFG -> SSA: dominance frontiers, phi insertion, dominator-tree renaming.
void buildSSA(Function& fn);

// Constant folding, trivial-phi elimination, local value numbering/CSE,
// dead-code elimination and CFG cleanup (jump threading, unreachable blocks).
void optimize(Function& fn);

// Human-readable IR, used by tests and for interview evidence.
std::string dumpFunction(const Function& fn);

const char* opName(Op op);
bool isPure(Op op);

}  // namespace mfc::ir
