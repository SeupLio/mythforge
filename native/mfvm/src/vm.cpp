#include "mfvm/vm.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "mfc/bytecode.h"

namespace mfvm {

namespace {

std::string formatFloat(double value) {
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%.17g", value);
  return std::string(buffer);
}

}  // namespace

VM::VM() = default;

VM::~VM() { unloadAll(); }

void VM::unloadAll() {
  for (Instance* instance : instances_) delete instance;
  instances_.clear();
  freeInstances_.clear();
  ready_.clear();
  while (!timers_.empty()) timers_.pop();
  waiters_.clear();
  module_ = mfc::Module();
  chunkIndex_.clear();
  strings_.clear();
  stringIds_.clear();
  now_ = 0;
  nextInstanceId_ = 1;
  nextTimerSeq_ = 1;
  lastError_.clear();
}

uint32_t VM::intern(const std::string& text) {
  auto it = stringIds_.find(text);
  if (it != stringIds_.end()) return it->second;
  const uint32_t id = static_cast<uint32_t>(strings_.size());
  strings_.push_back(text);
  stringIds_[text] = id;
  return id;
}

Value VM::stringValue(const std::string& text) { return Value::string(intern(text)); }

const std::string& VM::stringOf(uint32_t id) const {
  static const std::string kEmpty;
  if (id >= strings_.size()) return kEmpty;
  return strings_[id];
}

std::string VM::valueToString(const Value& value) const {
  switch (value.tag) {
    case ValueTag::Nil: return "nil";
    case ValueTag::Int: return std::to_string(value.as.i);
    case ValueTag::Float: return formatFloat(value.as.f);
    case ValueTag::Bool: return value.as.b != 0 ? "true" : "false";
    case ValueTag::String: return stringOf(value.as.s);
  }
  return "<invalid>";
}

bool VM::loadModule(const mfc::Module& module, std::string* error) {
  unloadAll();
  module_ = module;
  for (size_t i = 0; i < module_.chunks.size(); ++i) {
    const mfc::Chunk& chunk = module_.chunks[i];
    if (chunkIndex_.count(chunk.name) != 0) {
      if (error) *error = "duplicate chunk '" + chunk.name + "'";
      return false;
    }
    chunkIndex_[chunk.name] = static_cast<int>(i);
    for (const auto& constant : chunk.constants) {
      if (constant.kind == mfc::Constant::Kind::String) intern(constant.s);
    }
    for (const auto& predicate : chunk.predicates) {
      for (const auto& inst : predicate.code) {
        if (inst.op == mfc::PredOp::PushString || inst.op == mfc::PredOp::Field) {
          intern(inst.text);
        }
      }
    }
  }
  return true;
}

bool VM::hasChunk(const std::string& name) const { return chunkIndex_.count(name) != 0; }

const mfc::Chunk* VM::chunkOf(const Instance& instance) const {
  if (instance.chunk < 0 || static_cast<size_t>(instance.chunk) >= module_.chunks.size()) {
    return nullptr;
  }
  return &module_.chunks[static_cast<size_t>(instance.chunk)];
}

std::string VM::nodeIdFor(const mfc::Chunk& chunk, uint32_t pc) const {
  const mfc::DebugEntry* best = nullptr;
  for (const auto& entry : chunk.debug) {
    if (entry.pc > pc) continue;
    if (best == nullptr || entry.pc >= best->pc) best = &entry;
  }
  return best != nullptr ? best->nodeId : std::string();
}

void VM::emitTrace(const std::string& kind, const std::string& name, const std::string& nodeId,
                   const std::vector<Value>& args) {
  if (!trace_) return;
  TraceEvent event;
  event.kind = kind;
  event.name = name;
  event.nodeId = nodeId;
  event.args = args;
  event.atMicros = now_;
  trace_(event);
}

VM::Instance* VM::allocateInstance() {
  Instance* instance = nullptr;
  if (!freeInstances_.empty()) {
    instance = freeInstances_.back();
    freeInstances_.pop_back();
  } else {
    instance = new Instance();
    instances_.push_back(instance);
  }
  instance->id = nextInstanceId_++;
  instance->pc = 0;
  instance->state = Instance::State::Ready;
  instance->wakeAt = 0;
  instance->timerSeq = 0;
  instance->predicate = 0;
  instance->waitEvent.clear();
  instance->eventPayload.clear();
  return instance;
}

void VM::releaseInstance(Instance* instance) {
  instance->state = Instance::State::Done;
  instance->regs.clear();
  instance->eventPayload.clear();
  freeInstances_.push_back(instance);
}

uint64_t VM::startChunk(const std::string& name) {
  auto it = chunkIndex_.find(name);
  if (it == chunkIndex_.end()) {
    lastError_ = "unknown chunk '" + name + "'";
    return 0;
  }
  Instance* instance = allocateInstance();
  const mfc::Chunk& chunk = module_.chunks[static_cast<size_t>(it->second)];
  instance->chunk = it->second;
  instance->regs.assign(chunk.frameSize, Value::nil());
  ready_.push_back(instance);
  emitTrace("start", name, nodeIdFor(chunk, 0), {});
  return instance->id;
}

int VM::startQuest() {
  int started = 0;
  const std::string prefix = module_.id + "/";
  for (size_t i = 0; i < module_.chunks.size(); ++i) {
    const std::string& name = module_.chunks[i].name;
    if (name.compare(0, prefix.size(), prefix) != 0) continue;
    if (name.find("/on_") == std::string::npos) continue;
    if (startChunk(name) != 0) ++started;
  }
  const std::string startName = prefix + "start";
  if (chunkIndex_.count(startName) != 0) {
    if (startChunk(startName) != 0) ++started;
  }
  // Skill modules are entered through "<id>/cast"; the runtime treats it as the
  // same kind of entry point so one VM can host quests, dialogue and skills.
  const std::string castName = prefix + "cast";
  if (chunkIndex_.count(castName) != 0) {
    if (startChunk(castName) != 0) ++started;
  }
  return started;
}

void VM::fault(Instance* instance, const std::string& message) {
  const mfc::Chunk* chunk = chunkOf(*instance);
  lastError_ = "chunk '" + (chunk ? chunk->name : std::string("<unknown>")) + "' pc " +
               std::to_string(instance->pc) + ": " + message;
  instance->state = Instance::State::Faulted;
  if (chunk != nullptr) {
    emitTrace("fault", message, nodeIdFor(*chunk, instance->pc), {});
  }
}

bool VM::evaluatePredicate(const mfc::Predicate& predicate,
                           const std::vector<std::pair<std::string, Value>>& fields) {
  std::vector<Value> stack;
  auto pop = [&](Value& out) -> bool {
    if (stack.empty()) return false;
    out = stack.back();
    stack.pop_back();
    return true;
  };
  for (const auto& inst : predicate.code) {
    switch (inst.op) {
      case mfc::PredOp::PushInt:
        stack.push_back(Value::integer(inst.intValue));
        break;
      case mfc::PredOp::PushFloat:
        stack.push_back(Value::number(inst.floatValue));
        break;
      case mfc::PredOp::PushString:
        stack.push_back(Value::string(intern(inst.text)));
        break;
      case mfc::PredOp::PushBool:
        stack.push_back(Value::boolean(inst.intValue != 0));
        break;
      case mfc::PredOp::Field: {
        bool found = false;
        for (const auto& field : fields) {
          if (field.first == inst.text) {
            stack.push_back(field.second);
            found = true;
            break;
          }
        }
        if (!found) return false;
        break;
      }
      case mfc::PredOp::Not: {
        Value value;
        if (!pop(value) || !value.isBool()) return false;
        stack.push_back(Value::boolean(value.as.b == 0));
        break;
      }
      case mfc::PredOp::And:
      case mfc::PredOp::Or: {
        Value b;
        Value a;
        if (!pop(b) || !pop(a) || !a.isBool() || !b.isBool()) return false;
        const bool result = inst.op == mfc::PredOp::And ? (a.as.b != 0 && b.as.b != 0)
                                                        : (a.as.b != 0 || b.as.b != 0);
        stack.push_back(Value::boolean(result));
        break;
      }
      default: {
        Value b;
        Value a;
        if (!pop(b) || !pop(a)) return false;
        bool result = false;
        if (a.isNumber() && b.isNumber()) {
          const double lhs = a.asDouble();
          const double rhs = b.asDouble();
          switch (inst.op) {
            case mfc::PredOp::Eq: result = lhs == rhs; break;
            case mfc::PredOp::Ne: result = lhs != rhs; break;
            case mfc::PredOp::Lt: result = lhs < rhs; break;
            case mfc::PredOp::Le: result = lhs <= rhs; break;
            case mfc::PredOp::Gt: result = lhs > rhs; break;
            case mfc::PredOp::Ge: result = lhs >= rhs; break;
            default: return false;
          }
        } else if (a.isString() && b.isString()) {
          const std::string& lhs = stringOf(a.as.s);
          const std::string& rhs = stringOf(b.as.s);
          switch (inst.op) {
            case mfc::PredOp::Eq: result = lhs == rhs; break;
            case mfc::PredOp::Ne: result = lhs != rhs; break;
            case mfc::PredOp::Lt: result = lhs < rhs; break;
            case mfc::PredOp::Le: result = lhs <= rhs; break;
            case mfc::PredOp::Gt: result = lhs > rhs; break;
            case mfc::PredOp::Ge: result = lhs >= rhs; break;
            default: return false;
          }
        } else if (a.isBool() && b.isBool()) {
          if (inst.op == mfc::PredOp::Eq) result = a.as.b == b.as.b;
          else if (inst.op == mfc::PredOp::Ne) result = a.as.b != b.as.b;
          else return false;
        } else {
          return false;
        }
        stack.push_back(Value::boolean(result));
        break;
      }
    }
  }
  return stack.size() == 1 && stack[0].isBool() && stack[0].as.b != 0;
}

void VM::deliver(const std::string& name, const std::vector<std::pair<std::string, Value>>& fields) {
  auto it = waiters_.find(name);
  if (it == waiters_.end()) return;
  std::vector<Instance*> remaining;
  remaining.reserve(it->second.size());
  for (Instance* instance : it->second) {
    if (instance->state != Instance::State::WaitingEvent) continue;
    bool matched = true;
    if (instance->predicate > 0) {
      const mfc::Chunk* chunk = chunkOf(*instance);
      const int index = instance->predicate - 1;
      if (chunk == nullptr || index < 0 || static_cast<size_t>(index) >= chunk->predicates.size()) {
        matched = false;
      } else {
        matched = evaluatePredicate(chunk->predicates[static_cast<size_t>(index)], fields);
      }
    }
    if (!matched) {
      remaining.push_back(instance);
      continue;
    }
    instance->state = Instance::State::Ready;
    instance->eventPayload = fields;
    instance->waitEvent.clear();
    instance->predicate = 0;
    ready_.push_back(instance);
  }
  if (remaining.empty()) {
    waiters_.erase(it);
  } else {
    it->second = std::move(remaining);
  }
}

bool VM::emitEvent(const std::string& name,
                   const std::vector<std::pair<std::string, Value>>& fields) {
  if (chunkIndex_.empty()) {
    lastError_ = "no module loaded";
    return false;
  }
  deliver(name, fields);
  return true;
}

void VM::advanceTime(int64_t deltaMicros) {
  if (deltaMicros > 0) now_ += deltaMicros;
}

void VM::execute(Instance* instance, int64_t& stepBudget, RunStats& stats) {
  const mfc::Chunk* chunkPtr = chunkOf(*instance);
  if (chunkPtr == nullptr) {
    fault(instance, "invalid chunk index");
    ++stats.faults;
    return;
  }
  const mfc::Chunk& chunk = *chunkPtr;
  std::vector<Value>& regs = instance->regs;
  uint32_t pc = instance->pc;

  while (true) {
    if (stepBudget <= 0) {
      instance->pc = pc;
      instance->state = Instance::State::Ready;
      ready_.push_front(instance);
      return;
    }
    if (pc >= chunk.code.size()) {
      instance->pc = pc;
      fault(instance, "pc out of range");
      ++stats.faults;
      return;
    }
    const uint32_t word = chunk.code[pc++];
    --stepBudget;
    ++stats.steps;
    const uint8_t op = mfc::bc::opOf(word);
    const uint8_t a = mfc::bc::aOf(word);
    const uint8_t b = mfc::bc::bOf(word);
    const uint8_t c = mfc::bc::cOf(word);

    switch (op) {
      case mfc::bc::NOP:
        break;
      case mfc::bc::LOADK_I: {
        const mfc::Constant& constant = chunk.constants[b];
        regs[a] = Value::integer(constant.i);
        break;
      }
      case mfc::bc::LOADK_F: {
        const mfc::Constant& constant = chunk.constants[b];
        regs[a] = Value::number(constant.f);
        break;
      }
      case mfc::bc::LOADK_S: {
        const mfc::Constant& constant = chunk.constants[b];
        regs[a] = Value::string(intern(constant.s));
        break;
      }
      case mfc::bc::LOADK_B:
        regs[a] = Value::boolean(b != 0);
        break;
      case mfc::bc::MOVE:
        regs[a] = regs[b];
        break;
      case mfc::bc::ADD:
      case mfc::bc::SUB:
      case mfc::bc::MUL:
      case mfc::bc::DIV:
      case mfc::bc::MOD: {
        const Value& lhs = regs[b];
        const Value& rhs = regs[c];
        if (!lhs.isNumber() || !rhs.isNumber()) {
          instance->pc = pc;
          fault(instance, std::string("arithmetic on non-numeric value (") +
                              mfc::bc::opcodeName(op) + ")");
          ++stats.faults;
          return;
        }
        if (op == mfc::bc::MOD) {
          if (!lhs.isInt() || !rhs.isInt()) {
            instance->pc = pc;
            fault(instance, "'%' requires integer operands");
            ++stats.faults;
            return;
          }
          if (rhs.as.i == 0) {
            instance->pc = pc;
            fault(instance, "integer modulo by zero");
            ++stats.faults;
            return;
          }
          regs[a] = Value::integer(lhs.as.i % rhs.as.i);
          break;
        }
        if (op == mfc::bc::DIV && ((lhs.isInt() && rhs.isInt() && rhs.as.i == 0) ||
                                   (rhs.isFloat() && rhs.as.f == 0.0))) {
          instance->pc = pc;
          fault(instance, "division by zero");
          ++stats.faults;
          return;
        }
        const bool intMath = lhs.isInt() && rhs.isInt();
        if (intMath) {
          const uint64_t x = static_cast<uint64_t>(lhs.as.i);
          const uint64_t y = static_cast<uint64_t>(rhs.as.i);
          int64_t result = 0;
          if (op == mfc::bc::ADD) result = static_cast<int64_t>(x + y);
          else if (op == mfc::bc::SUB) result = static_cast<int64_t>(x - y);
          else if (op == mfc::bc::MUL) result = static_cast<int64_t>(x * y);
          else result = lhs.as.i / rhs.as.i;
          regs[a] = Value::integer(result);
        } else {
          const double x = lhs.asDouble();
          const double y = rhs.asDouble();
          double result = 0.0;
          if (op == mfc::bc::ADD) result = x + y;
          else if (op == mfc::bc::SUB) result = x - y;
          else if (op == mfc::bc::MUL) result = x * y;
          else result = x / y;
          regs[a] = Value::number(result);
        }
        break;
      }
      case mfc::bc::NEG: {
        if (regs[b].isInt()) {
          regs[a] = Value::integer(-regs[b].as.i);
        } else if (regs[b].isFloat()) {
          regs[a] = Value::number(-regs[b].as.f);
        } else {
          instance->pc = pc;
          fault(instance, "unary '-' on non-numeric value");
          ++stats.faults;
          return;
        }
        break;
      }
      case mfc::bc::NOT: {
        if (!regs[b].isBool()) {
          instance->pc = pc;
          fault(instance, "'!' on non-boolean value");
          ++stats.faults;
          return;
        }
        regs[a] = Value::boolean(regs[b].as.b == 0);
        break;
      }
      case mfc::bc::EQ:
      case mfc::bc::NE:
      case mfc::bc::LT:
      case mfc::bc::LE:
      case mfc::bc::GT:
      case mfc::bc::GE: {
        const Value& lhs = regs[b];
        const Value& rhs = regs[c];
        bool result = false;
        if (lhs.isNumber() && rhs.isNumber()) {
          const double x = lhs.asDouble();
          const double y = rhs.asDouble();
          if (op == mfc::bc::EQ) result = x == y;
          else if (op == mfc::bc::NE) result = x != y;
          else if (op == mfc::bc::LT) result = x < y;
          else if (op == mfc::bc::LE) result = x <= y;
          else if (op == mfc::bc::GT) result = x > y;
          else result = x >= y;
        } else if (lhs.isString() && rhs.isString()) {
          if (op == mfc::bc::EQ) result = lhs.as.s == rhs.as.s;
          else if (op == mfc::bc::NE) result = lhs.as.s != rhs.as.s;
          else {
            const std::string& x = stringOf(lhs.as.s);
            const std::string& y = stringOf(rhs.as.s);
            if (op == mfc::bc::LT) result = x < y;
            else if (op == mfc::bc::LE) result = x <= y;
            else if (op == mfc::bc::GT) result = x > y;
            else result = x >= y;
          }
        } else if (lhs.isBool() && rhs.isBool()) {
          if (op == mfc::bc::EQ) result = lhs.as.b == rhs.as.b;
          else if (op == mfc::bc::NE) result = lhs.as.b != rhs.as.b;
          else {
            instance->pc = pc;
            fault(instance, "ordered comparison on booleans");
            ++stats.faults;
            return;
          }
        } else {
          instance->pc = pc;
          fault(instance, "comparison between incompatible values");
          ++stats.faults;
          return;
        }
        regs[a] = Value::boolean(result);
        break;
      }
      case mfc::bc::CALL: {
        const std::string& name = chunk.constants[b].s;
        std::vector<Value> args;
        args.reserve(c);
        for (uint8_t i = 0; i < c; ++i) args.push_back(regs[a + 1 + i]);
        emitTrace("call", name, nodeIdFor(chunk, pc - 1), args);
        Value result = host_ ? host_->call(name, args) : Value::nil();
        regs[a] = result;
        break;
      }
      case mfc::bc::EMIT: {
        const std::string& name = chunk.constants[b].s;
        std::vector<std::pair<std::string, Value>> fields;
        std::vector<Value> args;
        fields.reserve(c);
        args.reserve(c);
        for (uint8_t i = 0; i < c; ++i) {
          fields.emplace_back("#" + std::to_string(i), regs[a + i]);
          args.push_back(regs[a + i]);
        }
        emitTrace("emit", name, nodeIdFor(chunk, pc - 1), args);
        if (host_ != nullptr) host_->emit(name, args);
        deliver(name, fields);
        break;
      }
      case mfc::bc::WAIT_TIME: {
        if (!regs[a].isNumber()) {
          instance->pc = pc;
          fault(instance, "wait time requires a number");
          ++stats.faults;
          return;
        }
        double seconds = regs[a].asDouble();
        if (seconds < 0.0) seconds = 0.0;
        emitTrace("wait", "", nodeIdFor(chunk, pc - 1), {Value::number(seconds)});
        instance->pc = pc;
        instance->state = Instance::State::WaitingTime;
        instance->wakeAt = now_ + static_cast<int64_t>(seconds * 1000000.0);
        instance->timerSeq = nextTimerSeq_++;
        Timer timer;
        timer.wakeAt = instance->wakeAt;
        timer.seq = instance->timerSeq;
        timer.instance = instance;
        timers_.push(timer);
        return;
      }
      case mfc::bc::WAIT_EVENT: {
        const std::string& name = chunk.constants[a].s;
        emitTrace("wait", name, nodeIdFor(chunk, pc - 1), {});
        instance->pc = pc;
        instance->state = Instance::State::WaitingEvent;
        instance->waitEvent = name;
        instance->predicate = b;
        waiters_[name].push_back(instance);
        return;
      }
      case mfc::bc::JUMP:
        pc = static_cast<uint32_t>(static_cast<int64_t>(pc) + mfc::bc::offsetOf(word));
        break;
      case mfc::bc::JUMP_IF_FALSE: {
        if (!regs[a].isBool()) {
          instance->pc = pc;
          fault(instance, "branch condition is not a boolean");
          ++stats.faults;
          return;
        }
        if (regs[a].as.b == 0) {
          pc = static_cast<uint32_t>(static_cast<int64_t>(pc) + mfc::bc::offsetOf(word));
        }
        break;
      }
      case mfc::bc::EVENT_FIELD: {
        const std::string& fieldName = chunk.constants[b].s;
        Value value = Value::nil();
        for (const auto& field : instance->eventPayload) {
          if (field.first == fieldName) {
            value = field.second;
            break;
          }
        }
        regs[a] = value;
        break;
      }
      case mfc::bc::HALT:
        instance->pc = pc;
        releaseInstance(instance);
        return;
      default:
        instance->pc = pc;
        fault(instance, "unknown opcode " + std::to_string(op));
        ++stats.faults;
        return;
    }
  }
}

RunStats VM::runUntilIdle(int64_t maxSteps) {
  RunStats stats;
  int64_t budget = maxSteps;
  while (true) {
    while (!ready_.empty()) {
      Instance* instance = ready_.front();
      ready_.pop_front();
      if (instance->state != Instance::State::Ready) continue;
      instance->state = Instance::State::Running;
      execute(instance, budget, stats);
      if (budget <= 0) {
        stats.budgetExceeded = true;
        return stats;
      }
    }
    bool woke = false;
    while (!timers_.empty() && timers_.top().wakeAt <= now_) {
      Instance* instance = timers_.top().instance;
      timers_.pop();
      if (instance->state == Instance::State::WaitingTime) {
        instance->state = Instance::State::Ready;
        ready_.push_back(instance);
        woke = true;
      }
    }
    if (!woke) break;
  }
  return stats;
}

RunStats VM::runUntilAllIdle(int64_t maxSteps) {
  RunStats total;
  int64_t remaining = maxSteps;
  int guard = 0;
  while (guard++ < 100000) {
    RunStats stats = runUntilIdle(remaining);
    total.steps += stats.steps;
    total.faults += stats.faults;
    remaining -= stats.steps;
    if (stats.budgetExceeded || remaining <= 0) {
      total.budgetExceeded = true;
      break;
    }
    if (timers_.empty()) break;
    if (!hasLiveInstances()) break;
    now_ = timers_.top().wakeAt;
  }
  return total;
}

bool VM::hasLiveInstances() const {
  for (const Instance* instance : instances_) {
    if (instance->state != Instance::State::Done &&
        instance->state != Instance::State::Faulted) {
      return true;
    }
  }
  return false;
}

size_t VM::liveInstanceCount() const {
  size_t count = 0;
  for (const Instance* instance : instances_) {
    if (instance->state != Instance::State::Done &&
        instance->state != Instance::State::Faulted) {
      ++count;
    }
  }
  return count;
}

}  // namespace mfvm
