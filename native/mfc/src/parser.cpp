#include "mfc/parser.h"

#include <cstdio>

#include "mfc/events.h"
#include "mfc/lexer.h"

namespace mfc {

namespace {

int binaryPrecedence(TokenKind kind) {
  switch (kind) {
    case TokenKind::PipePipe: return 1;
    case TokenKind::AmpAmp: return 2;
    case TokenKind::EqEq:
    case TokenKind::BangEq: return 3;
    case TokenKind::Less:
    case TokenKind::LessEq:
    case TokenKind::Greater:
    case TokenKind::GreaterEq: return 4;
    case TokenKind::Plus:
    case TokenKind::Minus: return 5;
    case TokenKind::Star:
    case TokenKind::Slash:
    case TokenKind::Percent: return 6;
    default: return -1;
  }
}

std::string operatorText(TokenKind kind) {
  switch (kind) {
    case TokenKind::PipePipe: return "||";
    case TokenKind::AmpAmp: return "&&";
    case TokenKind::EqEq: return "==";
    case TokenKind::BangEq: return "!=";
    case TokenKind::Less: return "<";
    case TokenKind::LessEq: return "<=";
    case TokenKind::Greater: return ">";
    case TokenKind::GreaterEq: return ">=";
    case TokenKind::Plus: return "+";
    case TokenKind::Minus: return "-";
    case TokenKind::Star: return "*";
    case TokenKind::Slash: return "/";
    case TokenKind::Percent: return "%";
    case TokenKind::Bang: return "!";
    default: return "";
  }
}

ExprPtr makeInt(const Token& t) {
  auto e = std::make_unique<Expr>();
  e->kind = ExprKind::IntLit;
  e->loc = t.loc;
  e->intValue = t.intValue;
  return e;
}

}  // namespace

Parser::Parser(std::vector<Token> tokens, std::string file)
    : tokens_(std::move(tokens)), file_(std::move(file)) {}

const Token& Parser::peek(size_t ahead) const {
  const size_t index = pos_ + ahead;
  if (index >= tokens_.size()) return tokens_.back();
  return tokens_[index];
}

const Token& Parser::advance() {
  const Token& t = peek();
  if (pos_ + 1 < tokens_.size()) ++pos_;
  return t;
}

bool Parser::check(TokenKind kind) const { return peek().kind == kind; }

bool Parser::match(TokenKind kind) {
  if (!check(kind)) return false;
  advance();
  return true;
}

bool Parser::expect(TokenKind kind, const char* what, DiagnosticSink& sink) {
  if (check(kind)) {
    advance();
    return true;
  }
  sink.error("MFC1001", peek().loc,
             std::string("expected ") + what + " but found " + tokenKindName(peek().kind),
             {}, {}, file_);
  return false;
}

void Parser::synchronize() {
  while (!check(TokenKind::EndOfFile)) {
    if (check(TokenKind::Semicolon)) {
      advance();
      return;
    }
    if (check(TokenKind::RBrace) || check(TokenKind::KwOn) || check(TokenKind::KwQuest)) {
      return;
    }
    advance();
  }
}

Program Parser::parse(DiagnosticSink& sink) {
  Program program;
  while (!check(TokenKind::EndOfFile)) {
    if (check(TokenKind::KwQuest)) {
      QuestDecl quest;
      if (parseQuest(quest, sink)) {
        program.quests.push_back(std::move(quest));
      } else {
        synchronize();
      }
      continue;
    }
    sink.error("MFC1002", peek().loc,
               std::string("expected 'quest' but found ") + tokenKindName(peek().kind),
               {}, {}, file_);
    synchronize();
    if (check(TokenKind::EndOfFile)) break;
    advance();
  }
  return program;
}

bool Parser::parseQuest(QuestDecl& out, DiagnosticSink& sink) {
  out.loc = peek().loc;
  if (!expect(TokenKind::KwQuest, "'quest'", sink)) return false;
  if (!check(TokenKind::Identifier)) {
    sink.error("MFC1003", peek().loc, "expected quest name", {}, {}, file_);
    return false;
  }
  out.name = advance().text;
  if (!expect(TokenKind::LBrace, "'{'", sink)) return false;
  while (!check(TokenKind::RBrace) && !check(TokenKind::EndOfFile)) {
    if (check(TokenKind::KwOn)) {
      Handler handler;
      const size_t before = pos_;
      if (parseHandler(handler, sink)) {
        out.handlers.push_back(std::move(handler));
      } else if (pos_ == before) {
        advance();  // guarantee forward progress
        synchronize();
      } else {
        synchronize();
      }
      continue;
    }
    sink.error("MFC1004", peek().loc,
               std::string("expected 'on' but found ") + tokenKindName(peek().kind),
               {}, {}, file_);
    advance();
    synchronize();
  }
  if (!expect(TokenKind::RBrace, "'}'", sink)) return false;
  return true;
}

bool Parser::parseHandler(Handler& out, DiagnosticSink& sink) {
  out.loc = peek().loc;
  if (!expect(TokenKind::KwOn, "'on'", sink)) return false;
  if (match(TokenKind::KwStart)) {
    out.isStart = true;
    out.body = parseBlock(sink);
    return out.body != nullptr;
  }
  if (check(TokenKind::KwEvent)) {
    advance();
    if (!expect(TokenKind::LParen, "'('", sink)) return false;
    if (!check(TokenKind::Identifier)) {
      sink.error("MFC1005", peek().loc, "expected event name", {}, {}, file_);
      return false;
    }
    out.eventName = advance().text;
    if (match(TokenKind::Comma)) {
      out.filter = parseExpression(1, sink);
      if (!out.filter) return false;
    }
    if (!expect(TokenKind::RParen, "')'", sink)) return false;
    out.body = parseBlock(sink);
    return out.body != nullptr;
  }
  sink.error("MFC1006", peek().loc, "expected 'start' or 'event(...)'", {}, {}, file_);
  return false;
}

StmtPtr Parser::parseBlock(DiagnosticSink& sink) {
  auto block = std::make_unique<Stmt>();
  block->kind = StmtKind::Block;
  block->loc = peek().loc;
  if (!expect(TokenKind::LBrace, "'{'", sink)) return nullptr;
  while (!check(TokenKind::RBrace) && !check(TokenKind::EndOfFile)) {
    StmtPtr stmt = parseStatement(sink);
    if (!stmt) {
      synchronize();
      continue;
    }
    block->body.push_back(std::move(stmt));
  }
  if (!expect(TokenKind::RBrace, "'}'", sink)) return nullptr;
  return block;
}

StmtPtr Parser::parseStatement(DiagnosticSink& sink) {
  const Loc loc = peek().loc;
  if (check(TokenKind::LBrace)) {
    return parseBlock(sink);
  }
  if (check(TokenKind::KwLet)) {
    advance();
    if (!check(TokenKind::Identifier)) {
      sink.error("MFC1007", peek().loc, "expected variable name after 'let'", {}, {}, file_);
      return nullptr;
    }
    auto stmt = std::make_unique<Stmt>();
    stmt->kind = StmtKind::Let;
    stmt->loc = loc;
    stmt->name = advance().text;
    if (match(TokenKind::Colon)) {
      stmt->hasDeclaredType = true;
      stmt->declaredType = parseTypeName();
      if (stmt->declaredType == Type::Error) {
        sink.error("MFC1008", peek().loc, "expected a type name (int/float/bool/string)", {}, {},
                   file_);
        return nullptr;
      }
    }
    if (!expect(TokenKind::Assign, "'='", sink)) return nullptr;
    stmt->expr = parseExpression(1, sink);
    if (!stmt->expr) return nullptr;
    if (!expect(TokenKind::Semicolon, "';'", sink)) return nullptr;
    return stmt;
  }
  if (check(TokenKind::KwIf)) {
    advance();
    auto stmt = std::make_unique<Stmt>();
    stmt->kind = StmtKind::If;
    stmt->loc = loc;
    if (!expect(TokenKind::LParen, "'('", sink)) return nullptr;
    stmt->expr = parseExpression(1, sink);
    if (!stmt->expr) return nullptr;
    if (!expect(TokenKind::RParen, "')'", sink)) return nullptr;
    StmtPtr thenBlock = parseBlock(sink);
    if (!thenBlock) return nullptr;
    stmt->body.push_back(std::move(thenBlock));
    if (match(TokenKind::KwElse)) {
      if (check(TokenKind::KwIf)) {
        StmtPtr nested = parseStatement(sink);
        if (!nested) return nullptr;
        stmt->elseBody.push_back(std::move(nested));
      } else {
        StmtPtr elseBlock = parseBlock(sink);
        if (!elseBlock) return nullptr;
        stmt->elseBody.push_back(std::move(elseBlock));
      }
    }
    return stmt;
  }
  if (check(TokenKind::KwWhile)) {
    advance();
    auto stmt = std::make_unique<Stmt>();
    stmt->kind = StmtKind::While;
    stmt->loc = loc;
    if (!expect(TokenKind::LParen, "'('", sink)) return nullptr;
    stmt->expr = parseExpression(1, sink);
    if (!stmt->expr) return nullptr;
    if (!expect(TokenKind::RParen, "')'", sink)) return nullptr;
    StmtPtr body = parseBlock(sink);
    if (!body) return nullptr;
    stmt->body.push_back(std::move(body));
    return stmt;
  }
  if (check(TokenKind::KwWait)) {
    advance();
    auto stmt = std::make_unique<Stmt>();
    stmt->loc = loc;
    if (match(TokenKind::KwEvent)) {
      stmt->kind = StmtKind::WaitEvent;
      if (!expect(TokenKind::LParen, "'('", sink)) return nullptr;
      if (!check(TokenKind::Identifier)) {
        sink.error("MFC1009", peek().loc, "expected event name", {}, {}, file_);
        return nullptr;
      }
      stmt->eventName = advance().text;
      if (match(TokenKind::Comma)) {
        stmt->filter = parseExpression(1, sink);
        if (!stmt->filter) return nullptr;
      }
      if (!expect(TokenKind::RParen, "')'", sink)) return nullptr;
      if (!expect(TokenKind::Semicolon, "';'", sink)) return nullptr;
      return stmt;
    }
    if (match(TokenKind::KwTime)) {
      stmt->kind = StmtKind::WaitTime;
      if (!expect(TokenKind::LParen, "'('", sink)) return nullptr;
      stmt->expr = parseExpression(1, sink);
      if (!stmt->expr) return nullptr;
      if (!expect(TokenKind::RParen, "')'", sink)) return nullptr;
      if (!expect(TokenKind::Semicolon, "';'", sink)) return nullptr;
      return stmt;
    }
    sink.error("MFC1010", peek().loc, "expected 'event' or 'time' after 'wait'", {}, {}, file_);
    return nullptr;
  }
  if (check(TokenKind::KwEmit) || check(TokenKind::KwCall)) {
    const bool isEmit = check(TokenKind::KwEmit);
    advance();
    auto stmt = std::make_unique<Stmt>();
    stmt->kind = isEmit ? StmtKind::Emit : StmtKind::Call;
    stmt->loc = loc;
    if (!check(TokenKind::Identifier)) {
      sink.error("MFC1011", peek().loc,
                 isEmit ? "expected event name after 'emit'" : "expected function name after 'call'",
                 {}, {}, file_);
      return nullptr;
    }
    stmt->callee = advance().text;
    if (!expect(TokenKind::LParen, "'('", sink)) return nullptr;
    if (!check(TokenKind::RParen)) {
      stmt->args = parseArguments(sink);
    }
    if (!expect(TokenKind::RParen, "')'", sink)) return nullptr;
    if (!expect(TokenKind::Semicolon, "';'", sink)) return nullptr;
    return stmt;
  }
  if (check(TokenKind::Identifier)) {
    auto stmt = std::make_unique<Stmt>();
    stmt->kind = StmtKind::Assign;
    stmt->loc = loc;
    stmt->name = advance().text;
    if (!expect(TokenKind::Assign, "'='", sink)) return nullptr;
    stmt->expr = parseExpression(1, sink);
    if (!stmt->expr) return nullptr;
    if (!expect(TokenKind::Semicolon, "';'", sink)) return nullptr;
    return stmt;
  }
  sink.error("MFC1012", loc,
             std::string("expected a statement but found ") + tokenKindName(peek().kind), {}, {},
             file_);
  return nullptr;
}

std::vector<ExprPtr> Parser::parseArguments(DiagnosticSink& sink) {
  std::vector<ExprPtr> args;
  ExprPtr first = parseExpression(1, sink);
  if (!first) return args;
  args.push_back(std::move(first));
  while (match(TokenKind::Comma)) {
    ExprPtr arg = parseExpression(1, sink);
    if (!arg) break;
    args.push_back(std::move(arg));
  }
  return args;
}

Type Parser::parseTypeName() {
  if (!check(TokenKind::Identifier)) return Type::Error;
  const std::string name = peek().text;
  if (name == "int") {
    advance();
    return Type::Int;
  }
  if (name == "float") {
    advance();
    return Type::Float;
  }
  if (name == "bool") {
    advance();
    return Type::Bool;
  }
  if (name == "string") {
    advance();
    return Type::String;
  }
  return Type::Error;
}

ExprPtr Parser::parseExpression(int minPrecedence, DiagnosticSink& sink) {
  ExprPtr lhs = parseUnary(sink);
  if (!lhs) return nullptr;
  while (true) {
    const int precedence = binaryPrecedence(peek().kind);
    if (precedence < minPrecedence) break;
    const Token op = advance();
    ExprPtr rhs = parseExpression(precedence + 1, sink);
    if (!rhs) return nullptr;
    auto binary = std::make_unique<Expr>();
    binary->kind = ExprKind::Binary;
    binary->loc = op.loc;
    binary->op = operatorText(op.kind);
    binary->lhs = std::move(lhs);
    binary->rhs = std::move(rhs);
    lhs = std::move(binary);
  }
  return lhs;
}

ExprPtr Parser::parseUnary(DiagnosticSink& sink) {
  if (check(TokenKind::Minus) || check(TokenKind::Bang)) {
    const Token op = advance();
    ExprPtr operand = parseUnary(sink);
    if (!operand) return nullptr;
    auto expr = std::make_unique<Expr>();
    expr->kind = ExprKind::Unary;
    expr->loc = op.loc;
    expr->op = operatorText(op.kind);
    expr->lhs = std::move(operand);
    return expr;
  }
  return parsePrimary(sink);
}

ExprPtr Parser::parsePrimary(DiagnosticSink& sink) {
  if (check(TokenKind::IntLiteral)) {
    return makeInt(advance());
  }
  if (check(TokenKind::FloatLiteral)) {
    const Token t = advance();
    auto e = std::make_unique<Expr>();
    e->kind = ExprKind::FloatLit;
    e->loc = t.loc;
    e->floatValue = t.floatValue;
    return e;
  }
  if (check(TokenKind::StringLiteral)) {
    const Token t = advance();
    auto e = std::make_unique<Expr>();
    e->kind = ExprKind::StringLit;
    e->loc = t.loc;
    e->text = t.text;
    return e;
  }
  if (check(TokenKind::KwTrue) || check(TokenKind::KwFalse)) {
    const Token t = advance();
    auto e = std::make_unique<Expr>();
    e->kind = ExprKind::BoolLit;
    e->loc = t.loc;
    e->boolValue = t.kind == TokenKind::KwTrue;
    return e;
  }
  if (check(TokenKind::Identifier)) {
    const Token t = advance();
    if (check(TokenKind::LParen)) {
      advance();
      auto call = std::make_unique<Expr>();
      call->kind = ExprKind::Call;
      call->loc = t.loc;
      call->text = t.text;
      if (!check(TokenKind::RParen)) {
        call->args = parseArguments(sink);
      }
      if (!expect(TokenKind::RParen, "')'", sink)) return nullptr;
      return call;
    }
    auto ident = std::make_unique<Expr>();
    ident->kind = ExprKind::Ident;
    ident->loc = t.loc;
    ident->text = t.text;
    return ident;
  }
  if (match(TokenKind::LParen)) {
    ExprPtr inner = parseExpression(1, sink);
    if (!inner) return nullptr;
    if (!expect(TokenKind::RParen, "')'", sink)) return nullptr;
    return inner;
  }
  sink.error("MFC1013", peek().loc,
             std::string("expected an expression but found ") + tokenKindName(peek().kind), {}, {},
             file_);
  return nullptr;
}

Program parseScript(const std::string& source, const std::string& file, DiagnosticSink& sink) {
  Lexer lexer(source, file);
  std::vector<Token> tokens = lexer.tokenize(sink);
  Parser parser(std::move(tokens), file);
  return parser.parse(sink);
}

namespace {

void printExpr(const Expr& expr, std::string& out) {
  switch (expr.kind) {
    case ExprKind::IntLit: out += std::to_string(expr.intValue); break;
    case ExprKind::FloatLit: {
      char buf[64];
      std::snprintf(buf, sizeof(buf), "%.17g", expr.floatValue);
      out += buf;
      break;
    }
    case ExprKind::StringLit:
      out += '"';
      for (char c : expr.text) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
      }
      out += '"';
      break;
    case ExprKind::BoolLit: out += expr.boolValue ? "true" : "false"; break;
    case ExprKind::Ident: out += expr.text; break;
    case ExprKind::Unary:
      out += expr.op;
      out += "(";
      printExpr(*expr.lhs, out);
      out += ")";
      break;
    case ExprKind::Binary:
      out += "(";
      printExpr(*expr.lhs, out);
      out += " ";
      out += expr.op;
      out += " ";
      printExpr(*expr.rhs, out);
      out += ")";
      break;
    case ExprKind::Call:
      out += expr.text;
      out += "(";
      for (size_t i = 0; i < expr.args.size(); ++i) {
        if (i) out += ", ";
        printExpr(*expr.args[i], out);
      }
      out += ")";
      break;
  }
}

void printIndent(int indent, std::string& out) { out.append(static_cast<size_t>(indent) * 2, ' '); }

void printStmtInner(const Stmt& stmt, int indent, std::string& out) {
  switch (stmt.kind) {
    case StmtKind::Block:
      out += "{\n";
      for (const auto& child : stmt.body) {
        printIndent(indent + 1, out);
        printStmtInner(*child, indent + 1, out);
        out += "\n";
      }
      printIndent(indent, out);
      out += "}";
      break;
    case StmtKind::Let:
      out += "let " + stmt.name;
      if (stmt.hasDeclaredType) {
        out += ": ";
        out += typeName(stmt.declaredType);
      }
      out += " = ";
      printExpr(*stmt.expr, out);
      out += ";";
      break;
    case StmtKind::Assign:
      out += stmt.name + " = ";
      printExpr(*stmt.expr, out);
      out += ";";
      break;
    case StmtKind::If:
      out += "if (";
      printExpr(*stmt.expr, out);
      out += ") ";
      printStmtInner(*stmt.body[0], indent, out);
      if (!stmt.elseBody.empty()) {
        out += " else ";
        printStmtInner(*stmt.elseBody[0], indent, out);
      }
      break;
    case StmtKind::While:
      out += "while (";
      printExpr(*stmt.expr, out);
      out += ") ";
      printStmtInner(*stmt.body[0], indent, out);
      break;
    case StmtKind::WaitEvent:
      out += "wait event(" + stmt.eventName;
      if (stmt.filter) {
        out += ", ";
        printExpr(*stmt.filter, out);
      }
      out += ");";
      break;
    case StmtKind::WaitTime:
      out += "wait time(";
      printExpr(*stmt.expr, out);
      out += ");";
      break;
    case StmtKind::Emit:
    case StmtKind::Call:
      out += stmt.kind == StmtKind::Emit ? "emit " : "call ";
      out += stmt.callee + "(";
      for (size_t i = 0; i < stmt.args.size(); ++i) {
        if (i) out += ", ";
        printExpr(*stmt.args[i], out);
      }
      out += ");";
      break;
  }
}

}  // namespace

std::string printStatement(const Stmt& stmt, int indent) {
  std::string out;
  printStmtInner(stmt, indent, out);
  return out;
}

std::string printProgram(const Program& program) {
  std::string out;
  for (const auto& quest : program.quests) {
    out += "quest " + quest.name + " {\n";
    for (const auto& handler : quest.handlers) {
      if (handler.isStart) {
        out += "  on start ";
      } else {
        out += "  on event(" + handler.eventName;
        if (handler.filter) {
          out += ", ";
          printExpr(*handler.filter, out);
        }
        out += ") ";
      }
      out += printStatement(*handler.body, 1);
      out += "\n";
    }
    out += "}\n";
  }
  return out;
}

}  // namespace mfc
