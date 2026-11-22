#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "mfc/ast.h"
#include "mfc/common.h"

namespace mfc {

// Event filters are compiled to a tiny postfix predicate program. They are
// evaluated by the VM when an event arrives, so `wait event(Kill, target ==
// "Wolf")` never wakes the coroutine for unrelated kills.
enum class PredOp : uint8_t {
  PushInt,
  PushFloat,
  PushString,
  PushBool,
  Field,
  Eq,
  Ne,
  Lt,
  Le,
  Gt,
  Ge,
  And,
  Or,
  Not,
};

struct PredInst {
  PredOp op = PredOp::PushInt;
  int64_t intValue = 0;
  double floatValue = 0.0;
  std::string text;  // string literal or event field name
};

struct Predicate {
  std::vector<PredInst> code;
  std::string describe() const;
};

bool compilePredicate(const Expr& expr, Predicate& out, DiagnosticSink& sink,
                      const std::string& file);

}  // namespace mfc
