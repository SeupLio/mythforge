#pragma once

#include <cstdint>

namespace mfc::bc {

// Fixed-width 32-bit register bytecode. Layout:
//   bits  0..7   opcode
//   bits  8..15  operand A (usually destination register)
//   bits 16..23  operand B (source / constant index / low byte of jump offset)
//   bits 24..31  operand C (source / argument count / high byte of jump offset)
// Register and constant indices are 8-bit on purpose: frames are capped at 255
// slots and chunks at 256 constants, which keeps every instruction one word and
// makes the dispatcher cache friendly. Larger units are split into chunks
// (documented limitation, see ADR-0001).
enum Op : uint8_t {
  NOP = 0,
  LOADK_I = 1,
  LOADK_F = 2,
  LOADK_S = 3,
  LOADK_B = 4,
  MOVE = 5,
  ADD = 6,
  SUB = 7,
  MUL = 8,
  DIV = 9,
  MOD = 10,
  NEG = 11,
  NOT = 12,
  EQ = 13,
  NE = 14,
  LT = 15,
  LE = 16,
  GT = 17,
  GE = 18,
  CALL = 19,
  EMIT = 20,
  WAIT_TIME = 21,
  WAIT_EVENT = 22,
  JUMP = 23,
  JUMP_IF_FALSE = 24,
  HALT = 25,
  EVENT_FIELD = 26,
  OP_COUNT = 27,
};

inline uint32_t encodeABC(uint8_t op, uint8_t a, uint8_t b, uint8_t c) {
  return static_cast<uint32_t>(op) | (static_cast<uint32_t>(a) << 8) |
         (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(c) << 24);
}

inline uint32_t encodeJump(uint8_t op, uint8_t reg, int16_t offset) {
  const uint16_t bits = static_cast<uint16_t>(offset);
  return static_cast<uint32_t>(op) | (static_cast<uint32_t>(reg) << 8) |
         (static_cast<uint32_t>(bits & 0xFF) << 16) | (static_cast<uint32_t>(bits >> 8) << 24);
}

inline uint8_t opOf(uint32_t word) { return static_cast<uint8_t>(word & 0xFF); }
inline uint8_t aOf(uint32_t word) { return static_cast<uint8_t>((word >> 8) & 0xFF); }
inline uint8_t bOf(uint32_t word) { return static_cast<uint8_t>((word >> 16) & 0xFF); }
inline uint8_t cOf(uint32_t word) { return static_cast<uint8_t>((word >> 24) & 0xFF); }

inline int16_t offsetOf(uint32_t word) {
  const uint16_t bits = static_cast<uint16_t>(((word >> 16) & 0xFF) | (((word >> 24) & 0xFF) << 8));
  return static_cast<int16_t>(bits);
}

const char* opcodeName(uint8_t op);
bool isValidOpcode(uint8_t op);

}  // namespace mfc::bc
