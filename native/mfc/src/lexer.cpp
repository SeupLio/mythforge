#include "mfc/lexer.h"

#include <cctype>
#include <cstdlib>
#include <unordered_map>

namespace mfc {

namespace {

bool isIdentStart(unsigned char c) {
  return std::isalpha(c) != 0 || c == '_' || c >= 0x80;
}

bool isIdentContinue(unsigned char c) {
  return std::isalnum(c) != 0 || c == '_' || c >= 0x80;
}

const std::unordered_map<std::string, TokenKind>& keywords() {
  static const std::unordered_map<std::string, TokenKind> kKeywords = {
      {"quest", TokenKind::KwQuest}, {"on", TokenKind::KwOn},
      {"start", TokenKind::KwStart}, {"event", TokenKind::KwEvent},
      {"let", TokenKind::KwLet},     {"if", TokenKind::KwIf},
      {"else", TokenKind::KwElse},   {"while", TokenKind::KwWhile},
      {"wait", TokenKind::KwWait},   {"time", TokenKind::KwTime},
      {"emit", TokenKind::KwEmit},   {"call", TokenKind::KwCall},
      {"true", TokenKind::KwTrue},   {"false", TokenKind::KwFalse},
  };
  return kKeywords;
}

}  // namespace

const char* tokenKindName(TokenKind kind) {
  switch (kind) {
    case TokenKind::EndOfFile: return "end of file";
    case TokenKind::Identifier: return "identifier";
    case TokenKind::IntLiteral: return "integer literal";
    case TokenKind::FloatLiteral: return "float literal";
    case TokenKind::StringLiteral: return "string literal";
    case TokenKind::KwQuest: return "'quest'";
    case TokenKind::KwOn: return "'on'";
    case TokenKind::KwStart: return "'start'";
    case TokenKind::KwEvent: return "'event'";
    case TokenKind::KwLet: return "'let'";
    case TokenKind::KwIf: return "'if'";
    case TokenKind::KwElse: return "'else'";
    case TokenKind::KwWhile: return "'while'";
    case TokenKind::KwWait: return "'wait'";
    case TokenKind::KwTime: return "'time'";
    case TokenKind::KwEmit: return "'emit'";
    case TokenKind::KwCall: return "'call'";
    case TokenKind::KwTrue: return "'true'";
    case TokenKind::KwFalse: return "'false'";
    case TokenKind::LParen: return "'('";
    case TokenKind::RParen: return "')'";
    case TokenKind::LBrace: return "'{'";
    case TokenKind::RBrace: return "'}'";
    case TokenKind::Comma: return "','";
    case TokenKind::Semicolon: return "';'";
    case TokenKind::Colon: return "':'";
    case TokenKind::Assign: return "'='";
    case TokenKind::Plus: return "'+'";
    case TokenKind::Minus: return "'-'";
    case TokenKind::Star: return "'*'";
    case TokenKind::Slash: return "'/'";
    case TokenKind::Percent: return "'%'";
    case TokenKind::EqEq: return "'=='";
    case TokenKind::BangEq: return "'!='";
    case TokenKind::Less: return "'<'";
    case TokenKind::LessEq: return "'<='";
    case TokenKind::Greater: return "'>'";
    case TokenKind::GreaterEq: return "'>='";
    case TokenKind::AmpAmp: return "'&&'";
    case TokenKind::PipePipe: return "'||'";
    case TokenKind::Bang: return "'!'";
    case TokenKind::Dot: return "'.'";
  }
  return "<token>";
}

Lexer::Lexer(std::string source, std::string file)
    : source_(std::move(source)), file_(std::move(file)) {}

char Lexer::peek(size_t ahead) const {
  const size_t index = pos_ + ahead;
  return index < source_.size() ? source_[index] : '\0';
}

void Lexer::advance() {
  if (pos_ >= source_.size()) return;
  if (source_[pos_] == '\n') {
    ++line_;
    col_ = 1;
  } else {
    ++col_;
  }
  ++pos_;
}

std::vector<Token> Lexer::tokenize(DiagnosticSink& sink) {
  std::vector<Token> tokens;
  bool broken = false;
  while (!atEnd()) {
    const char c = peek();
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
      advance();
      continue;
    }
    if (c == '/' && peek(1) == '/') {
      while (!atEnd() && peek() != '\n') advance();
      continue;
    }
    if (c == '/' && peek(1) == '*') {
      const Loc start{line_, col_};
      advance();
      advance();
      bool closed = false;
      while (!atEnd()) {
        if (peek() == '*' && peek(1) == '/') {
          advance();
          advance();
          closed = true;
          break;
        }
        advance();
      }
      if (!closed) {
        sink.error("MFC0001", start, "unterminated block comment", {}, {}, file_);
        broken = true;
        break;
      }
      continue;
    }

    Token token;
    token.loc = Loc{line_, col_};

    if (isIdentStart(static_cast<unsigned char>(c))) {
      const size_t start = pos_;
      while (!atEnd() && isIdentContinue(static_cast<unsigned char>(peek()))) advance();
      token.text = source_.substr(start, pos_ - start);
      auto it = keywords().find(token.text);
      token.kind = it == keywords().end() ? TokenKind::Identifier : it->second;
      tokens.push_back(std::move(token));
      continue;
    }

    if (std::isdigit(static_cast<unsigned char>(c)) != 0) {
      const size_t start = pos_;
      bool isFloat = false;
      while (!atEnd() && std::isdigit(static_cast<unsigned char>(peek())) != 0) advance();
      if (peek() == '.' && std::isdigit(static_cast<unsigned char>(peek(1))) != 0) {
        isFloat = true;
        advance();
        while (!atEnd() && std::isdigit(static_cast<unsigned char>(peek())) != 0) advance();
      }
      if (peek() == 'e' || peek() == 'E') {
        isFloat = true;
        advance();
        if (peek() == '+' || peek() == '-') advance();
        while (!atEnd() && std::isdigit(static_cast<unsigned char>(peek())) != 0) advance();
      }
      const std::string text = source_.substr(start, pos_ - start);
      if (isFloat) {
        token.kind = TokenKind::FloatLiteral;
        token.floatValue = std::strtod(text.c_str(), nullptr);
      } else {
        token.kind = TokenKind::IntLiteral;
        token.intValue = std::strtoll(text.c_str(), nullptr, 10);
      }
      token.text = text;
      tokens.push_back(std::move(token));
      continue;
    }

    if (c == '"') {
      advance();
      std::string value;
      bool closed = false;
      while (!atEnd()) {
        const char ch = peek();
        if (ch == '"') {
          advance();
          closed = true;
          break;
        }
        if (ch == '\n') break;
        if (ch == '\\') {
          advance();
          if (atEnd()) break;
          const char esc = peek();
          switch (esc) {
            case 'n': value += '\n'; break;
            case 't': value += '\t'; break;
            case 'r': value += '\r'; break;
            case '"': value += '"'; break;
            case '\\': value += '\\'; break;
            default:
              sink.error("MFC0002", token.loc,
                         std::string("unknown escape sequence '\\") + esc + "'", {}, {}, file_);
              value += esc;
              break;
          }
          advance();
          continue;
        }
        value += ch;
        advance();
      }
      if (!closed) {
        sink.error("MFC0003", token.loc, "unterminated string literal", {}, {}, file_);
        broken = true;
        break;
      }
      token.kind = TokenKind::StringLiteral;
      token.text = std::move(value);
      tokens.push_back(std::move(token));
      continue;
    }

    auto single = [&](TokenKind kind) {
      token.kind = kind;
      advance();
      tokens.push_back(std::move(token));
    };
    auto doubleChar = [&](char second, TokenKind both, TokenKind singleKind) {
      if (peek(1) == second) {
        token.kind = both;
        advance();
        advance();
      } else {
        token.kind = singleKind;
        advance();
      }
      tokens.push_back(std::move(token));
    };

    switch (c) {
      case '(': single(TokenKind::LParen); break;
      case ')': single(TokenKind::RParen); break;
      case '{': single(TokenKind::LBrace); break;
      case '}': single(TokenKind::RBrace); break;
      case ',': single(TokenKind::Comma); break;
      case ';': single(TokenKind::Semicolon); break;
      case ':': single(TokenKind::Colon); break;
      case '.': single(TokenKind::Dot); break;
      case '+': single(TokenKind::Plus); break;
      case '-': single(TokenKind::Minus); break;
      case '*': single(TokenKind::Star); break;
      case '/': single(TokenKind::Slash); break;
      case '%': single(TokenKind::Percent); break;
      case '=': doubleChar('=', TokenKind::EqEq, TokenKind::Assign); break;
      case '!': doubleChar('=', TokenKind::BangEq, TokenKind::Bang); break;
      case '<': doubleChar('=', TokenKind::LessEq, TokenKind::Less); break;
      case '>': doubleChar('=', TokenKind::GreaterEq, TokenKind::Greater); break;
      case '&':
        if (peek(1) == '&') {
          token.kind = TokenKind::AmpAmp;
          advance();
          advance();
          tokens.push_back(std::move(token));
        } else {
          sink.error("MFC0004", token.loc, "expected '&&'", {}, {}, file_);
          advance();
          broken = true;
        }
        break;
      case '|':
        if (peek(1) == '|') {
          token.kind = TokenKind::PipePipe;
          advance();
          advance();
          tokens.push_back(std::move(token));
        } else {
          sink.error("MFC0005", token.loc, "expected '||'", {}, {}, file_);
          advance();
          broken = true;
        }
        break;
      default:
        sink.error("MFC0006", token.loc,
                   std::string("unexpected character '") + c + "'", {}, {}, file_);
        advance();
        broken = true;
        break;
    }
    if (broken) break;
  }

  Token eof;
  eof.kind = TokenKind::EndOfFile;
  eof.loc = Loc{line_, col_};
  tokens.push_back(std::move(eof));
  return tokens;
}

}  // namespace mfc
