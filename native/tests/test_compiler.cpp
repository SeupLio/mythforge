#include "mf_test.h"

#include <algorithm>

#include "compile_util.h"
#include "mfc/ir.h"

namespace {

const mfc::Handler* startHandler(const mfc::Program& program) {
  for (const auto& handler : program.quests.front().handlers) {
    if (handler.isStart) return &handler;
  }
  return nullptr;
}

bool hasConstant(const mfc::Chunk& chunk, int64_t value) {
  for (const auto& constant : chunk.constants) {
    if (constant.kind == mfc::Constant::Kind::Int && constant.i == value) return true;
  }
  return false;
}

}  // namespace

MF_TEST(compiler_emits_ssa_phi_for_loops) {
  const std::string source = R"(
    quest q {
      on start {
        let i = 0;
        while (i < 3) { i = i + 1; }
        emit Done(i);
      }
    }
  )";
  mfc::DiagnosticSink sink;
  const testutil::Compilation compilation = testutil::compileScript(source, sink);
  MF_CHECK(compilation.ok);
  if (!compilation.ok) {
    std::cout << sink.formatText() << "\n";
    return;
  }
  const mfc::Handler* handler = startHandler(compilation.program);
  MF_CHECK(handler != nullptr);
  if (handler == nullptr) return;
  const mfc::ir::Function fn = mfc::ir::buildFunction(*handler, "q/start", sink, "<test>");
  const std::string dump = mfc::ir::dumpFunction(fn);
  MF_CHECK(dump.find("phi") != std::string::npos);
  MF_CHECK(dump.find("branch") != std::string::npos);
}

MF_TEST(compiler_folds_constants) {
  const std::string source = "quest q { on start { let x = 1 + 2 * 3; emit Done(x); } }";
  mfc::DiagnosticSink sink;
  const testutil::Compilation compilation = testutil::compileScript(source, sink);
  MF_CHECK(compilation.ok);
  if (!compilation.ok) return;
  MF_CHECK_EQ(compilation.module.chunks.size(), static_cast<size_t>(1));
  MF_CHECK(hasConstant(compilation.module.chunks[0], 7));
}

MF_TEST(compiler_emits_debug_info_with_node_ids) {
  const std::string source = "quest q { on start { emit Done(1); } }";
  mfc::DiagnosticSink sink;
  const testutil::Compilation compilation = testutil::compileScript(source, sink);
  MF_CHECK(compilation.ok);
  if (!compilation.ok) return;
  MF_CHECK(!compilation.module.chunks[0].debug.empty());
  MF_CHECK(compilation.module.chunks[0].frameSize > 0);
  MF_CHECK(compilation.module.chunks[0].frameSize <= 255);
}

MF_TEST(compiler_emits_predicate_programs) {
  const std::string source =
      "quest q { on start { wait event(Kill, target == \"Wolf\" && count >= 2); "
      "emit Done(); } }";
  mfc::DiagnosticSink sink;
  const testutil::Compilation compilation = testutil::compileScript(source, sink);
  MF_CHECK(compilation.ok);
  if (!compilation.ok) return;
  MF_CHECK_EQ(compilation.module.chunks[0].predicates.size(), static_cast<size_t>(1));
  if (!compilation.module.chunks[0].predicates.empty()) {
    const std::string text = compilation.module.chunks[0].predicates[0].describe();
    MF_CHECK(text.find("Kill") == std::string::npos);  // describe only shows ops/fields
    MF_CHECK(text.find("target") != std::string::npos);
    MF_CHECK(text.find("count") != std::string::npos);
  }
}
