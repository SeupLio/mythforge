#pragma once

#include <memory>
#include <string>
#include <vector>

#include "mfc/common.h"
#include "mfc/lexer.h"

namespace mfc {

enum class ExprKind { IntLit, FloatLit, StringLit, BoolLit, Ident, Unary, Binary, Call };

struct Expr;
using ExprPtr = std::unique_ptr<Expr>;

struct Expr {
  ExprKind kind = ExprKind::IntLit;
  Loc loc;
  int64_t intValue = 0;
  double floatValue = 0.0;
  bool boolValue = false;
  std::string text;  // identifier / string literal / callee name
  std::string op;    // unary or binary operator token text
  ExprPtr lhs;
  ExprPtr rhs;
  std::vector<ExprPtr> args;
};

enum class StmtKind { Block, Let, Assign, If, While, WaitEvent, WaitTime, Emit, Call };

struct Stmt;
using StmtPtr = std::unique_ptr<Stmt>;

struct Stmt {
  StmtKind kind = StmtKind::Block;
  Loc loc;
  std::string name;                 // let / assign target
  bool hasDeclaredType = false;
  Type declaredType = Type::Error;
  ExprPtr expr;                     // initializer / condition / wait time
  std::vector<StmtPtr> body;        // block, if-then, while body
  std::vector<StmtPtr> elseBody;    // if-else
  std::string eventName;            // wait event
  ExprPtr filter;                   // wait event filter
  std::string callee;               // emit / call
  std::vector<ExprPtr> args;        // emit / call arguments
  std::string nodeId;               // graph provenance for diagnostics
};

struct Handler {
  bool isStart = false;
  std::string eventName;
  ExprPtr filter;
  StmtPtr body;
  Loc loc;
};

struct QuestDecl {
  std::string name;
  Loc loc;
  std::vector<Handler> handlers;
};

struct Program {
  std::vector<QuestDecl> quests;
};

// Prints the AST back as MythScript. Used by `mf dsl` to prove that graph
// lowering and hand-written scripts share exactly one pipeline.
std::string printProgram(const Program& program);
std::string printStatement(const Stmt& stmt, int indent);

}  // namespace mfc
