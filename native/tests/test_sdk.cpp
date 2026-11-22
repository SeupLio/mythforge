#include "mf_test.h"

#include <string>

#include "compile_util.h"
#include "mf_sdk.h"
#include "mfc/graph.h"
#include "mfc/json.h"
#include "mfc/module.h"

namespace {

std::string sourcePath(const std::string& relative) {
  return std::string(MYTHFORGE_SOURCE_DIR) + "/" + relative;
}

bool loadSchema(mfc::json::Value& schema) {
  std::string text;
  std::string error;
  if (!mfc::readFile(sourcePath("schema/node_types.json"), &text, &error)) return false;
  mfc::DiagnosticSink sink;
  return mfc::loadNodeSchema(text, schema, sink);
}

bool buildModule(const std::string& relative, mfc::Module& module) {
  mfc::json::Value schema;
  if (!loadSchema(schema)) return false;
  std::string text;
  std::string error;
  if (!mfc::readFile(sourcePath(relative), &text, &error)) return false;
  mfc::DiagnosticSink sink;
  mfc::Graph graph;
  if (!mfc::parseGraph(text, relative, graph, sink)) return false;
  const testutil::Compilation compilation = testutil::compileGraph(graph, schema, sink);
  if (!compilation.ok) return false;
  module = compilation.module;
  return true;
}

std::string drain(mf_vm* vm) {
  std::string all;
  char line[8192];
  while (mf_vm_poll(vm, line, static_cast<int32_t>(sizeof(line))) == 1) {
    if (!all.empty()) all += "\n";
    all += line;
  }
  return all;
}

}  // namespace

MF_TEST(sdk_runs_quest_through_c_abi) {
  mfc::Module module;
  MF_CHECK(buildModule("content/quests/q_forest_wolves.json", module));
  mfc::DiagnosticSink sink;
  const std::string bytes = mfc::serializeModule(module, sink);
  MF_CHECK(!sink.hasErrors());

  mf_vm* vm = mf_vm_create();
  MF_CHECK(vm != nullptr);
  char error[1024] = {0};
  MF_CHECK_EQ(mf_vm_load_module(vm, reinterpret_cast<const uint8_t*>(bytes.data()),
                                static_cast<int32_t>(bytes.size()), error, sizeof(error)),
              1);
  MF_CHECK_STR_EQ(error, "");
  MF_CHECK(mf_vm_start(vm) >= 1);
  const std::string started = drain(vm);
  MF_CHECK(started.find("\"type\": \"start\"") != std::string::npos);
  MF_CHECK(started.find("QuestStarted") != std::string::npos);
  MF_CHECK(started.find("n_start") != std::string::npos);
  MF_CHECK(mf_vm_running(vm) == 1);

  MF_CHECK_EQ(mf_vm_emit(vm, "Kill", "{\"target\":\"Wolf\"}"), 1);
  MF_CHECK_EQ(mf_vm_emit(vm, "Kill", "{\"target\":\"Wolf\"}"), 1);
  MF_CHECK_EQ(mf_vm_emit(vm, "Talk", "{\"npc\":\"Elder\"}"), 1);
  const std::string finished = drain(vm);
  MF_CHECK(finished.find("QuestCompleted") != std::string::npos);
  MF_CHECK(finished.find("GrantReward") != std::string::npos);
  MF_CHECK(finished.find("n_end") != std::string::npos);
  MF_CHECK(mf_vm_running(vm) == 0);
  MF_CHECK_STR_EQ(mf_vm_last_error(vm), "");
  mf_vm_destroy(vm);
}

MF_TEST(sdk_drives_skill_timeline) {
  mfc::Module module;
  MF_CHECK(buildModule("content/skills/sk_wolf_bite.json", module));
  mfc::DiagnosticSink sink;
  const std::string bytes = mfc::serializeModule(module, sink);
  MF_CHECK(!sink.hasErrors());

  mf_vm* vm = mf_vm_create();
  char error[1024] = {0};
  MF_CHECK_EQ(mf_vm_load_module(vm, reinterpret_cast<const uint8_t*>(bytes.data()),
                                static_cast<int32_t>(bytes.size()), error, sizeof(error)),
              1);
  MF_CHECK(mf_vm_start(vm) >= 1);
  drain(vm);
  MF_CHECK_EQ(mf_vm_advance_time(vm, 300000), 1);
  MF_CHECK_EQ(mf_vm_advance_time(vm, 300000), 1);
  const std::string lines = drain(vm);
  MF_CHECK(lines.find("SkillCast") != std::string::npos ||
           lines.find("SkillFrame") != std::string::npos);
  MF_CHECK(lines.find("SkillEnded") != std::string::npos);
  MF_CHECK_EQ(mf_vm_now_micros(vm), static_cast<int64_t>(600000));
  MF_CHECK(mf_vm_running(vm) == 0);
  mf_vm_destroy(vm);
}
