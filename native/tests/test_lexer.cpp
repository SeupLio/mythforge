#include "mf_test.h"

#include "mfc/lexer.h"

namespace {

std::vector<mfc::Token> lex(const std::string& source, mfc::DiagnosticSink& sink) {
  mfc::Lexer lexer(source, "<test>");
  return lexer.tokenize(sink);
}

}  // namespace

MF_TEST(lexer_tokenizes_basic_script) {
  mfc::DiagnosticSink sink;
  const auto tokens = lex("let x: int = 1 + 2.5; // comment\nemit Done(x);", sink);
  MF_CHECK(!sink.hasErrors());
  MF_CHECK(tokens.size() >= 12);
  MF_CHECK(tokens[0].kind == mfc::TokenKind::KwLet);
  MF_CHECK(tokens[1].kind == mfc::TokenKind::Identifier);
  MF_CHECK_STR_EQ(tokens[1].text, "x");
  MF_CHECK(tokens[2].kind == mfc::TokenKind::Colon);
  MF_CHECK(tokens[4].kind == mfc::TokenKind::Assign);
  MF_CHECK(tokens[5].kind == mfc::TokenKind::IntLiteral);
  MF_CHECK_EQ(tokens[5].intValue, 1);
  MF_CHECK(tokens[7].kind == mfc::TokenKind::FloatLiteral);
  MF_CHECK_NEAR(tokens[7].floatValue, 2.5, 1e-9);
}

MF_TEST(lexer_accepts_utf8_identifiers) {
  mfc::DiagnosticSink sink;
  const auto tokens = lex("let 狼数量 = 2; emit 提示(狼数量);", sink);
  MF_CHECK(!sink.hasErrors());
  MF_CHECK_EQ(tokens[1].text, std::string("\xE7\x8B\xBC\xE6\x95\xB0\xE9\x87\x8F"));
}

MF_TEST(lexer_skips_block_comments) {
  mfc::DiagnosticSink sink;
  const auto tokens = lex("/* a\n b */ let x = 1;", sink);
  MF_CHECK(!sink.hasErrors());
  MF_CHECK(tokens[0].kind == mfc::TokenKind::KwLet);
}

MF_TEST(lexer_reports_unterminated_string) {
  mfc::DiagnosticSink sink;
  lex("let x = \"oops;", sink);
  MF_CHECK(sink.hasErrors());
  MF_CHECK_STR_EQ(sink.all().front().code, "MFC0003");
}

MF_TEST(lexer_reports_invalid_character) {
  mfc::DiagnosticSink sink;
  lex("let x = 1 @ 2;", sink);
  MF_CHECK(sink.hasErrors());
  MF_CHECK_STR_EQ(sink.all().front().code, "MFC0006");
}
