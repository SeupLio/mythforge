#include "mfc/module.h"

#include <array>
#include <cstring>

#include "mfc/bytecode.h"
#include "mfc/hash.h"

namespace mfc {

namespace {

constexpr uint32_t kFormatVersion = 1;
constexpr char kMagic[4] = {'M', 'F', 'B', 'M'};

class Writer {
 public:
  void raw(const void* data, size_t size) {
    buffer_.append(static_cast<const char*>(data), size);
  }
  void u8(uint8_t v) { buffer_.push_back(static_cast<char>(v)); }
  void u32(uint32_t v) {
    for (int i = 0; i < 4; ++i) buffer_.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
  }
  void i32(int32_t v) { u32(static_cast<uint32_t>(v)); }
  void i64(int64_t v) {
    for (int i = 0; i < 8; ++i) {
      buffer_.push_back(static_cast<char>((static_cast<uint64_t>(v) >> (8 * i)) & 0xFF));
    }
  }
  void f64(double v) {
    uint64_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    for (int i = 0; i < 8; ++i) {
      buffer_.push_back(static_cast<char>((bits >> (8 * i)) & 0xFF));
    }
  }
  void str(const std::string& s) {
    u32(static_cast<uint32_t>(s.size()));
    raw(s.data(), s.size());
  }
  const std::string& data() const { return buffer_; }

 private:
  std::string buffer_;
};

class Reader {
 public:
  Reader(const std::string& data) : data_(data) {}

  bool ok() const { return ok_; }
  size_t remaining() const { return data_.size() - pos_; }

  uint8_t u8() {
    if (!ensure(1)) return 0;
    return static_cast<uint8_t>(data_[pos_++]);
  }
  uint32_t u32() {
    if (!ensure(4)) return 0;
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
      value |= static_cast<uint32_t>(static_cast<uint8_t>(data_[pos_ + static_cast<size_t>(i)]))
               << (8 * i);
    }
    pos_ += 4;
    return value;
  }
  int32_t i32() { return static_cast<int32_t>(u32()); }
  int64_t i64() {
    if (!ensure(8)) return 0;
    uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
      value |= static_cast<uint64_t>(static_cast<uint8_t>(data_[pos_ + static_cast<size_t>(i)]))
               << (8 * i);
    }
    pos_ += 8;
    return static_cast<int64_t>(value);
  }
  double f64() {
    const uint64_t bits = static_cast<uint64_t>(i64());
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
  }
  std::string str() {
    const uint32_t length = u32();
    if (!ok_ || length > remaining()) {
      ok_ = false;
      return {};
    }
    std::string out = data_.substr(pos_, length);
    pos_ += length;
    return out;
  }

  void raw(char* out, size_t size) {
    if (!ensure(size)) return;
    std::memcpy(out, data_.data() + pos_, size);
    pos_ += size;
  }

 private:
  bool ensure(size_t size) {
    if (!ok_ || pos_ + size > data_.size()) {
      ok_ = false;
      return false;
    }
    return true;
  }

  const std::string& data_;
  size_t pos_ = 0;
  bool ok_ = true;
};

void writeConstant(Writer& writer, const Constant& value) {
  writer.u8(static_cast<uint8_t>(value.kind));
  switch (value.kind) {
    case Constant::Kind::Int: writer.i64(value.i); break;
    case Constant::Kind::Float: writer.f64(value.f); break;
    case Constant::Kind::String: writer.str(value.s); break;
  }
}

bool readConstant(Reader& reader, Constant& out) {
  out.kind = static_cast<Constant::Kind>(reader.u8());
  switch (out.kind) {
    case Constant::Kind::Int: out.i = reader.i64(); break;
    case Constant::Kind::Float: out.f = reader.f64(); break;
    case Constant::Kind::String: out.s = reader.str(); break;
    default: return false;
  }
  return reader.ok();
}

void writePredicate(Writer& writer, const Predicate& predicate) {
  writer.u32(static_cast<uint32_t>(predicate.code.size()));
  for (const auto& inst : predicate.code) {
    writer.u8(static_cast<uint8_t>(inst.op));
    switch (inst.op) {
      case PredOp::PushInt: writer.i64(inst.intValue); break;
      case PredOp::PushFloat: writer.f64(inst.floatValue); break;
      case PredOp::PushString:
      case PredOp::Field: writer.str(inst.text); break;
      case PredOp::PushBool: writer.u8(inst.intValue != 0 ? 1 : 0); break;
      default: break;
    }
  }
}

bool readPredicate(Reader& reader, Predicate& out) {
  const uint32_t count = reader.u32();
  if (count > 4096) return false;
  out.code.clear();
  out.code.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    PredInst inst;
    inst.op = static_cast<PredOp>(reader.u8());
    switch (inst.op) {
      case PredOp::PushInt: inst.intValue = reader.i64(); break;
      case PredOp::PushFloat: inst.floatValue = reader.f64(); break;
      case PredOp::PushString:
      case PredOp::Field: inst.text = reader.str(); break;
      case PredOp::PushBool: inst.intValue = reader.u8() != 0 ? 1 : 0; break;
      case PredOp::Eq:
      case PredOp::Ne:
      case PredOp::Lt:
      case PredOp::Le:
      case PredOp::Gt:
      case PredOp::Ge:
      case PredOp::And:
      case PredOp::Or:
      case PredOp::Not:
        break;
      default:
        return false;
    }
    if (!reader.ok()) return false;
    out.code.push_back(std::move(inst));
  }
  return true;
}

bool checkJumpTarget(const Chunk& chunk, uint32_t pc, int16_t offset, DiagnosticSink& sink,
                     const std::string& chunkName) {
  const int64_t target = static_cast<int64_t>(pc) + 1 + offset;
  if (target < 0 || target >= static_cast<int64_t>(chunk.code.size())) {
    sink.error("MFC4101", {},
               "chunk '" + chunkName + "' has a jump out of range at pc " + std::to_string(pc), {},
               {}, "<module>");
    return false;
  }
  return true;
}

const Constant* constantAt(const Chunk& chunk, uint8_t index) {
  if (index >= chunk.constants.size()) return nullptr;
  return &chunk.constants[static_cast<size_t>(index)];
}

}  // namespace

const char* moduleFormatVersion() { return "1"; }

const char* bc::opcodeName(uint8_t op) {
  switch (op) {
    case bc::NOP: return "nop";
    case bc::LOADK_I: return "loadk_i";
    case bc::LOADK_F: return "loadk_f";
    case bc::LOADK_S: return "loadk_s";
    case bc::LOADK_B: return "loadk_b";
    case bc::MOVE: return "move";
    case bc::ADD: return "add";
    case bc::SUB: return "sub";
    case bc::MUL: return "mul";
    case bc::DIV: return "div";
    case bc::MOD: return "mod";
    case bc::NEG: return "neg";
    case bc::NOT: return "not";
    case bc::EQ: return "eq";
    case bc::NE: return "ne";
    case bc::LT: return "lt";
    case bc::LE: return "le";
    case bc::GT: return "gt";
    case bc::GE: return "ge";
    case bc::CALL: return "call";
    case bc::EMIT: return "emit";
    case bc::WAIT_TIME: return "wait_time";
    case bc::WAIT_EVENT: return "wait_event";
    case bc::JUMP: return "jump";
    case bc::JUMP_IF_FALSE: return "jump_if_false";
    case bc::HALT: return "halt";
    case bc::EVENT_FIELD: return "event_field";
    default: return "?";
  }
}

bool bc::isValidOpcode(uint8_t op) { return op < bc::OP_COUNT; }

std::string serializeModule(const Module& module, DiagnosticSink& sink) {
  Writer writer;
  writer.raw(kMagic, sizeof(kMagic));
  writer.u32(kFormatVersion);
  writer.str(module.id);
  writer.str(module.sourcePath);
  writer.str(module.sourceHash);
  writer.u32(static_cast<uint32_t>(module.chunks.size()));
  for (const auto& chunk : module.chunks) {
    writer.str(chunk.name);
    writer.u32(chunk.frameSize);
    writer.u32(static_cast<uint32_t>(chunk.code.size()));
    for (uint32_t word : chunk.code) writer.u32(word);
    writer.u32(static_cast<uint32_t>(chunk.constants.size()));
    for (const auto& constant : chunk.constants) writeConstant(writer, constant);
    writer.u32(static_cast<uint32_t>(chunk.predicates.size()));
    for (const auto& predicate : chunk.predicates) writePredicate(writer, predicate);
    writer.u32(static_cast<uint32_t>(chunk.debug.size()));
    for (const auto& entry : chunk.debug) {
      writer.u32(entry.pc);
      writer.i32(entry.line);
      writer.i32(entry.col);
      writer.str(entry.nodeId);
    }
  }
  const std::string digest = sha256Hex(writer.data());
  writer.raw(digest.data(), digest.size());
  if (sink.hasErrors()) return {};
  return writer.data();
}
bool verifyModule(const Module& module, DiagnosticSink& sink) {
  bool ok = true;
  if (module.id.empty()) {
    sink.error("MFC4100", {}, "module id is empty", {}, {}, "<module>");
    ok = false;
  }
  std::vector<std::string> names;
  for (const auto& chunk : module.chunks) {
    if (chunk.name.empty()) {
      sink.error("MFC4102", {}, "chunk with empty name", {}, {}, "<module>");
      ok = false;
      continue;
    }
    for (const auto& existing : names) {
      if (existing == chunk.name) {
        sink.error("MFC4103", {}, "duplicate chunk '" + chunk.name + "'", {}, {}, "<module>");
        ok = false;
      }
    }
    names.push_back(chunk.name);

    if (chunk.frameSize == 0 || chunk.frameSize > 255) {
      sink.error("MFC4104", {},
                 "chunk '" + chunk.name + "' has invalid frame size " +
                     std::to_string(chunk.frameSize),
                 {}, {}, "<module>");
      ok = false;
    }
    if (chunk.code.empty()) {
      sink.error("MFC4105", {}, "chunk '" + chunk.name + "' has no code", {}, {}, "<module>");
      ok = false;
      continue;
    }
    if (chunk.constants.size() > 256) {
      sink.error("MFC4106", {}, "chunk '" + chunk.name + "' has too many constants", {}, {},
                 "<module>");
      ok = false;
    }
    if (chunk.predicates.size() > 255) {
      sink.error("MFC4107", {}, "chunk '" + chunk.name + "' has too many predicates", {}, {},
                 "<module>");
      ok = false;
    }
    const uint32_t frame = chunk.frameSize;
    for (uint32_t pc = 0; pc < chunk.code.size(); ++pc) {
      const uint32_t word = chunk.code[pc];
      const uint8_t op = bc::opOf(word);
      const uint8_t a = bc::aOf(word);
      const uint8_t b = bc::bOf(word);
      const uint8_t c = bc::cOf(word);
      auto fail = [&](const std::string& message) {
        sink.error("MFC4108", {},
                   "chunk '" + chunk.name + "' pc " + std::to_string(pc) + ": " + message, {},
                   {}, "<module>");
        ok = false;
      };
      if (!bc::isValidOpcode(op)) {
        fail("unknown opcode " + std::to_string(op));
        continue;
      }
      switch (op) {
        case bc::NOP:
        case bc::HALT:
          break;
        case bc::LOADK_I:
          if (a >= frame) fail("destination register out of range");
          if (const Constant* constant = constantAt(chunk, b);
              constant == nullptr || constant->kind != Constant::Kind::Int) {
            fail("integer constant index invalid");
          }
          break;
        case bc::LOADK_F:
          if (a >= frame) fail("destination register out of range");
          if (const Constant* constant = constantAt(chunk, b);
              constant == nullptr || constant->kind != Constant::Kind::Float) {
            fail("float constant index invalid");
          }
          break;
        case bc::LOADK_S:
        case bc::EVENT_FIELD:
          if (a >= frame) fail("destination register out of range");
          if (const Constant* constant = constantAt(chunk, b);
              constant == nullptr || constant->kind != Constant::Kind::String) {
            fail("string constant index invalid");
          }
          break;
        case bc::LOADK_B:
          if (a >= frame) fail("destination register out of range");
          if (b > 1) fail("boolean immediate must be 0 or 1");
          break;
        case bc::MOVE:
          if (a >= frame || b >= frame) fail("register out of range");
          break;
        case bc::ADD:
        case bc::SUB:
        case bc::MUL:
        case bc::DIV:
        case bc::MOD:
        case bc::EQ:
        case bc::NE:
        case bc::LT:
        case bc::LE:
        case bc::GT:
        case bc::GE:
          if (a >= frame || b >= frame || c >= frame) fail("register out of range");
          break;
        case bc::NEG:
        case bc::NOT:
          if (a >= frame || b >= frame) fail("register out of range");
          break;
        case bc::CALL: {
          if (a >= frame) fail("call base register out of range");
          if (static_cast<uint32_t>(a) + c >= frame) fail("call arguments exceed frame");
          if (const Constant* constant = constantAt(chunk, b);
              constant == nullptr || constant->kind != Constant::Kind::String) {
            fail("call name constant invalid");
          }
          break;
        }
        case bc::EMIT: {
          if (static_cast<uint32_t>(a) + c > frame) fail("emit arguments exceed frame");
          if (const Constant* constant = constantAt(chunk, b);
              constant == nullptr || constant->kind != Constant::Kind::String) {
            fail("emit name constant invalid");
          }
          break;
        }
        case bc::WAIT_TIME:
          if (a >= frame) fail("wait register out of range");
          break;
        case bc::WAIT_EVENT:
          if (const Constant* constant = constantAt(chunk, a);
              constant == nullptr || constant->kind != Constant::Kind::String) {
            fail("event name constant invalid");
          }
          if (b > chunk.predicates.size()) fail("predicate index out of range");
          break;
        case bc::JUMP:
          if (!checkJumpTarget(chunk, pc, bc::offsetOf(word), sink, chunk.name)) ok = false;
          break;
        case bc::JUMP_IF_FALSE:
          if (a >= frame) fail("branch condition register out of range");
          if (!checkJumpTarget(chunk, pc, bc::offsetOf(word), sink, chunk.name)) ok = false;
          break;
        default:
          fail("unhandled opcode in verifier");
          break;
      }
    }
    for (const auto& entry : chunk.debug) {
      if (entry.pc >= chunk.code.size()) {
        sink.error("MFC4109", {},
                   "chunk '" + chunk.name + "' debug entry points outside the code", {}, {},
                   "<module>");
        ok = false;
        break;
      }
    }
  }
  return ok;
}

bool deserializeModule(const std::string& bytes, Module& out, DiagnosticSink& sink) {
  if (bytes.size() < sizeof(kMagic) + 4 + 64) {
    sink.error("MFC4200", {}, "module file is truncated", {}, {}, "<module>");
    return false;
  }
  const std::string payload = bytes.substr(0, bytes.size() - 64);
  const std::string digest = bytes.substr(bytes.size() - 64);
  if (sha256Hex(payload) != digest) {
    sink.error("MFC4201", {}, "module checksum mismatch (package corrupted or tampered with)",
               {}, "rebuild the content", "<module>");
    return false;
  }
  if (std::memcmp(bytes.data(), kMagic, sizeof(kMagic)) != 0) {
    sink.error("MFC4202", {}, "bad module magic", {}, {}, "<module>");
    return false;
  }
  Reader reader(payload);
  char magic[4] = {};
  reader.raw(magic, sizeof(magic));
  const uint32_t version = reader.u32();
  if (version != kFormatVersion) {
    sink.error("MFC4203", {},
               "unsupported module version " + std::to_string(version), {}, {}, "<module>");
    return false;
  }
  out.chunks.clear();
  out.id = reader.str();
  out.sourcePath = reader.str();
  out.sourceHash = reader.str();
  const uint32_t chunkCount = reader.u32();
  if (!reader.ok() || chunkCount > 4096) {
    sink.error("MFC4204", {}, "module header is corrupted", {}, {}, "<module>");
    return false;
  }
  for (uint32_t i = 0; i < chunkCount; ++i) {
    Chunk chunk;
    chunk.name = reader.str();
    chunk.frameSize = reader.u32();
    const uint32_t codeSize = reader.u32();
    if (!reader.ok() || codeSize > 1u << 24) {
      sink.error("MFC4205", {}, "chunk header is corrupted", {}, {}, "<module>");
      return false;
    }
    chunk.code.reserve(codeSize);
    for (uint32_t j = 0; j < codeSize; ++j) chunk.code.push_back(reader.u32());
    const uint32_t constantCount = reader.u32();
    if (!reader.ok() || constantCount > 256) {
      sink.error("MFC4206", {}, "constant pool is corrupted", {}, {}, "<module>");
      return false;
    }
    for (uint32_t j = 0; j < constantCount; ++j) {
      Constant constant;
      if (!readConstant(reader, constant)) {
        sink.error("MFC4207", {}, "constant payload is corrupted", {}, {}, "<module>");
        return false;
      }
      chunk.constants.push_back(std::move(constant));
    }
    const uint32_t predicateCount = reader.u32();
    if (!reader.ok() || predicateCount > 255) {
      sink.error("MFC4208", {}, "predicate table is corrupted", {}, {}, "<module>");
      return false;
    }
    for (uint32_t j = 0; j < predicateCount; ++j) {
      Predicate predicate;
      if (!readPredicate(reader, predicate)) {
        sink.error("MFC4209", {}, "predicate payload is corrupted", {}, {}, "<module>");
        return false;
      }
      chunk.predicates.push_back(std::move(predicate));
    }
    const uint32_t debugCount = reader.u32();
    if (!reader.ok() || debugCount > 1u << 20) {
      sink.error("MFC4210", {}, "debug table is corrupted", {}, {}, "<module>");
      return false;
    }
    for (uint32_t j = 0; j < debugCount; ++j) {
      DebugEntry entry;
      entry.pc = reader.u32();
      entry.line = reader.i32();
      entry.col = reader.i32();
      entry.nodeId = reader.str();
      if (!reader.ok()) {
        sink.error("MFC4211", {}, "debug entry is corrupted", {}, {}, "<module>");
        return false;
      }
      chunk.debug.push_back(std::move(entry));
    }
    out.chunks.push_back(std::move(chunk));
  }
  if (!reader.ok()) {
    sink.error("MFC4212", {}, "module payload is corrupted", {}, {}, "<module>");
    return false;
  }
  return verifyModule(out, sink);
}

}  // namespace mfc
