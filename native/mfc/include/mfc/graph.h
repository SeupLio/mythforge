#pragma once

#include <string>
#include <vector>

#include "mfc/ast.h"
#include "mfc/common.h"
#include "mfc/json.h"

namespace mfc {

struct GraphNode {
  std::string id;
  std::string type;
  json::Value props;
};

struct GraphEdge {
  std::string from;
  std::string to;
};

struct Graph {
  int schemaVersion = 1;
  std::string kind = "quest";
  std::string id;
  std::string title;
  json::Value meta;
  std::vector<GraphNode> nodes;
  std::vector<GraphEdge> edges;
  std::string file;
};

// Parses a content graph produced by the Unity editor (or by hand).
bool parseGraph(const std::string& text, const std::string& file, Graph& out,
                DiagnosticSink& sink);

// Schema-driven validation: node types, required props, port degrees, single
// entry, acyclicity, reachability and "every branch terminates".
bool validateGraph(const Graph& graph, const json::Value& schema, DiagnosticSink& sink);

// Graph -> MythScript AST. The graph is only a different front end: lowering
// feeds the exact same sema/IR/optimizer/codegen pipeline as hand-written
// scripts, which guarantees one semantic definition.
Program lowerGraph(const Graph& graph, DiagnosticSink& sink);

// Kind-specific lowering (kept public so tests can target one front end).
Program lowerQuestGraph(const Graph& graph, DiagnosticSink& sink);
Program lowerDialogueGraph(const Graph& graph, DiagnosticSink& sink);
Program lowerSkillGraph(const Graph& graph, DiagnosticSink& sink);

// Chunk name for a lowered handler: quest/dialogue entries are "<id>/start",
// skills are "<id>/cast", listeners are "<id>/on_<event>". The VM starts both
// "/start" and "/cast" entry chunks.
std::string chunkNameFor(const Graph& graph, const Handler& handler);

bool loadNodeSchema(const std::string& text, json::Value& out, DiagnosticSink& sink);

// Quest ids referenced by `quest.depends` nodes; used by the incremental build
// to invalidate downstream content.
std::vector<std::string> questDependencies(const Graph& graph);

}  // namespace mfc
