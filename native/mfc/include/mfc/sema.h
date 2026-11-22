#pragma once

#include <map>
#include <string>
#include <vector>

#include "mfc/ast.h"
#include "mfc/common.h"
#include "mfc/events.h"

namespace mfc {

struct VarSymbol {
  Type type = Type::Error;
  bool readOnly = false;
  bool used = false;
  Loc loc;
};

// Semantic analysis: scoped symbol table, type checking, event field checking,
// unused-variable and unknown-event warnings. The same pass serves hand-written
// MythScript and graph-lowered ASTs.
class Sema {
 public:
  bool analyze(const Program& program, DiagnosticSink& sink, const std::string& file);

 private:
  struct Scope {
    std::map<std::string, VarSymbol> vars;
  };

  void pushScope();
  void popScope(DiagnosticSink& sink, const std::string& file);
  VarSymbol* lookup(const std::string& name);
  const VarSymbol* lookup(const std::string& name) const;

  void checkHandler(const QuestDecl& quest, const Handler& handler, DiagnosticSink& sink,
                    const std::string& file);
  void checkStatement(const Stmt& stmt, DiagnosticSink& sink, const std::string& file);
  Type checkExpr(const Expr& expr, DiagnosticSink& sink, const std::string& file,
                 const EventSchema* filterEvent, bool inFilter);

  void declareEventFields(const EventSchema& schema);
  std::string suggestVariable(const std::string& name) const;
  std::string suggestEvent(const std::string& name) const;

  std::vector<Scope> scopes_;
  std::string currentFile_;
};

}  // namespace mfc
