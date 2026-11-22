#include "mf_test.h"

#include "mfc/parser.h"
#include "mfc/sema.h"

namespace {

bool analyze(const std::string& source, mfc::DiagnosticSink& sink) {
  const mfc::Program program = mfc::parseScript(source, "<test>", sink);
  if (sink.hasErrors()) return false;
  mfc::Sema sema;
  return sema.analyze(program, sink, "<test>");
}

bool hasCode(const mfc::DiagnosticSink& sink, const std::string& code) {
  for (const auto& diagnostic : sink.all()) {
    if (diagnostic.code == code) return true;
  }
  return false;
}

const mfc::Diagnostic* findCode(const mfc::DiagnosticSink& sink, const std::string& code) {
  for (const auto& diagnostic : sink.all()) {
    if (diagnostic.code == code) return &diagnostic;
  }
  return nullptr;
}

}  // namespace

MF_TEST(sema_accepts_valid_quest) {
  mfc::DiagnosticSink sink;
  const bool ok = analyze(R"(
    quest q {
      on start {
        let killed = 0;
        while (killed < 2) {
          wait event(Kill, target == "Wolf");
          killed = killed + 1;
        }
        emit QuestCompleted("q");
      }
    }
  )",
                          sink);
  MF_CHECK(ok);
  MF_CHECK(!sink.hasErrors());
}

MF_TEST(sema_reports_undefined_variable_with_hint) {
  mfc::DiagnosticSink sink;
  analyze("quest q { on start { let killCount = 0; killCounts = 1; } }", sink);
  MF_CHECK(sink.hasErrors());
  const mfc::Diagnostic* diagnostic = findCode(sink, "MFC2007");
  MF_CHECK(diagnostic != nullptr);
  if (diagnostic) MF_CHECK(diagnostic->hint.find("killCount") != std::string::npos);
}

MF_TEST(sema_rejects_bad_initializer_type) {
  mfc::DiagnosticSink sink;
  analyze("quest q { on start { let x: int = 1.5; } }", sink);
  MF_CHECK(hasCode(sink, "MFC2006"));
}

MF_TEST(sema_rejects_float_to_int_assignment) {
  mfc::DiagnosticSink sink;
  analyze("quest q { on start { let x = 1; x = 2.5; } }", sink);
  MF_CHECK(hasCode(sink, "MFC2009"));
}

MF_TEST(sema_rejects_unknown_event_field) {
  mfc::DiagnosticSink sink;
  analyze("quest q { on start { wait event(Kill, targt == \"Wolf\"); } }", sink);
  MF_CHECK(hasCode(sink, "MFC2014"));
}

MF_TEST(sema_rejects_variables_inside_filters) {
  mfc::DiagnosticSink sink;
  analyze(R"(
    quest q {
      on start {
        let targetName = "Wolf";
        wait event(Kill, targetName == "Wolf");
      }
    }
  )",
          sink);
  MF_CHECK(hasCode(sink, "MFC2014"));
}

MF_TEST(sema_rejects_assignment_to_event_field) {
  mfc::DiagnosticSink sink;
  analyze(R"(
    quest q {
      on event(Kill) {
        target = "Boar";
        emit Log("x");
      }
    }
  )",
          sink);
  MF_CHECK(hasCode(sink, "MFC2008"));
}

MF_TEST(sema_warns_about_unused_variable) {
  mfc::DiagnosticSink sink;
  analyze("quest q { on start { let neverUsed = 1; } }", sink);
  MF_CHECK(!sink.hasErrors());
  MF_CHECK(hasCode(sink, "MFC2100"));
}

MF_TEST(sema_warns_about_unknown_event_with_suggestion) {
  mfc::DiagnosticSink sink;
  analyze("quest q { on start { wait event(Kil); } }", sink);
  const mfc::Diagnostic* diagnostic = findCode(sink, "MFC2102");
  MF_CHECK(diagnostic != nullptr);
  if (diagnostic) MF_CHECK(diagnostic->hint.find("Kill") != std::string::npos);
}

MF_TEST(sema_requires_start_handler) {
  mfc::DiagnosticSink sink;
  analyze("quest q { on event(Kill) { emit Log(\"x\"); } }", sink);
  MF_CHECK(hasCode(sink, "MFC2003"));
}
