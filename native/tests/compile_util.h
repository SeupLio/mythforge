#pragma once

#include <map>
#include <string>
#include <vector>

#include "mfc/codegen.h"
#include "mfc/graph.h"
#include "mfc/ir.h"
#include "mfc/json.h"
#include "mfc/module.h"
#include "mfc/parser.h"
#include "mfc/sema.h"
#include "mfvm/vm.h"

namespace testutil {

struct Compilation {
  bool ok = false;
  mfc::Program program;
  mfc::Module module;
  std::string dsl;
};

inline Compilation compileScript(const std::string& source, mfc::DiagnosticSink& sink) {
  Compilation out;
  out.program = mfc::parseScript(source, "<test>", sink);
  if (sink.hasErrors() || out.program.quests.empty()) return out;
  mfc::Sema sema;
  if (!sema.analyze(out.program, sink, "<test>")) return out;
  out.dsl = mfc::printProgram(out.program);
  const mfc::QuestDecl& quest = out.program.quests.front();
  out.module.id = quest.name;
  for (const auto& handler : quest.handlers) {
    const std::string chunkName =
        quest.name + (handler.isStart ? "/start" : "/on_" + handler.eventName);
    mfc::ir::Function fn = mfc::ir::buildFunction(handler, chunkName, sink, "<test>");
    if (sink.hasErrors()) return out;
    mfc::Chunk chunk;
    if (!mfc::generateChunk(fn, chunk, sink, "<test>")) return out;
    out.module.chunks.push_back(std::move(chunk));
  }
  if (!mfc::verifyModule(out.module, sink)) return out;
  out.ok = true;
  return out;
}

inline Compilation compileGraph(const mfc::Graph& graph, const mfc::json::Value& schema,
                                mfc::DiagnosticSink& sink) {
  Compilation out;
  if (!mfc::validateGraph(graph, schema, sink)) return out;
  out.program = mfc::lowerGraph(graph, sink);
  if (sink.hasErrors() || out.program.quests.empty()) return out;
  mfc::Sema sema;
  if (!sema.analyze(out.program, sink, "<graph>")) return out;
  out.dsl = mfc::printProgram(out.program);
  const mfc::QuestDecl& quest = out.program.quests.front();
  out.module.id = quest.name;
  for (const auto& handler : quest.handlers) {
    const std::string chunkName = mfc::chunkNameFor(graph, handler);
    mfc::ir::Function fn = mfc::ir::buildFunction(handler, chunkName, sink, "<graph>");
    if (sink.hasErrors()) return out;
    mfc::Chunk chunk;
    if (!mfc::generateChunk(fn, chunk, sink, "<graph>")) return out;
    out.module.chunks.push_back(std::move(chunk));
  }
  if (!mfc::verifyModule(out.module, sink)) return out;
  out.ok = true;
  return out;
}

struct RecordingHost : mfvm::Host {
  mfvm::VM* vm = nullptr;
  std::vector<std::string> emits;
  std::vector<std::string> calls;

  mfvm::Value call(const std::string& function, const std::vector<mfvm::Value>& args) override {
    std::string line = function + "(";
    for (size_t i = 0; i < args.size(); ++i) {
      if (i) line += ", ";
      line += vm->valueToString(args[i]);
    }
    line += ")";
    calls.push_back(std::move(line));
    return mfvm::Value::nil();
  }

  void emit(const std::string& event, const std::vector<mfvm::Value>& args) override {
    (void)args;
    emits.push_back(event);
  }
};

// Runs a whole quest: starts listeners/start, feeds events in order and drains
// timers between them.
inline void runQuest(mfvm::VM& vm, RecordingHost& host,
                     const std::vector<std::pair<std::string, std::vector<std::pair<std::string, std::string>>>>&
                         events) {
  host.vm = &vm;
  vm.setHost(&host);
  vm.startQuest();
  vm.runUntilAllIdle();
  for (const auto& event : events) {
    std::vector<std::pair<std::string, mfvm::Value>> fields;
    for (const auto& field : event.second) {
      fields.emplace_back(field.first, vm.stringValue(field.second));
    }
    vm.emitEvent(event.first, fields);
    vm.runUntilAllIdle();
  }
  vm.runUntilAllIdle();
}

}  // namespace testutil
