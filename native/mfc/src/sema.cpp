#include "mfc/sema.h"

#include <algorithm>
#include <set>

#include "mfc/parser.h"

namespace mfc {

namespace {

bool isComparison(const std::string& op) {
  return op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=";
}

}  // namespace

void Sema::pushScope() { scopes_.emplace_back(); }

void Sema::popScope(DiagnosticSink& sink, const std::string& file) {
  for (const auto& kv : scopes_.back().vars) {
    if (!kv.second.used && !kv.second.readOnly) {
      sink.warning("MFC2100", kv.second.loc,
                   "variable '" + kv.first + "' is declared but never used", {}, {}, file);
    }
  }
  scopes_.pop_back();
}

VarSymbol* Sema::lookup(const std::string& name) {
  for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
    auto found = it->vars.find(name);
    if (found != it->vars.end()) return &found->second;
  }
  return nullptr;
}

const VarSymbol* Sema::lookup(const std::string& name) const {
  for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
    auto found = it->vars.find(name);
    if (found != it->vars.end()) return &found->second;
  }
  return nullptr;
}

std::string Sema::suggestVariable(const std::string& name) const {
  std::string best;
  int bestDistance = 3;
  for (const auto& scope : scopes_) {
    for (const auto& kv : scope.vars) {
      const int d = editDistance(name, kv.first);
      if (d < bestDistance) {
        bestDistance = d;
        best = kv.first;
      }
    }
  }
  return best;
}

std::string Sema::suggestEvent(const std::string& name) const {
  std::string best;
  int bestDistance = 3;
  for (const auto& schema : builtinEvents()) {
    const int d = editDistance(name, schema.name);
    if (d < bestDistance) {
      bestDistance = d;
      best = schema.name;
    }
  }
  return best;
}

void Sema::declareEventFields(const EventSchema& schema) {
  for (const auto& field : schema.fields) {
    VarSymbol symbol;
    symbol.type = field.type;
    symbol.readOnly = true;
    symbol.used = true;  // implicit fields should never trigger unused warnings
    scopes_.back().vars[field.name] = symbol;
  }
}

bool Sema::analyze(const Program& program, DiagnosticSink& sink, const std::string& file) {
  currentFile_ = file;
  for (const auto& quest : program.quests) {
    bool hasStart = false;
    std::set<std::string> seenEvents;
    for (const auto& handler : quest.handlers) {
      if (handler.isStart) {
        if (hasStart) {
          sink.error("MFC2001", handler.loc, "quest '" + quest.name + "' has more than one 'on start' handler",
                     {}, {}, file);
        }
        hasStart = true;
      } else {
        if (!seenEvents.insert(handler.eventName).second) {
          sink.error("MFC2002", handler.loc,
                     "duplicate 'on event(" + handler.eventName + ")' handler in quest '" + quest.name + "'",
                     {}, {}, file);
        }
        if (findEventSchema(handler.eventName) == nullptr) {
          const std::string suggestion = suggestEvent(handler.eventName);
          sink.warning("MFC2101", handler.loc, "unknown event '" + handler.eventName + "'",
                       {}, suggestion.empty() ? "" : "did you mean '" + suggestion + "'?", file);
        }
      }
      checkHandler(quest, handler, sink, file);
    }
    if (!hasStart) {
      sink.error("MFC2003", quest.loc, "quest '" + quest.name + "' is missing an 'on start' handler",
                 {}, {}, file);
    }
  }
  return !sink.hasErrors();
}

void Sema::checkHandler(const QuestDecl& quest, const Handler& handler, DiagnosticSink& sink,
                        const std::string& file) {
  (void)quest;
  pushScope();
  const EventSchema* schema = nullptr;
  if (!handler.isStart) {
    schema = findEventSchema(handler.eventName);
    if (schema) declareEventFields(*schema);
  }
  if (handler.filter) {
    const Type filterType = checkExpr(*handler.filter, sink, file, schema, true);
    if (filterType != Type::Bool && filterType != Type::Error) {
      sink.error("MFC2004", handler.filter->loc,
                 "event filter must be a boolean expression, got '" +
                     std::string(typeName(filterType)) + "'",
                 {}, {}, file);
    }
  }
  checkStatement(*handler.body, sink, file);
  popScope(sink, file);
}

void Sema::checkStatement(const Stmt& stmt, DiagnosticSink& sink, const std::string& file) {
  switch (stmt.kind) {
    case StmtKind::Block: {
      pushScope();
      for (const auto& child : stmt.body) checkStatement(*child, sink, file);
      popScope(sink, file);
      break;
    }
    case StmtKind::Let: {
      if (lookup(stmt.name) != nullptr) {
        sink.error("MFC2005", stmt.loc, "variable '" + stmt.name + "' is already declared in this scope",
                   stmt.nodeId, "rename the variable or assign to the existing one", file);
      }
      Type declared = stmt.hasDeclaredType ? stmt.declaredType : Type::Error;
      const Type valueType = checkExpr(*stmt.expr, sink, file, nullptr, false);
      if (stmt.hasDeclaredType && !isAssignable(valueType, declared)) {
        sink.error("MFC2006", stmt.loc,
                   std::string("cannot initialize '") + stmt.name + "' of type " + typeName(declared) +
                       " with " + typeName(valueType),
                   stmt.nodeId, {}, file);
      }
      VarSymbol symbol;
      symbol.type = stmt.hasDeclaredType ? declared : valueType;
      symbol.loc = stmt.loc;
      scopes_.back().vars[stmt.name] = symbol;
      break;
    }
    case StmtKind::Assign: {
      VarSymbol* symbol = lookup(stmt.name);
      if (!symbol) {
        const std::string suggestion = suggestVariable(stmt.name);
        sink.error("MFC2007", stmt.loc, "assignment to undeclared variable '" + stmt.name + "'",
                   stmt.nodeId, suggestion.empty() ? "declare it with 'let'" : "did you mean '" + suggestion + "'?",
                   file);
        checkExpr(*stmt.expr, sink, file, nullptr, false);
        break;
      }
      if (symbol->readOnly) {
        sink.error("MFC2008", stmt.loc, "cannot assign to read-only event field '" + stmt.name + "'",
                   stmt.nodeId, {}, file);
      }
      const Type valueType = checkExpr(*stmt.expr, sink, file, nullptr, false);
      if (!isAssignable(valueType, symbol->type)) {
        sink.error("MFC2009", stmt.loc,
                   std::string("cannot assign ") + typeName(valueType) + " to '" + stmt.name + "' of type " +
                       typeName(symbol->type),
                   stmt.nodeId, {}, file);
      }
      break;
    }
    case StmtKind::If: {
      const Type cond = checkExpr(*stmt.expr, sink, file, nullptr, false);
      if (cond != Type::Bool && cond != Type::Error) {
        sink.error("MFC2010", stmt.expr->loc,
                   "if condition must be bool, got '" + std::string(typeName(cond)) + "'", stmt.nodeId,
                   {}, file);
      }
      for (const auto& child : stmt.body) checkStatement(*child, sink, file);
      for (const auto& child : stmt.elseBody) checkStatement(*child, sink, file);
      break;
    }
    case StmtKind::While: {
      const Type cond = checkExpr(*stmt.expr, sink, file, nullptr, false);
      if (cond != Type::Bool && cond != Type::Error) {
        sink.error("MFC2011", stmt.expr->loc,
                   "while condition must be bool, got '" + std::string(typeName(cond)) + "'", stmt.nodeId,
                   {}, file);
      }
      for (const auto& child : stmt.body) checkStatement(*child, sink, file);
      break;
    }
    case StmtKind::WaitEvent: {
      const EventSchema* schema = findEventSchema(stmt.eventName);
      if (!schema) {
        const std::string suggestion = suggestEvent(stmt.eventName);
        sink.warning("MFC2102", stmt.loc, "waiting for unknown event '" + stmt.eventName + "'",
                     stmt.nodeId, suggestion.empty() ? "user-defined events are allowed; "
                                                       "make sure some system emits it"
                                                     : "did you mean '" + suggestion + "'?",
                     file);
      }
      if (stmt.filter) {
        const Type filterType = checkExpr(*stmt.filter, sink, file, schema, true);
        if (filterType != Type::Bool && filterType != Type::Error) {
          sink.error("MFC2012", stmt.filter->loc,
                     "event filter must be boolean, got '" + std::string(typeName(filterType)) + "'",
                     stmt.nodeId, {}, file);
        }
      }
      break;
    }
    case StmtKind::WaitTime: {
      const Type valueType = checkExpr(*stmt.expr, sink, file, nullptr, false);
      if (!isNumeric(valueType) && valueType != Type::Error) {
        sink.error("MFC2013", stmt.expr->loc,
                   "wait time must be a number, got '" + std::string(typeName(valueType)) + "'",
                   stmt.nodeId, {}, file);
      }
      break;
    }
    case StmtKind::Emit:
    case StmtKind::Call: {
      for (const auto& arg : stmt.args) checkExpr(*arg, sink, file, nullptr, false);
      break;
    }
  }
}

Type Sema::checkExpr(const Expr& expr, DiagnosticSink& sink, const std::string& file,
                     const EventSchema* filterEvent, bool inFilter) {
  switch (expr.kind) {
    case ExprKind::IntLit: return Type::Int;
    case ExprKind::FloatLit: return Type::Float;
    case ExprKind::StringLit: return Type::String;
    case ExprKind::BoolLit: return Type::Bool;
    case ExprKind::Ident: {
      if (inFilter) {
        if (filterEvent) {
          const EventField* field = filterEvent->findField(expr.text);
          if (field) return field->type;
        }
        std::string suggestion;
        int best = 3;
        if (filterEvent) {
          for (const auto& field : filterEvent->fields) {
            const int d = editDistance(expr.text, field.name);
            if (d < best) {
              best = d;
              suggestion = field.name;
            }
          }
        }
        sink.error("MFC2014", expr.loc,
                   "event filter can only reference fields of '" +
                       std::string(filterEvent ? filterEvent->name : "<unknown>") + "', found '" +
                       expr.text + "'",
                   {}, suggestion.empty() ? "filters cannot read quest variables" : "did you mean '" + suggestion + "'?",
                   file);
        return Type::Error;
      }
      const VarSymbol* symbol = lookup(expr.text);
      if (!symbol) {
        const std::string suggestion = suggestVariable(expr.text);
        sink.error("MFC2015", expr.loc, "undefined variable '" + expr.text + "'", {},
                   suggestion.empty() ? "declare it with 'let'" : "did you mean '" + suggestion + "'?",
                   file);
        return Type::Error;
      }
      const_cast<VarSymbol*>(symbol)->used = true;
      return symbol->type;
    }
    case ExprKind::Unary: {
      const Type inner = checkExpr(*expr.lhs, sink, file, filterEvent, inFilter);
      if (expr.op == "-") {
        if (!isNumeric(inner) && inner != Type::Error) {
          sink.error("MFC2016", expr.loc, "unary '-' requires a number, got '" +
                                             std::string(typeName(inner)) + "'", {}, {}, file);
          return Type::Error;
        }
        return inner;
      }
      if (inner != Type::Bool && inner != Type::Error) {
        sink.error("MFC2017", expr.loc, "'!' requires bool, got '" + std::string(typeName(inner)) + "'",
                   {}, {}, file);
        return Type::Error;
      }
      return Type::Bool;
    }
    case ExprKind::Binary: {
      const Type lhs = checkExpr(*expr.lhs, sink, file, filterEvent, inFilter);
      const Type rhs = checkExpr(*expr.rhs, sink, file, filterEvent, inFilter);
      if (expr.op == "&&" || expr.op == "||") {
        if ((lhs != Type::Bool && lhs != Type::Error) || (rhs != Type::Bool && rhs != Type::Error)) {
          sink.error("MFC2018", expr.loc,
                     "'" + expr.op + "' requires bool operands, got '" + typeName(lhs) + "' and '" +
                         typeName(rhs) + "'",
                     {}, {}, file);
          return Type::Error;
        }
        return Type::Bool;
      }
      if (isComparison(expr.op)) {
        const bool numericCompare = isNumeric(lhs) && isNumeric(rhs);
        const bool sameType = lhs == rhs;
        if (!numericCompare && !sameType && lhs != Type::Error && rhs != Type::Error) {
          sink.error("MFC2019", expr.loc,
                     "cannot compare '" + std::string(typeName(lhs)) + "' with '" +
                         std::string(typeName(rhs)) + "'",
                     {}, {}, file);
        }
        if ((expr.op == "<" || expr.op == "<=" || expr.op == ">" || expr.op == ">=") &&
            !numericCompare && lhs != Type::Error && rhs != Type::Error) {
          sink.error("MFC2020", expr.loc,
                     "'" + expr.op + "' only supports numeric operands", {}, {}, file);
        }
        return Type::Bool;
      }
      if (!isNumeric(lhs) || !isNumeric(rhs)) {
        if (lhs != Type::Error && rhs != Type::Error) {
          sink.error("MFC2021", expr.loc,
                     "operator '" + expr.op + "' requires numeric operands, got '" +
                         std::string(typeName(lhs)) + "' and '" + std::string(typeName(rhs)) + "'",
                     {}, {}, file);
        }
        return Type::Error;
      }
      if (expr.op == "%" && (lhs != Type::Int || rhs != Type::Int)) {
        sink.error("MFC2022", expr.loc, "'%' requires integer operands", {}, {}, file);
        return Type::Error;
      }
      return (lhs == Type::Float || rhs == Type::Float) ? Type::Float : Type::Int;
    }
    case ExprKind::Call: {
      for (const auto& arg : expr.args) checkExpr(*arg, sink, file, filterEvent, inFilter);
      // Host function signatures are registered at runtime; the compiler treats
      // the result as an opaque value and lets the host decide.
      return Type::Error;
    }
  }
  return Type::Error;
}

}  // namespace mfc
