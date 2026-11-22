#pragma once

#include <string>
#include <vector>

#include "mfc/ast.h"
#include "mfc/common.h"

namespace mfc {

class Parser {
 public:
  Parser(std::vector<Token> tokens, std::string file);

  Program parse(DiagnosticSink& sink);

 private:
  const Token& peek(size_t ahead = 0) const;
  const Token& advance();
  bool check(TokenKind kind) const;
  bool match(TokenKind kind);
  bool expect(TokenKind kind, const char* what, DiagnosticSink& sink);

  bool parseQuest(QuestDecl& out, DiagnosticSink& sink);
  bool parseHandler(Handler& out, DiagnosticSink& sink);
  StmtPtr parseBlock(DiagnosticSink& sink);
  StmtPtr parseStatement(DiagnosticSink& sink);

  ExprPtr parseExpression(int minPrecedence, DiagnosticSink& sink);
  ExprPtr parseUnary(DiagnosticSink& sink);
  ExprPtr parsePrimary(DiagnosticSink& sink);
  std::vector<ExprPtr> parseArguments(DiagnosticSink& sink);
  Type parseTypeName();

  void synchronize();

  std::vector<Token> tokens_;
  std::string file_;
  size_t pos_ = 0;
};

// A tiny hand-written parser is enough for MythScript, but keeping the entry
// point symmetric helps tests and the `mf dsl` command.
Program parseScript(const std::string& source, const std::string& file, DiagnosticSink& sink);

}  // namespace mfc
