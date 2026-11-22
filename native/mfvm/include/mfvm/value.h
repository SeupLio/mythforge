#pragma once

#include <cstdint>

namespace mfvm {

// 16-byte tagged value. Strings are interned ids owned by the VM, so equality
// on strings is a single integer compare in the hot path.
enum class ValueTag : uint8_t { Nil = 0, Int = 1, Float = 2, Bool = 3, String = 4 };

struct Value {
  ValueTag tag = ValueTag::Nil;
  uint8_t padding[3] = {0, 0, 0};
  union {
    int64_t i;
    double f;
    int32_t b;
    uint32_t s;
  } as;

  Value() : tag(ValueTag::Nil), padding{0, 0, 0}, as{0} {}

  static Value nil() { return Value(); }
  static Value integer(int64_t v) {
    Value value;
    value.tag = ValueTag::Int;
    value.as.i = v;
    return value;
  }
  static Value number(double v) {
    Value value;
    value.tag = ValueTag::Float;
    value.as.f = v;
    return value;
  }
  static Value boolean(bool v) {
    Value value;
    value.tag = ValueTag::Bool;
    value.as.b = v ? 1 : 0;
    return value;
  }
  static Value string(uint32_t id) {
    Value value;
    value.tag = ValueTag::String;
    value.as.s = id;
    return value;
  }

  bool isNil() const { return tag == ValueTag::Nil; }
  bool isInt() const { return tag == ValueTag::Int; }
  bool isFloat() const { return tag == ValueTag::Float; }
  bool isNumber() const { return tag == ValueTag::Int || tag == ValueTag::Float; }
  bool isBool() const { return tag == ValueTag::Bool; }
  bool isString() const { return tag == ValueTag::String; }

  double asDouble() const { return tag == ValueTag::Int ? static_cast<double>(as.i) : as.f; }
};

}  // namespace mfvm
