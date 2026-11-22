#include "mfc/predicate.h"

namespace mfc {

namespace {

const char* predOpName(PredOp op) {
  switch (op) {
    case PredOp::PushInt: return "int";
    case PredOp::PushFloat: return "float";
    case PredOp::PushString: return "string";
    case PredOp::PushBool: return "bool";
    case PredOp::Field: return "field";
    case PredOp::Eq: return "==";
    case PredOp::Ne: return "!=";
    case PredOp::Lt: return "<";
    case PredOp::Le: return "<=";
    case PredOp::Gt: return ">";
    case PredOp::Ge: return ">=";
    case PredOp::And: return "&&";
    case PredOp::Or: return "||";
    case PredOp::Not: return "!";
  }
  return "?";
}

bool emit(const Expr& expr, Predicate& out, DiagnosticSink& sink, const std::string& file) {
  switch (expr.kind) {
    case ExprKind::IntLit: {
      PredInst inst;
      inst.op = PredOp::PushInt;
      inst.intValue = expr.intValue;
      out.code.push_back(std::move(inst));
      return true;
    }
    case ExprKind::FloatLit: {
      PredInst inst;
      inst.op = PredOp::PushFloat;
      inst.floatValue = expr.floatValue;
      out.code.push_back(std::move(inst));
      return true;
    }
    case ExprKind::StringLit: {
      PredInst inst;
      inst.op = PredOp::PushString;
      inst.text = expr.text;
      out.code.push_back(std::move(inst));
      return true;
    }
    case ExprKind::BoolLit: {
      PredInst inst;
      inst.op = PredOp::PushBool;
      inst.intValue = expr.boolValue ? 1 : 0;
      out.code.push_back(std::move(inst));
      return true;
    }
    case ExprKind::Ident: {
      PredInst inst;
      inst.op = PredOp::Field;
      inst.text = expr.text;
      out.code.push_back(std::move(inst));
      return true;
    }
    case ExprKind::Unary: {
      if (expr.op != "!") {
        sink.error("MFC2200", expr.loc, "only '!' is supported in event filters", {}, {}, file);
        return false;
      }
      if (!emit(*expr.lhs, out, sink, file)) return false;
      PredInst inst;
      inst.op = PredOp::Not;
      out.code.push_back(std::move(inst));
      return true;
    }
    case ExprKind::Binary: {
      if (!emit(*expr.lhs, out, sink, file)) return false;
      if (!emit(*expr.rhs, out, sink, file)) return false;
      PredInst inst;
      if (expr.op == "&&") inst.op = PredOp::And;
      else if (expr.op == "||") inst.op = PredOp::Or;
      else if (expr.op == "==") inst.op = PredOp::Eq;
      else if (expr.op == "!=") inst.op = PredOp::Ne;
      else if (expr.op == "<") inst.op = PredOp::Lt;
      else if (expr.op == "<=") inst.op = PredOp::Le;
      else if (expr.op == ">") inst.op = PredOp::Gt;
      else if (expr.op == ">=") inst.op = PredOp::Ge;
      else {
        sink.error("MFC2201", expr.loc,
                   "operator '" + expr.op + "' is not supported in event filters", {}, {}, file);
        return false;
      }
      out.code.push_back(std::move(inst));
      return true;
    }
    case ExprKind::Call:
      sink.error("MFC2202", expr.loc, "function calls are not supported in event filters", {},
                 "compute the value before the wait and compare against a constant", file);
      return false;
  }
  return false;
}

}  // namespace

std::string Predicate::describe() const {
  std::string out;
  for (const auto& inst : code) {
    if (!out.empty()) out += " ";
    out += predOpName(inst.op);
    if (inst.op == PredOp::PushInt) out += ":" + std::to_string(inst.intValue);
    if (inst.op == PredOp::PushFloat) out += ":" + std::to_string(inst.floatValue);
    if (inst.op == PredOp::PushString) out += ":\"" + inst.text + "\"";
    if (inst.op == PredOp::Field) out += ":" + inst.text;
  }
  return out;
}

bool compilePredicate(const Expr& expr, Predicate& out, DiagnosticSink& sink,
                      const std::string& file) {
  out.code.clear();
  return emit(expr, out, sink, file);
}

}  // namespace mfc
