#include "mf_test.h"

#include "compile_util.h"
#include "mfc/bytecode.h"
#include "mfc/module.h"

MF_TEST(module_roundtrips_through_binary_format) {
  mfc::DiagnosticSink sink;
  const testutil::Compilation compilation =
      testutil::compileScript("quest q { on start { emit Done(1); } }", sink);
  MF_CHECK(compilation.ok);
  if (!compilation.ok) return;

  mfc::DiagnosticSink writeSink;
  const std::string bytes = mfc::serializeModule(compilation.module, writeSink);
  MF_CHECK(!writeSink.hasErrors());
  MF_CHECK(bytes.size() > 64);

  mfc::Module loaded;
  mfc::DiagnosticSink readSink;
  MF_CHECK(mfc::deserializeModule(bytes, loaded, readSink));
  MF_CHECK(!readSink.hasErrors());
  MF_CHECK_STR_EQ(loaded.id, "q");
  MF_CHECK_EQ(loaded.chunks.size(), compilation.module.chunks.size());
  if (!loaded.chunks.empty()) {
    MF_CHECK_EQ(loaded.chunks[0].code.size(), compilation.module.chunks[0].code.size());
    MF_CHECK_EQ(loaded.chunks[0].frameSize, compilation.module.chunks[0].frameSize);
    MF_CHECK_STR_EQ(loaded.chunks[0].name, "q/start");
  }
}

MF_TEST(module_rejects_tampered_bytes) {
  mfc::DiagnosticSink sink;
  const testutil::Compilation compilation =
      testutil::compileScript("quest q { on start { emit Done(1); } }", sink);
  MF_CHECK(compilation.ok);
  if (!compilation.ok) return;
  std::string bytes = mfc::serializeModule(compilation.module, sink);
  MF_CHECK(bytes.size() > 80);
  bytes[bytes.size() / 2] = static_cast<char>(bytes[bytes.size() / 2] ^ 0xFF);

  mfc::Module loaded;
  mfc::DiagnosticSink readSink;
  MF_CHECK(!mfc::deserializeModule(bytes, loaded, readSink));
  MF_CHECK(readSink.hasErrors());
  MF_CHECK_STR_EQ(readSink.all().front().code, "MFC4201");
}

MF_TEST(verifier_rejects_register_overflow) {
  mfc::DiagnosticSink sink;
  const testutil::Compilation compilation =
      testutil::compileScript("quest q { on start { emit Done(1); } }", sink);
  MF_CHECK(compilation.ok);
  if (!compilation.ok) return;
  mfc::Module broken = compilation.module;
  MF_CHECK(!broken.chunks.empty());
  if (broken.chunks.empty()) return;
  // Craft a LOADK_I that writes register 200 in a 2-register frame.
  broken.chunks[0].code[0] = mfc::bc::encodeABC(mfc::bc::LOADK_I, 200, 0, 0);
  broken.chunks[0].constants.clear();
  mfc::Constant constant;
  constant.kind = mfc::Constant::Kind::Int;
  constant.i = 1;
  broken.chunks[0].constants.push_back(constant);

  mfc::DiagnosticSink verifySink;
  MF_CHECK(!mfc::verifyModule(broken, verifySink));
  MF_CHECK(verifySink.hasErrors());
}

MF_TEST(verifier_rejects_out_of_range_jump) {
  mfc::DiagnosticSink sink;
  const testutil::Compilation compilation = testutil::compileScript(
      "quest q { on start { if (true) { emit A(); } else { emit B(); } } }", sink);
  MF_CHECK(compilation.ok);
  if (!compilation.ok) return;
  mfc::Module broken = compilation.module;
  broken.chunks[0].code[0] = mfc::bc::encodeJump(mfc::bc::JUMP, 0, 30000);
  mfc::DiagnosticSink verifySink;
  MF_CHECK(!mfc::verifyModule(broken, verifySink));
}
