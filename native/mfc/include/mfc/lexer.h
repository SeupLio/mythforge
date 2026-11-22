#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "mfc/common.h"

namespace mfc {

enum class TokenKind {
  EndOfFile,
  Identifier,
  IntLiteral,
  FloatLiteral,
  StringLiteral,
  // keywords
  KwQuest,
  KwOn,
  KwStart,
  KwEvent,
  KwLet,
  KwIf,
  KwElse,
  KwWhile,
  KwWait,
  KwTime,
  KwEmit,
  KwCall,
  KwTrue,
  KwFalse,
  // punctuation
  LParen,
  RParen,
  LBrace,
  RBrace,
  Comma,
  Semicolon,
  Colon,
  Assign,
  Plus,
  Minus,
  Star,
  Slash,
  Percent,
  EqEq,
  BangEq,
  Less,
  LessEq,
  Greater,
  GreaterEq,
  AmpAmp,
  PipePipe,
  Bang,
  Dot,
};

struct Token {
  TokenKind kind = TokenKind::EndOfFile;
  std::string text;  // identifier name / string contents
  int64_t intValue = 0;
  double floatValue = 0.0;
  Loc loc;
};

const char* tokenKindName(TokenKind kind);

// Hand-written DFA lexer. Accepts UTF-8 identifiers (bytes >= 0x80 continue an
// identifier) so Chinese quest/node names work without extra registration.
class Lexer {
 public:
  Lexer(std::string source, std::string file);

  std::vector<Token> tokenize(DiagnosticSink& sink);

 private:
  char peek(size_t ahead = 0) const;
  void advance();
  bool atEnd() const { return pos_ >= source_.size(); }

  std::string source_;
  std::string file_;
  size_t pos_ = 0;
  int line_ = 1;
  int col_ = 1;
};

}  // namespace mfc
