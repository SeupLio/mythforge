#include "mf_test.h"

#include "mfc/parser.h"

MF_TEST(parser_builds_quest_and_handlers) {
  const std::string source = R"(
    quest q_demo {
      on start {
        let x = 1 + 2 * 3;
        if (x > 2 && x < 10) { emit A(x); } else { emit B(); }
        while (x < 5) { x = x + 1; }
        wait event(Kill, target == "Wolf");
        wait time(1.5);
      }
      on event(Talk, npc == "Elder") { emit C(); }
    }
  )";
  mfc::DiagnosticSink sink;
  const mfc::Program program = mfc::parseScript(source, "<test>", sink);
  MF_CHECK(!sink.hasErrors());

  MF_CHECK_EQ(program.quests.size(), static_cast<size_t>(1));
  const mfc::QuestDecl& quest = program.quests.front();
  MF_CHECK_STR_EQ(quest.name, "q_demo");
  MF_CHECK_EQ(quest.handlers.size(), static_cast<size_t>(2));
  MF_CHECK(quest.handlers[0].isStart);
  MF_CHECK(!quest.handlers[1].isStart);
  MF_CHECK_STR_EQ(quest.handlers[1].eventName, "Talk");
  MF_CHECK(quest.handlers[1].filter != nullptr);
  MF_CHECK_EQ(quest.handlers[0].body->body.size(), static_cast<size_t>(5));
}

MF_TEST(parser_respects_precedence) {
  mfc::DiagnosticSink sink;
  const mfc::Program program =
      mfc::parseScript("quest q { on start { let x = 1 + 2 * 3; } }", "<test>", sink);
  MF_CHECK(!sink.hasErrors());
  const mfc::Stmt& let = *program.quests[0].handlers[0].body->body[0];
  MF_CHECK(let.kind == mfc::StmtKind::Let);
  MF_CHECK_STR_EQ(let.expr->op, "+");
  MF_CHECK_STR_EQ(let.expr->rhs->op, "*");
  MF_CHECK_EQ(let.expr->lhs->intValue, 1);
  MF_CHECK_EQ(let.expr->rhs->lhs->intValue, 2);
  MF_CHECK_EQ(let.expr->rhs->rhs->intValue, 3);
}

MF_TEST(parser_recovers_after_errors) {
  mfc::DiagnosticSink sink;
  const mfc::Program program =
      mfc::parseScript("quest q { on start { let x = ; emit Ok(); } }", "<test>", sink);
  MF_CHECK(sink.hasErrors());
  (void)program;
}

MF_TEST(parser_reports_missing_brace) {
  mfc::DiagnosticSink sink;
  mfc::parseScript("quest q { on start { emit A(); ", "<test>", sink);
  MF_CHECK(sink.hasErrors());
}
