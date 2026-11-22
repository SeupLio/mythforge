#include "mf_test.h"

#include <string>

#include "agent.h"
#include "mfc/graph.h"
#include "mfc/json.h"
#include "mfc/sema.h"

namespace {

bool loadSchema(mfc::json::Value& schema) {
  std::string text;
  std::string error;
  const std::string path = std::string(MYTHFORGE_SOURCE_DIR) + "/schema/node_types.json";
  if (!mfc::readFile(path, &text, &error)) return false;
  mfc::DiagnosticSink sink;
  return mfc::loadNodeSchema(text, schema, sink);
}

bool validates(mfc::json::Value& schema, const std::string& text, mfc::DiagnosticSink& sink) {
  mfc::Graph graph;
  if (!mfc::parseGraph(text, "<repaired>", graph, sink)) return false;
  if (!mfc::validateGraph(graph, schema, sink)) return false;
  const mfc::Program program = mfc::lowerGraph(graph, sink);
  if (sink.hasErrors()) return false;
  mfc::Sema sema;
  return sema.analyze(program, sink, "<repaired>");
}

}  // namespace

MF_TEST(agent_repairs_skill_graph) {
  mfc::json::Value schema;
  MF_CHECK(loadSchema(schema));
  const std::string broken = R"({
    "schemaVersion": 1,
    "kind": "skill",
    "id": "sk_broken",
    "nodes": [
      {"id": "n_a", "type": "skill.frame", "props": {"timeMs": 300, "event": "damage"}},
      {"id": "n_b", "type": "skill.frame", "props": {"timeMs": -5}}
    ],
    "edges": []
  })";
  mfc::DiagnosticSink sink;
  const mfagent::FixReport report = mfagent::fixGraphText(broken, "<inline>", schema, sink);
  MF_CHECK(report.changed);
  MF_CHECK_EQ(report.remainingErrors, 0);
  MF_CHECK(report.text.find("skill.cast") != std::string::npos);
  MF_CHECK(report.text.find("skill.end") != std::string::npos);
  MF_CHECK(report.fixedCount >= 4);

  mfc::DiagnosticSink revalidate;
  MF_CHECK(validates(schema, report.text, revalidate));
  MF_CHECK(!revalidate.hasErrors());
}

MF_TEST(agent_repairs_quest_props) {
  mfc::json::Value schema;
  MF_CHECK(loadSchema(schema));
  const std::string broken = R"({
    "schemaVersion": 1,
    "kind": "quest",
    "id": "q_broken",
    "nodes": [
      {"id": "s", "type": "quest.start", "props": {}},
      {"id": "k", "type": "quest.condition.kill", "props": {"count": 0}},
      {"id": "t", "type": "quest.timer", "props": {"seconds": -2}},
      {"id": "e", "type": "quest.end", "props": {"result": "weird"}}
    ],
    "edges": [{"from": "s", "to": "k"}, {"from": "k", "to": "t"}, {"from": "t", "to": "e"}]
  })";
  mfc::DiagnosticSink sink;
  const mfagent::FixReport report = mfagent::fixGraphText(broken, "<inline>", schema, sink);
  MF_CHECK(report.changed);
  MF_CHECK_EQ(report.remainingErrors, 0);
  MF_CHECK(report.text.find("\"Target\"") != std::string::npos);

  mfc::DiagnosticSink revalidate;
  MF_CHECK(validates(schema, report.text, revalidate));
}

MF_TEST(agent_leaves_design_values_unfixed) {
  mfc::json::Value schema;
  MF_CHECK(loadSchema(schema));
  const std::string broken = R"({
    "schemaVersion": 1,
    "kind": "dialogue",
    "id": "d_broken",
    "nodes": [
      {"id": "s", "type": "dialogue.start", "props": {}},
      {"id": "t", "type": "dialogue.timeline", "props": {"waitForFinish": true}},
      {"id": "e", "type": "dialogue.end", "props": {}}
    ],
    "edges": [{"from": "s", "to": "t"}, {"from": "t", "to": "e"}]
  })";
  mfc::DiagnosticSink sink;
  const mfagent::FixReport report = mfagent::fixGraphText(broken, "<inline>", schema, sink);
  // asset is a design value: filled as empty, reported as remaining instead of
  // inventing a path.
  MF_CHECK(report.remainingErrors > 0);
  MF_CHECK(report.report.find("remaining") != nullptr);
}
