#include "mf_test.h"

#include <string>

#include "compile_util.h"
#include "mfc/graph.h"
#include "mfc/json.h"

namespace {

std::string contentPath(const std::string& relative) {
  return std::string(MYTHFORGE_SOURCE_DIR) + "/" + relative;
}

bool loadSchema(mfc::json::Value& schema) {
  std::string text;
  std::string error;
  if (!mfc::readFile(contentPath("schema/node_types.json"), &text, &error)) return false;
  mfc::DiagnosticSink sink;
  return mfc::loadNodeSchema(text, schema, sink);
}

bool loadGraph(const std::string& relative, mfc::Graph& graph, mfc::DiagnosticSink& sink) {
  std::string text;
  std::string error;
  if (!mfc::readFile(contentPath(relative), &text, &error)) return false;
  return mfc::parseGraph(text, relative, graph, sink);
}

bool hasCode(const mfc::DiagnosticSink& sink, const std::string& code) {
  for (const auto& diagnostic : sink.all()) {
    if (diagnostic.code == code) return true;
  }
  return false;
}

}  // namespace

MF_TEST(graph_validates_forest_wolves) {
  mfc::json::Value schema;
  MF_CHECK(loadSchema(schema));
  mfc::Graph graph;
  mfc::DiagnosticSink sink;
  MF_CHECK(loadGraph("content/quests/q_forest_wolves.json", graph, sink));
  MF_CHECK(!sink.hasErrors());
  MF_CHECK_STR_EQ(graph.id, "q_forest_wolves");
  MF_CHECK_EQ(graph.nodes.size(), static_cast<size_t>(5));
  MF_CHECK(mfc::validateGraph(graph, schema, sink));
  MF_CHECK(!sink.hasErrors());
}

MF_TEST(graph_lowers_to_mythscript) {
  mfc::json::Value schema;
  MF_CHECK(loadSchema(schema));
  mfc::Graph graph;
  mfc::DiagnosticSink sink;
  MF_CHECK(loadGraph("content/quests/q_forest_wolves.json", graph, sink));
  MF_CHECK(mfc::validateGraph(graph, schema, sink));
  const mfc::Program program = mfc::lowerQuestGraph(graph, sink);
  MF_CHECK(!sink.hasErrors());
  const std::string dsl = mfc::printProgram(program);
  MF_CHECK(dsl.find("wait event(Kill") != std::string::npos);
  MF_CHECK(dsl.find("while") != std::string::npos);
  MF_CHECK(dsl.find("GrantReward") != std::string::npos);
  MF_CHECK(dsl.find("QuestCompleted") != std::string::npos);
}

MF_TEST(graph_compiles_and_runs_end_to_end) {
  mfc::json::Value schema;
  MF_CHECK(loadSchema(schema));
  mfc::Graph graph;
  mfc::DiagnosticSink parseSink;
  MF_CHECK(loadGraph("content/quests/q_forest_wolves.json", graph, parseSink));
  mfc::DiagnosticSink sink;
  const testutil::Compilation compilation = testutil::compileGraph(graph, schema, sink);
  MF_CHECK(compilation.ok);
  if (!compilation.ok) {
    std::cout << sink.formatText() << "\n";
    return;
  }
  mfvm::VM vm;
  std::string error;
  MF_CHECK(vm.loadModule(compilation.module, &error));
  testutil::RecordingHost host;
  testutil::runQuest(vm, host,
                     {{"Kill", {{"target", "Wolf"}}},
                      {"Kill", {{"target", "Wolf"}}},
                      {"Talk", {{"npc", "Elder"}}}});
  MF_CHECK_STR_EQ(vm.lastError(), "");
  MF_CHECK_EQ(host.emits.size(), static_cast<size_t>(3));
  if (host.emits.size() == 3) {
    MF_CHECK_STR_EQ(host.emits[0], "QuestStarted");
    MF_CHECK_STR_EQ(host.emits[1], "ObjectiveProgress");
    MF_CHECK_STR_EQ(host.emits[2], "QuestCompleted");
  }
  MF_CHECK_EQ(host.calls.size(), static_cast<size_t>(1));
}

MF_TEST(graph_reports_missing_required_prop) {
  mfc::json::Value schema;
  MF_CHECK(loadSchema(schema));
  const std::string text = R"({
    "schemaVersion": 1,
    "kind": "quest",
    "id": "q_bad",
    "nodes": [
      {"id": "s", "type": "quest.start", "props": {}},
      {"id": "k", "type": "quest.condition.kill", "props": {"count": 1}},
      {"id": "e", "type": "quest.end", "props": {}}
    ],
    "edges": [{"from": "s", "to": "k"}, {"from": "k", "to": "e"}]
  })";
  mfc::Graph graph;
  mfc::DiagnosticSink sink;
  MF_CHECK(mfc::parseGraph(text, "<inline>", graph, sink));
  mfc::validateGraph(graph, schema, sink);
  MF_CHECK(hasCode(sink, "MFC5021"));
}

MF_TEST(graph_detects_cycles) {
  mfc::json::Value schema;
  MF_CHECK(loadSchema(schema));
  mfc::Graph graph;
  mfc::DiagnosticSink sink;
  MF_CHECK(loadGraph("content/quests/q_forest_wolves.json", graph, sink));
  graph.edges.push_back({"n_end", "n_start"});
  mfc::validateGraph(graph, schema, sink);
  MF_CHECK(hasCode(sink, "MFC5031"));
}

MF_TEST(graph_detects_unreachable_nodes) {
  mfc::json::Value schema;
  MF_CHECK(loadSchema(schema));
  mfc::Graph graph;
  mfc::DiagnosticSink sink;
  MF_CHECK(loadGraph("content/quests/q_forest_wolves.json", graph, sink));
  mfc::GraphNode orphan;
  orphan.id = "n_orphan";
  orphan.type = "quest.log";
  orphan.props = mfc::json::Value::object();
  orphan.props["text"] = mfc::json::Value::string("orphan");
  graph.nodes.push_back(std::move(orphan));
  mfc::validateGraph(graph, schema, sink);
  MF_CHECK(hasCode(sink, "MFC5029"));
}

MF_TEST(graph_rejects_branching_in_v1) {
  mfc::json::Value schema;
  MF_CHECK(loadSchema(schema));
  mfc::Graph graph;
  mfc::DiagnosticSink sink;
  MF_CHECK(loadGraph("content/quests/q_forest_wolves.json", graph, sink));
  graph.edges.push_back({"n_kill", "n_end"});
  mfc::validateGraph(graph, schema, sink);
  MF_CHECK(hasCode(sink, "MFC5025"));
}

MF_TEST(graph_dependencies_are_tracked) {
  mfc::Graph graph;
  mfc::DiagnosticSink sink;
  const std::string text = R"({
    "schemaVersion": 1,
    "kind": "quest",
    "id": "q_dep",
    "nodes": [
      {"id": "s", "type": "quest.start", "props": {}},
      {"id": "d", "type": "quest.depends", "props": {"questId": "q_forest_wolves"}},
      {"id": "e", "type": "quest.end", "props": {}}
    ],
    "edges": [{"from": "s", "to": "d"}, {"from": "d", "to": "e"}]
  })";
  MF_CHECK(mfc::parseGraph(text, "<inline>", graph, sink));
  const std::vector<std::string> deps = mfc::questDependencies(graph);
  MF_CHECK_EQ(deps.size(), static_cast<size_t>(1));
  if (!deps.empty()) MF_CHECK_STR_EQ(deps[0], "q_forest_wolves");
}

MF_TEST(dialogue_graph_validates_and_lowers) {
  mfc::json::Value schema;
  MF_CHECK(loadSchema(schema));
  mfc::Graph graph;
  mfc::DiagnosticSink sink;
  MF_CHECK(loadGraph("content/dialogues/d_elder_intro.json", graph, sink));
  MF_CHECK_STR_EQ(graph.kind, "dialogue");
  MF_CHECK(mfc::validateGraph(graph, schema, sink));
  MF_CHECK(!sink.hasErrors());
  const mfc::Program program = mfc::lowerDialogueGraph(graph, sink);
  MF_CHECK(!sink.hasErrors());
  const std::string dsl = mfc::printProgram(program);
  MF_CHECK(dsl.find("DialogueLine") != std::string::npos);
  MF_CHECK(dsl.find("DialogueAdvance") != std::string::npos);
  MF_CHECK(dsl.find("PlayTimeline") != std::string::npos);
  MF_CHECK(dsl.find("TimelineFinished") != std::string::npos);
  MF_CHECK(dsl.find("DialogueEnded") != std::string::npos);
}

MF_TEST(dialogue_runs_end_to_end) {
  mfc::json::Value schema;
  MF_CHECK(loadSchema(schema));
  mfc::Graph graph;
  mfc::DiagnosticSink parseSink;
  MF_CHECK(loadGraph("content/dialogues/d_elder_intro.json", graph, parseSink));
  mfc::DiagnosticSink sink;
  const testutil::Compilation compilation = testutil::compileGraph(graph, schema, sink);
  MF_CHECK(compilation.ok);
  if (!compilation.ok) {
    std::cout << sink.formatText() << "\n";
    return;
  }
  MF_CHECK_STR_EQ(compilation.module.chunks.front().name, "d_elder_intro/start");
  mfvm::VM vm;
  std::string error;
  MF_CHECK(vm.loadModule(compilation.module, &error));
  testutil::RecordingHost host;
  testutil::runQuest(vm, host,
                     {{"DialogueAdvance", {}},
                      {"DialogueChoiceSelected", {}},
                      {"TimelineFinished", {{"asset", "Assets/MythForgeContent/Timelines/TL_ElderIntro.playable"}}},
                      {"DialogueAdvance", {}}});
  MF_CHECK_STR_EQ(vm.lastError(), "");
  std::vector<std::string> expected = {"DialogueStarted", "DialogueLine", "DialogueChoice",
                                       "DialogueLine", "DialogueEnded"};
  MF_CHECK(host.emits == expected);
  MF_CHECK_EQ(host.calls.size(), static_cast<size_t>(1));
  if (!host.calls.empty()) {
    MF_CHECK(host.calls[0].find("PlayTimeline") == 0);
  }
}

MF_TEST(skill_graph_validates_and_lowers) {
  mfc::json::Value schema;
  MF_CHECK(loadSchema(schema));
  mfc::Graph graph;
  mfc::DiagnosticSink sink;
  MF_CHECK(loadGraph("content/skills/sk_wolf_bite.json", graph, sink));
  MF_CHECK_STR_EQ(graph.kind, "skill");
  MF_CHECK(mfc::validateGraph(graph, schema, sink));
  MF_CHECK(!sink.hasErrors());
  const mfc::Program program = mfc::lowerSkillGraph(graph, sink);
  MF_CHECK(!sink.hasErrors());
  const std::string dsl = mfc::printProgram(program);
  MF_CHECK(dsl.find("wait time(") != std::string::npos);
  MF_CHECK(dsl.find("SkillEvent(") != std::string::npos);
  MF_CHECK(dsl.find("SkillEnded") != std::string::npos);
}

MF_TEST(skill_timeline_runs_with_vm_timers) {
  mfc::json::Value schema;
  MF_CHECK(loadSchema(schema));
  mfc::Graph graph;
  mfc::DiagnosticSink parseSink;
  MF_CHECK(loadGraph("content/skills/sk_wolf_bite.json", graph, parseSink));
  mfc::DiagnosticSink sink;
  const testutil::Compilation compilation = testutil::compileGraph(graph, schema, sink);
  MF_CHECK(compilation.ok);
  if (!compilation.ok) {
    std::cout << sink.formatText() << "\n";
    return;
  }
  MF_CHECK_STR_EQ(compilation.module.chunks.front().name, "sk_wolf_bite/cast");
  mfvm::VM vm;
  std::string error;
  MF_CHECK(vm.loadModule(compilation.module, &error));
  testutil::RecordingHost host;
  testutil::runQuest(vm, host, {});
  MF_CHECK_STR_EQ(vm.lastError(), "");
  std::vector<std::string> expected = {"SkillCast", "SkillFrame", "SkillFrame", "SkillFrame",
                                       "SkillEnded"};
  MF_CHECK(host.emits == expected);
  MF_CHECK_EQ(host.calls.size(), static_cast<size_t>(3));
  MF_CHECK_EQ(vm.nowMicros(), static_cast<int64_t>(300000));
}

MF_TEST(skill_rejects_out_of_order_frames) {
  mfc::json::Value schema;
  MF_CHECK(loadSchema(schema));
  mfc::Graph graph;
  mfc::DiagnosticSink sink;
  MF_CHECK(loadGraph("content/skills/sk_wolf_bite.json", graph, sink));
  for (auto& node : graph.nodes) {
    if (node.id == "n_roar") node.props["timeMs"] = mfc::json::Value::integer(50);
  }
  mfc::validateGraph(graph, schema, sink);
  MF_CHECK(hasCode(sink, "MFC5039"));
}
