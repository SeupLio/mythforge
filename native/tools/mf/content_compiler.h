#pragma once

#include <string>

#include "mfc/codegen.h"
#include "mfc/graph.h"
#include "mfc/hash.h"
#include "mfc/ir.h"
#include "mfc/json.h"
#include "mfc/module.h"
#include "mfc/sema.h"

namespace mfcli {

struct CompiledContent {
  bool ok = false;
  std::string kind;
  mfc::Graph graph;
  mfc::Module module;
  std::string dsl;
  std::string bytes;
};

// Shared by `mf` and the content service (`mfd`): graph -> validate -> lower ->
// sema -> IR -> bytecode -> serialized .mfb. One implementation, one semantic.
inline CompiledContent compileContentText(const std::string& path, const std::string& text,
                                          const mfc::json::Value& schema,
                                          mfc::DiagnosticSink& sink) {
  CompiledContent out;
  mfc::Graph graph;
  if (!mfc::parseGraph(text, path, graph, sink)) return out;
  if (!mfc::validateGraph(graph, schema, sink)) return out;
  mfc::Program program = mfc::lowerGraph(graph, sink);
  if (sink.hasErrors()) return out;

  mfc::Sema sema;
  if (!sema.analyze(program, sink, path)) return out;
  out.dsl = mfc::printProgram(program);

  if (program.quests.empty()) return out;
  mfc::Module module;
  module.id = graph.id;
  module.sourcePath = path;
  module.sourceHash = mfc::sha256Hex(text);
  const mfc::QuestDecl& decl = program.quests.front();
  for (const auto& handler : decl.handlers) {
    const std::string chunkName = mfc::chunkNameFor(graph, handler);
    mfc::ir::Function fn = mfc::ir::buildFunction(handler, chunkName, sink, path);
    if (sink.hasErrors()) return out;
    mfc::Chunk chunk;
    if (!mfc::generateChunk(fn, chunk, sink, path)) return out;
    module.chunks.push_back(std::move(chunk));
  }
  if (!mfc::verifyModule(module, sink)) return out;
  out.bytes = mfc::serializeModule(module, sink);
  if (sink.hasErrors()) return out;
  out.module = std::move(module);
  out.graph = std::move(graph);
  out.kind = out.graph.kind;
  out.ok = true;
  return out;
}

inline mfc::json::Value diagnosticsToJson(const mfc::DiagnosticSink& sink) {
  mfc::json::Value array = mfc::json::Value::array();
  for (const auto& diagnostic : sink.all()) {
    mfc::json::Value item = mfc::json::Value::object();
    item["code"] = mfc::json::Value::string(diagnostic.code);
    item["severity"] = mfc::json::Value::string(mfc::severityName(diagnostic.severity));
    item["message"] = mfc::json::Value::string(diagnostic.message);
    item["file"] = mfc::json::Value::string(diagnostic.file);
    item["line"] = mfc::json::Value::integer(diagnostic.loc.line);
    item["col"] = mfc::json::Value::integer(diagnostic.loc.col);
    item["nodeId"] = mfc::json::Value::string(diagnostic.nodeId);
    item["hint"] = mfc::json::Value::string(diagnostic.hint);
    array.push(std::move(item));
  }
  return array;
}

}  // namespace mfcli
