#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "mfc/common.h"
#include "mfc/predicate.h"

namespace mfc {

struct Constant {
  enum class Kind : uint8_t { Int = 1, Float = 2, String = 3 };
  Kind kind = Kind::Int;
  int64_t i = 0;
  double f = 0.0;
  std::string s;
};

struct DebugEntry {
  uint32_t pc = 0;
  int32_t line = 0;
  int32_t col = 0;
  std::string nodeId;
};

struct Chunk {
  std::string name;
  uint32_t frameSize = 0;
  std::vector<uint32_t> code;
  std::vector<Constant> constants;
  std::vector<Predicate> predicates;
  std::vector<DebugEntry> debug;
};

struct Module {
  std::string id;              // quest id
  std::string sourcePath;
  std::string sourceHash;
  std::vector<Chunk> chunks;
};

// Binary container (.mfb): magic "MFBM", version, chunks, SHA-256 trailer.
std::string serializeModule(const Module& module, DiagnosticSink& sink);

// Loads and structurally validates a module. A corrupted/tampered package is
// rejected before a single bytecode word is executed.
bool deserializeModule(const std::string& bytes, Module& out, DiagnosticSink& sink);

// Structural verification independent of serialization: opcodes, register
// ranges, constant kinds, jump targets, predicate indices.
bool verifyModule(const Module& module, DiagnosticSink& sink);

const char* moduleFormatVersion();

}  // namespace mfc
