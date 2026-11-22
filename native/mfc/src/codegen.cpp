#include "mfc/codegen.h"

#include <algorithm>
#include <cstdint>
#include <vector>

#include "mfc/bytecode.h"

namespace mfc {

namespace {

struct Move {
  int dst = 0;
  int src = 0;
};

// Parallel copy resolution. Phi nodes are evaluated simultaneously; a naive
// sequential copy order breaks swaps like `a<-b, b<-a`. When a cycle is found
// we park one destination in the reserved temp slot and rewire readers.
void emitParallelCopies(std::vector<Move> moves, int tempSlot, std::vector<uint32_t>& code) {
  int guard = 0;
  while (!moves.empty() && guard++ < 4096) {
    bool progress = false;
    for (size_t i = 0; i < moves.size(); ++i) {
      bool blocked = false;
      for (size_t j = 0; j < moves.size(); ++j) {
        if (i == j) continue;
        if (moves[j].src == moves[i].dst) {
          blocked = true;
          break;
        }
      }
      if (blocked) continue;
      if (moves[i].dst != moves[i].src) {
        code.push_back(bc::encodeABC(bc::MOVE, static_cast<uint8_t>(moves[i].dst),
                                     static_cast<uint8_t>(moves[i].src), 0));
      }
      moves.erase(moves.begin() + static_cast<std::ptrdiff_t>(i));
      progress = true;
      break;
    }
    if (!progress) {
      const int dst = moves.front().dst;
      code.push_back(bc::encodeABC(bc::MOVE, static_cast<uint8_t>(tempSlot),
                                   static_cast<uint8_t>(dst), 0));
      for (auto& move : moves) {
        if (move.src == dst) move.src = tempSlot;
      }
    }
  }
}

void rebuildCFG(ir::Function& fn) {
  for (auto& block : fn.blocks) {
    block.succ.clear();
    block.pred.clear();
  }
  for (auto& block : fn.blocks) {
    auto link = [&](int to) {
      if (to < 0) return;
      block.succ.push_back(to);
      fn.blocks[static_cast<size_t>(to)].pred.push_back(block.id);
    };
    switch (block.term.kind) {
      case ir::TermKind::Jump:
        link(block.term.target);
        break;
      case ir::TermKind::Branch:
        link(block.term.target);
        if (block.term.falseTarget != block.term.target) link(block.term.falseTarget);
        break;
      case ir::TermKind::WaitTime:
      case ir::TermKind::WaitEvent:
        link(block.term.target);
        break;
      case ir::TermKind::End:
        break;
    }
  }
}

bool blockStartsWithPhi(const ir::Block& block) {
  return !block.insts.empty() && block.insts.front().op == ir::Op::Phi;
}

// A conditional branch with a phi-carrying successor needs a dedicated edge
// block, otherwise the copies would also run on the other branch.
void splitCriticalEdges(ir::Function& fn) {
  const size_t original = fn.blocks.size();
  bool changed = false;
  for (size_t bi = 0; bi < original; ++bi) {
    if (fn.blocks[bi].succ.size() < 2) continue;
    const int trueTarget = fn.blocks[bi].term.target;
    const int falseTarget = fn.blocks[bi].term.falseTarget;
    if (trueTarget >= 0 && blockStartsWithPhi(fn.blocks[static_cast<size_t>(trueTarget)])) {
      const int edgeId = static_cast<int>(fn.blocks.size());
      ir::Block edge;
      edge.id = edgeId;
      edge.term.kind = ir::TermKind::Jump;
      edge.term.hasTerm = true;
      edge.term.target = trueTarget;
      fn.blocks.push_back(std::move(edge));
      fn.blocks[bi].term.target = edgeId;
      for (auto& inst : fn.blocks[static_cast<size_t>(trueTarget)].insts) {
        if (inst.op != ir::Op::Phi) break;
        for (auto& operandBlock : inst.operandBlocks) {
          if (operandBlock == static_cast<int>(bi)) operandBlock = edgeId;
        }
      }
      changed = true;
    }
    if (falseTarget >= 0 && falseTarget != trueTarget &&
        blockStartsWithPhi(fn.blocks[static_cast<size_t>(falseTarget)])) {
      const int edgeId = static_cast<int>(fn.blocks.size());
      ir::Block edge;
      edge.id = edgeId;
      edge.term.kind = ir::TermKind::Jump;
      edge.term.hasTerm = true;
      edge.term.target = falseTarget;
      fn.blocks.push_back(std::move(edge));
      fn.blocks[bi].term.falseTarget = edgeId;
      for (auto& inst : fn.blocks[static_cast<size_t>(falseTarget)].insts) {
        if (inst.op != ir::Op::Phi) break;
        for (auto& operandBlock : inst.operandBlocks) {
          if (operandBlock == static_cast<int>(bi)) operandBlock = edgeId;
        }
      }
      changed = true;
    }
  }
  if (changed) rebuildCFG(fn);
}

int phiSourceFor(const ir::Inst& phi, int predBlock) {
  for (size_t i = 0; i < phi.operands.size(); ++i) {
    if (i < phi.operandBlocks.size() && phi.operandBlocks[i] == predBlock) {
      return phi.operands[i];
    }
  }
  return -1;
}

bool constantEquals(const Constant& a, const Constant& b) {
  if (a.kind != b.kind) return false;
  switch (a.kind) {
    case Constant::Kind::Int: return a.i == b.i;
    case Constant::Kind::Float: return a.f == b.f;
    case Constant::Kind::String: return a.s == b.s;
  }
  return false;
}

struct Patch {
  uint32_t pc = 0;
  uint8_t op = 0;
  uint8_t reg = 0;
  int targetBlock = -1;
  Loc loc;
  std::string nodeId;
};

}  // namespace

bool generateChunk(const ir::Function& function, Chunk& out, DiagnosticSink& sink,
                   const std::string& file) {
  ir::Function fn = function;
  out.name = fn.name;
  out.code.clear();
  out.constants.clear();
  out.predicates = fn.predicates;
  out.debug.clear();

  splitCriticalEdges(fn);

  if (fn.predicates.size() > 255) {
    sink.error("MFC4001", {}, "chunk '" + fn.name + "' has more than 255 event predicates", {},
               "split the quest into smaller handlers", file);
    return false;
  }

  std::vector<int> slotOf(static_cast<size_t>(fn.valueCount), -1);
  int nextSlot = 0;
  auto slot = [&](int value) -> int {
    if (value < 0) return 0;
    if (slotOf[static_cast<size_t>(value)] < 0) {
      slotOf[static_cast<size_t>(value)] = nextSlot++;
    }
    return slotOf[static_cast<size_t>(value)];
  };

  int maxArgs = 0;
  for (const auto& block : fn.blocks) {
    for (const auto& inst : block.insts) {
      if (inst.dst >= 0) slot(inst.dst);
      slot(inst.a);
      slot(inst.b);
      for (int arg : inst.args) slot(arg);
      for (int operand : inst.operands) slot(operand);
      if (inst.op == ir::Op::Call || inst.op == ir::Op::Emit) {
        maxArgs = std::max(maxArgs, static_cast<int>(inst.args.size()));
      }
    }
    slot(block.term.cond);
    slot(block.term.value);
  }

  const int scratchBase = nextSlot;
  const int scratchSize = maxArgs + 1;  // CALL returns into the base slot
  const int tempSlot = scratchBase + scratchSize;
  const int frameSize = tempSlot + 1;
  if (frameSize > 255) {
    sink.error("MFC4003", {},
               "chunk '" + fn.name + "' needs " + std::to_string(frameSize) +
                   " registers (limit 255)",
               {}, "split the handler into smaller quests/functions", file);
    return false;
  }

  bool failed = false;
  auto reportOnce = [&](const std::string& code, const std::string& message,
                        const std::string& hint) {
    if (failed) return;
    failed = true;
    sink.error(code, {}, message, {}, hint, file);
  };

  auto internConstant = [&](Constant value) -> int {
    for (size_t i = 0; i < out.constants.size(); ++i) {
      if (constantEquals(out.constants[i], value)) return static_cast<int>(i);
    }
    if (out.constants.size() >= 256) {
      reportOnce("MFC4002", "chunk '" + fn.name + "' exceeds 256 constants",
                 "split the handler or reuse literals");
      return 0;
    }
    out.constants.push_back(std::move(value));
    return static_cast<int>(out.constants.size()) - 1;
  };
  auto internString = [&](const std::string& text) -> int {
    Constant value;
    value.kind = Constant::Kind::String;
    value.s = text;
    return internConstant(std::move(value));
  };
  auto internInt = [&](int64_t v) -> int {
    Constant value;
    value.kind = Constant::Kind::Int;
    value.i = v;
    return internConstant(std::move(value));
  };
  auto internFloat = [&](double v) -> int {
    Constant value;
    value.kind = Constant::Kind::Float;
    value.f = v;
    return internConstant(std::move(value));
  };

  auto addDebug = [&](uint32_t pc, const Loc& loc, const std::string& nodeId) {
    if (loc.line == 0 && nodeId.empty()) return;
    if (!out.debug.empty()) {
      const DebugEntry& last = out.debug.back();
      if (last.pc + 1 == pc && last.line == loc.line && last.col == loc.col &&
          last.nodeId == nodeId) {
        return;
      }
    }
    DebugEntry entry;
    entry.pc = pc;
    entry.line = loc.line;
    entry.col = loc.col;
    entry.nodeId = nodeId;
    out.debug.push_back(std::move(entry));
  };

  auto emitWord = [&](uint32_t word, const Loc& loc, const std::string& nodeId) {
    out.code.push_back(word);
    addDebug(static_cast<uint32_t>(out.code.size()) - 1, loc, nodeId);
  };
  auto emitABC = [&](uint8_t op, uint8_t a, uint8_t b, uint8_t c, const Loc& loc,
                     const std::string& nodeId) {
    emitWord(bc::encodeABC(op, a, b, c), loc, nodeId);
  };
  auto emitMove = [&](int dst, int src, const Loc& loc, const std::string& nodeId) {
    if (dst == src) return;
    emitABC(bc::MOVE, static_cast<uint8_t>(dst), static_cast<uint8_t>(src), 0, loc, nodeId);
  };

  const size_t blockCount = fn.blocks.size();
  std::vector<int> blockPc(blockCount, -1);
  std::vector<Patch> patches;

  for (size_t bi = 0; bi < blockCount; ++bi) {
    const ir::Block& block = fn.blocks[bi];
    blockPc[bi] = static_cast<int>(out.code.size());

    for (const auto& inst : block.insts) {
      if (inst.op == ir::Op::Phi) continue;
      const int dst = inst.dst >= 0 ? slot(inst.dst) : -1;
      switch (inst.op) {
        case ir::Op::Nop:
        case ir::Op::LoadVar:
        case ir::Op::StoreVar:
          break;
        case ir::Op::IntConst: {
          const int index = internInt(inst.intValue);
          emitABC(bc::LOADK_I, static_cast<uint8_t>(dst), static_cast<uint8_t>(index), 0,
                  inst.loc, inst.nodeId);
          break;
        }
        case ir::Op::FloatConst: {
          const int index = internFloat(inst.floatValue);
          emitABC(bc::LOADK_F, static_cast<uint8_t>(dst), static_cast<uint8_t>(index), 0,
                  inst.loc, inst.nodeId);
          break;
        }
        case ir::Op::StrConst: {
          const int index = internString(inst.text);
          emitABC(bc::LOADK_S, static_cast<uint8_t>(dst), static_cast<uint8_t>(index), 0,
                  inst.loc, inst.nodeId);
          break;
        }
        case ir::Op::BoolConst: {
          emitABC(bc::LOADK_B, static_cast<uint8_t>(dst),
                  static_cast<uint8_t>(inst.intValue != 0 ? 1 : 0), 0, inst.loc, inst.nodeId);
          break;
        }
        case ir::Op::Add:
        case ir::Op::Sub:
        case ir::Op::Mul:
        case ir::Op::Div:
        case ir::Op::Mod:
        case ir::Op::Eq:
        case ir::Op::Ne:
        case ir::Op::Lt:
        case ir::Op::Le:
        case ir::Op::Gt:
        case ir::Op::Ge: {
          uint8_t op = 0;
          switch (inst.op) {
            case ir::Op::Add: op = bc::ADD; break;
            case ir::Op::Sub: op = bc::SUB; break;
            case ir::Op::Mul: op = bc::MUL; break;
            case ir::Op::Div: op = bc::DIV; break;
            case ir::Op::Mod: op = bc::MOD; break;
            case ir::Op::Eq: op = bc::EQ; break;
            case ir::Op::Ne: op = bc::NE; break;
            case ir::Op::Lt: op = bc::LT; break;
            case ir::Op::Le: op = bc::LE; break;
            case ir::Op::Gt: op = bc::GT; break;
            default: op = bc::GE; break;
          }
          emitABC(op, static_cast<uint8_t>(dst), static_cast<uint8_t>(slot(inst.a)),
                  static_cast<uint8_t>(slot(inst.b)), inst.loc, inst.nodeId);
          break;
        }
        case ir::Op::Neg:
        case ir::Op::Not:
          emitABC(inst.op == ir::Op::Neg ? bc::NEG : bc::NOT, static_cast<uint8_t>(dst),
                  static_cast<uint8_t>(slot(inst.a)), 0, inst.loc, inst.nodeId);
          break;
        case ir::Op::EventField: {
          const int index = internString(inst.text);
          emitABC(bc::EVENT_FIELD, static_cast<uint8_t>(dst), static_cast<uint8_t>(index), 0,
                  inst.loc, inst.nodeId);
          break;
        }
        case ir::Op::Call: {
          for (size_t i = 0; i < inst.args.size(); ++i) {
            emitMove(scratchBase + 1 + static_cast<int>(i), slot(inst.args[i]), inst.loc,
                     inst.nodeId);
          }
          const int nameIndex = internString(inst.text);
          emitABC(bc::CALL, static_cast<uint8_t>(scratchBase), static_cast<uint8_t>(nameIndex),
                  static_cast<uint8_t>(inst.args.size()), inst.loc, inst.nodeId);
          if (dst >= 0) emitMove(dst, scratchBase, inst.loc, inst.nodeId);
          break;
        }
        case ir::Op::Emit: {
          for (size_t i = 0; i < inst.args.size(); ++i) {
            emitMove(scratchBase + static_cast<int>(i), slot(inst.args[i]), inst.loc,
                     inst.nodeId);
          }
          const int nameIndex = internString(inst.text);
          emitABC(bc::EMIT, static_cast<uint8_t>(scratchBase), static_cast<uint8_t>(nameIndex),
                  static_cast<uint8_t>(inst.args.size()), inst.loc, inst.nodeId);
          break;
        }
        case ir::Op::Phi:
          break;
      }
    }

    // Phi copies for the single successor edge.
    if (block.succ.size() == 1) {
      const int succ = block.succ[0];
      std::vector<Move> moves;
      for (const auto& inst : fn.blocks[static_cast<size_t>(succ)].insts) {
        if (inst.op != ir::Op::Phi) break;
        const int src = phiSourceFor(inst, block.id);
        if (src >= 0) {
          Move move;
          move.dst = slot(inst.dst);
          move.src = slot(src);
          moves.push_back(move);
        }
      }
      if (!moves.empty()) {
        const size_t before = out.code.size();
        emitParallelCopies(moves, tempSlot, out.code);
        if (out.code.size() > before) {
          addDebug(static_cast<uint32_t>(out.code.size()) - 1, block.term.loc,
                   block.term.nodeId);
        }
      }
    }

    const ir::Terminator& term = block.term;
    switch (term.kind) {
      case ir::TermKind::End:
        emitABC(bc::HALT, 0, 0, 0, term.loc, term.nodeId);
        break;
      case ir::TermKind::Jump: {
        Patch patch;
        patch.pc = static_cast<uint32_t>(out.code.size());
        patch.op = bc::JUMP;
        patch.targetBlock = term.target;
        patch.loc = term.loc;
        patch.nodeId = term.nodeId;
        patches.push_back(patch);
        emitWord(bc::encodeJump(bc::JUMP, 0, 0), term.loc, term.nodeId);
        break;
      }
      case ir::TermKind::Branch: {
        Patch falsePatch;
        falsePatch.pc = static_cast<uint32_t>(out.code.size());
        falsePatch.op = bc::JUMP_IF_FALSE;
        falsePatch.reg = static_cast<uint8_t>(slot(term.cond));
        falsePatch.targetBlock = term.falseTarget;
        falsePatch.loc = term.loc;
        falsePatch.nodeId = term.nodeId;
        patches.push_back(falsePatch);
        emitWord(bc::encodeJump(bc::JUMP_IF_FALSE, falsePatch.reg, 0), term.loc, term.nodeId);

        Patch truePatch;
        truePatch.pc = static_cast<uint32_t>(out.code.size());
        truePatch.op = bc::JUMP;
        truePatch.targetBlock = term.target;
        truePatch.loc = term.loc;
        truePatch.nodeId = term.nodeId;
        patches.push_back(truePatch);
        emitWord(bc::encodeJump(bc::JUMP, 0, 0), term.loc, term.nodeId);
        break;
      }
      case ir::TermKind::WaitTime: {
        emitABC(bc::WAIT_TIME, static_cast<uint8_t>(slot(term.value)), 0, 0, term.loc,
                term.nodeId);
        Patch patch;
        patch.pc = static_cast<uint32_t>(out.code.size());
        patch.op = bc::JUMP;
        patch.targetBlock = term.target;
        patch.loc = term.loc;
        patch.nodeId = term.nodeId;
        patches.push_back(patch);
        emitWord(bc::encodeJump(bc::JUMP, 0, 0), term.loc, term.nodeId);
        break;
      }
      case ir::TermKind::WaitEvent: {
        const int nameIndex = internString(term.eventName);
        emitABC(bc::WAIT_EVENT, static_cast<uint8_t>(nameIndex),
                static_cast<uint8_t>(term.predicate), 0, term.loc, term.nodeId);
        Patch patch;
        patch.pc = static_cast<uint32_t>(out.code.size());
        patch.op = bc::JUMP;
        patch.targetBlock = term.target;
        patch.loc = term.loc;
        patch.nodeId = term.nodeId;
        patches.push_back(patch);
        emitWord(bc::encodeJump(bc::JUMP, 0, 0), term.loc, term.nodeId);
        break;
      }
    }
  }

  for (const auto& patch : patches) {
    if (patch.targetBlock < 0 || static_cast<size_t>(patch.targetBlock) >= blockCount ||
        blockPc[static_cast<size_t>(patch.targetBlock)] < 0) {
      reportOnce("MFC4004", "chunk '" + fn.name + "' contains a jump to an unknown block", "");
      return false;
    }
    const int64_t offset =
        static_cast<int64_t>(blockPc[static_cast<size_t>(patch.targetBlock)]) -
        static_cast<int64_t>(patch.pc + 1);
    if (offset < -32768 || offset > 32767) {
      reportOnce("MFC4005",
                 "chunk '" + fn.name + "' jump distance exceeds 16 bits (" +
                     std::to_string(offset) + ")",
                 "split the handler; wide jumps are a documented v1 limit");
      return false;
    }
    out.code[patch.pc] =
        bc::encodeJump(patch.op, patch.reg, static_cast<int16_t>(offset));
  }

  if (failed) return false;
  if (out.code.empty()) {
    out.code.push_back(bc::encodeABC(bc::HALT, 0, 0, 0));
  }
  out.frameSize = static_cast<uint32_t>(frameSize);
  return true;
}

}  // namespace mfc
