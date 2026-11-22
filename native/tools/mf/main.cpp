#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "agent.h"
#include "content_compiler.h"
#include "mfc/codegen.h"
#include "mfc/common.h"
#include "mfc/graph.h"
#include "mfc/hash.h"
#include "mfc/ir.h"
#include "mfc/json.h"
#include "mfc/module.h"
#include "mfc/parser.h"
#include "mfc/sema.h"
#include "mfvm/vm.h"

namespace fs = std::filesystem;

namespace {

// Bump when the compiler or bytecode format changes so stale artifacts from an
// older toolchain are never reused.
constexpr const char* kBuildCacheVersion = "mf-build-2";

struct Options {
  std::string command;
  std::vector<std::string> positional;
  std::string schemaPath = "schema/node_types.json";
  std::string outDir;
  std::string eventsPath;
  bool force = false;
  bool write = false;
  bool jsonOutput = false;
  int files = 100;
  int nodesPerFile = 20;
};

void printUsage() {
  std::printf(
      "MythForge content compiler (mf)\n"
      "\n"
      "Usage:\n"
      "  mf validate <content-dir> [--schema <path>] [--json]\n"
      "  mf build    <content-dir> [--schema <path>] [--out <dir>] [--force]\n"
      "  mf test     <content-dir> [--schema <path>]\n"
      "  mf stats    <content-dir>\n"
      "  mf dsl      <graph-file> [--schema <path>]\n"
      "  mf check    <graph-file> [--schema <path>] [--json]\n"
      "  mf sim      <graph-file> --events <events.json> [--schema <path>] [--json]\n"
      "  mf agent fix <graph-file> [--write] [--schema <path>] [--json]\n"
      "  mf gen-content --out <dir> [--files N] [--nodes N]\n"
      "\n"
      "Content layout:\n"
      "  <content-dir>/quests/*.json     quest graphs\n"
      "  <content-dir>/dialogues/*.json  dialogue graphs\n"
      "  <content-dir>/skills/*.json     skill timeline graphs\n"
      "  <content-dir>/tests/*.json      scripted regression cases\n"
      "\n"
      "Exit codes: 0 ok, 1 diagnostics, 2 usage/IO error.\n");
}

Options parseOptions(int argc, char** argv) {
  Options options;
  if (argc >= 2) options.command = argv[1];
  for (int i = 2; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg.rfind("--", 0) == 0) {
      const std::string name = arg.substr(2);
      std::string value = "1";
      if (i + 1 < argc && std::string(argv[i + 1]).rfind("--", 0) != 0) {
        value = argv[++i];
      }
      if (name == "schema") options.schemaPath = value;
      else if (name == "out") options.outDir = value;
      else if (name == "events") options.eventsPath = value;
      else if (name == "force") options.force = true;
      else if (name == "write") options.write = true;
      else if (name == "json") options.jsonOutput = true;
      else if (name == "files") options.files = std::max(1, std::atoi(value.c_str()));
      else if (name == "nodes") options.nodesPerFile = std::max(3, std::atoi(value.c_str()));
      else {
        std::fprintf(stderr, "unknown option --%s\n", name.c_str());
        std::exit(2);
      }
    } else {
      options.positional.push_back(arg);
    }
  }
  return options;
}

bool loadFile(const std::string& path, std::string* out) {
  std::string error;
  if (!mfc::readFile(path, out, &error)) {
    std::fprintf(stderr, "%s\n", error.c_str());
    return false;
  }
  return true;
}

bool loadSchema(const Options& options, mfc::json::Value& schema) {
  std::string text;
  if (!loadFile(options.schemaPath, &text)) return false;
  mfc::DiagnosticSink sink;
  if (!mfc::loadNodeSchema(text, schema, sink)) {
    std::fputs(sink.formatText().c_str(), stderr);
    return false;
  }
  return true;
}

using mfcli::CompiledContent;
using mfcli::compileContentText;

std::vector<fs::directory_entry> collectContentFiles(const fs::path& contentDir) {
  std::vector<fs::directory_entry> files;
  std::error_code ec;
  const char* kContentDirs[] = {"quests", "dialogues", "skills"};
  bool foundSubdir = false;
  for (const char* name : kContentDirs) {
    const fs::path directory = contentDir / name;
    if (!fs::is_directory(directory, ec)) continue;
    foundSubdir = true;
    for (const auto& entry : fs::recursive_directory_iterator(directory, ec)) {
      if (entry.is_regular_file() && entry.path().extension() == ".json") {
        files.push_back(entry);
      }
    }
  }
  if (!foundSubdir) {
    for (const auto& entry : fs::directory_iterator(contentDir, ec)) {
      if (entry.is_regular_file() && entry.path().extension() == ".json") {
        files.push_back(entry);
      }
    }
  }
  std::sort(files.begin(), files.end(), [](const fs::directory_entry& a,
                                           const fs::directory_entry& b) {
    return a.path().generic_string() < b.path().generic_string();
  });
  return files;
}

int cmdValidate(const Options& options) {
  if (options.positional.empty()) {
    std::fprintf(stderr, "validate requires a content directory\n");
    return 2;
  }
  mfc::json::Value schema;
  if (!loadSchema(options, schema)) return 2;
  const fs::path contentDir = options.positional[0];
  const std::vector<fs::directory_entry> files = collectContentFiles(contentDir);
  if (files.empty()) {
    std::fprintf(stderr, "no content files found under '%s'\n", contentDir.string().c_str());
    return 2;
  }
  int errors = 0;
  int warnings = 0;
  int okFiles = 0;
  mfc::DiagnosticSink all;
  for (const auto& fileEntry : files) {
    const fs::path& path = fileEntry.path();
    std::string text;
    if (!loadFile(path.string(), &text)) return 2;
    mfc::DiagnosticSink sink;
    const CompiledContent compiled = compileContentText(path.string(), text, schema, sink);
    for (const auto& diagnostic : sink.all()) {
      all.add(diagnostic);
    }
    if (!sink.all().empty() && !options.jsonOutput) {
      std::printf("%s", sink.formatText().c_str());
    }
    errors += sink.errorCount();
    warnings += sink.warningCount();
    if (compiled.ok) ++okFiles;
  }
  if (options.jsonOutput) {
    mfc::json::Value report = mfc::json::Value::object();
    report["files"] = mfc::json::Value::integer(static_cast<int64_t>(files.size()));
    report["ok"] = mfc::json::Value::integer(okFiles);
    report["errors"] = mfc::json::Value::integer(errors);
    report["warnings"] = mfc::json::Value::integer(warnings);
    mfc::json::Value diagnostics;
    std::string error;
    if (!mfc::json::parse(all.formatJson(), diagnostics, &error)) {
      diagnostics = mfc::json::Value::array();
    }
    report["diagnostics"] = std::move(diagnostics);
    std::printf("%s\n", mfc::json::dump(report).c_str());
    return errors == 0 ? 0 : 1;
  }
  std::printf("validate: %zu file(s), %d ok, %d error(s), %d warning(s)\n", files.size(),
              okFiles, errors, warnings);
  return errors == 0 ? 0 : 1;
}

struct CacheEntry {
  std::string hash;
  std::string questId;
  std::string kind;
  std::string artifact;
  std::string artifactHash;
  std::vector<std::string> chunks;
  std::vector<std::string> deps;
};

struct BuildEntry {
  fs::path path;
  std::string relPath;
  std::string text;
  std::string hash;
  std::string questId;
  std::string kind;
  std::vector<std::string> deps;
  bool parsed = false;
  bool recompiled = false;
  std::string artifactName;
  std::string artifactHash;
  std::vector<std::string> chunks;
};

mfc::json::Value loadCache(const fs::path& path) {
  mfc::json::Value root;
  std::string text;
  std::error_code ec;
  if (!fs::exists(path, ec)) return root;
  if (!loadFile(path.string(), &text)) return root;
  std::string error;
  if (!mfc::json::parse(text, root, &error)) return mfc::json::Value();
  return root;
}

int cmdBuild(const Options& options) {
  if (options.positional.empty()) {
    std::fprintf(stderr, "build requires a content directory\n");
    return 2;
  }
  mfc::json::Value schema;
  if (!loadSchema(options, schema)) return 2;

  const auto startTime = std::chrono::steady_clock::now();
  const fs::path contentDir = options.positional[0];
  const std::vector<fs::directory_entry> files = collectContentFiles(contentDir);
  if (files.empty()) {
    std::fprintf(stderr, "no content files found under '%s'\n", contentDir.string().c_str());
    return 2;
  }
  const fs::path outDir = options.outDir.empty() ? contentDir / "out" : fs::path(options.outDir);
  std::error_code ec;
  fs::create_directories(outDir, ec);

  // Load the incremental cache.
  const fs::path cachePath = contentDir / ".mfcache" / "build.json";
  std::map<std::string, CacheEntry> cache;
  {
    mfc::json::Value root = loadCache(cachePath);
    std::string cacheVersion;
    root.getString("compiler", &cacheVersion);
    const mfc::json::Value* entries = root.find("files");
    if (cacheVersion == kBuildCacheVersion && entries && entries->isObject()) {
      for (const auto& item : entries->members()) {
        CacheEntry entry;
        item.second.getString("hash", &entry.hash);
        item.second.getString("quest", &entry.questId);
        item.second.getString("kind", &entry.kind);
        item.second.getString("artifact", &entry.artifact);
        item.second.getString("artifactHash", &entry.artifactHash);
        if (const mfc::json::Value* chunks = item.second.find("chunks")) {
          for (const auto& chunk : chunks->items()) {
            if (chunk.isString()) entry.chunks.push_back(chunk.asString());
          }
        }
        if (const mfc::json::Value* deps = item.second.find("deps")) {
          for (const auto& dep : deps->items()) {
            if (dep.isString()) entry.deps.push_back(dep.asString());
          }
        }
        cache[item.first] = std::move(entry);
      }
    }
  }

  std::vector<BuildEntry> entries;
  entries.reserve(files.size());
  int errors = 0;
  int warnings = 0;
  std::set<std::string> existingArtifacts;
  if (fs::is_directory(outDir, ec)) {
    for (const auto& artifact : fs::directory_iterator(outDir, ec)) {
      if (artifact.is_regular_file()) existingArtifacts.insert(artifact.path().filename().string());
    }
  }
  for (const auto& fileEntry : files) {
    const fs::path& path = fileEntry.path();
    BuildEntry entry;
    entry.path = path;
    // lexically_relative is pure string math; fs::relative would canonicalize
    // both paths and add two stat calls per file (28s on a 10k-file corpus).
    entry.relPath = path.lexically_relative(contentDir).generic_string();

    auto cached = cache.find(entry.relPath);
    if (!loadFile(path.string(), &entry.text)) return 2;
    entry.hash = mfc::sha256Hex(entry.text);

    // Same bytes as the last build: reuse id and dependency edges without
    // re-parsing the graph. Only changed content goes through validation.
    if (!options.force && cached != cache.end() && cached->second.hash == entry.hash &&
        !cached->second.questId.empty()) {
      entry.questId = cached->second.questId;
      entry.kind = cached->second.kind;
      entry.deps = cached->second.deps;
      entry.parsed = true;
      entries.push_back(std::move(entry));
      continue;
    }

    mfc::DiagnosticSink sink;
    mfc::Graph graph;
    if (mfc::parseGraph(entry.text, path.string(), graph, sink)) {
      entry.questId = graph.id;
      entry.kind = graph.kind;
      entry.deps = mfc::questDependencies(graph);
      if (mfc::validateGraph(graph, schema, sink)) entry.parsed = true;
    }
    if (!sink.all().empty()) std::printf("%s", sink.formatText().c_str());
    errors += sink.errorCount();
    warnings += sink.warningCount();
    entries.push_back(std::move(entry));
  }

  // Changed = source hash differs, artifact missing, or a dependency changed.
  std::set<std::string> changed;
  for (const auto& entry : entries) {
    if (entry.questId.empty()) continue;
    auto it = cache.find(entry.relPath);
    const bool artifactMissing =
        existingArtifacts.count(entry.questId + ".mfb") == 0;
    if (options.force || it == cache.end() || it->second.hash != entry.hash || artifactMissing) {
      changed.insert(entry.questId);
    }
  }
  bool propagated = true;
  while (propagated) {
    propagated = false;
    for (const auto& entry : entries) {
      if (entry.questId.empty() || changed.count(entry.questId) != 0) continue;
      for (const auto& dep : entry.deps) {
        if (changed.count(dep) != 0) {
          changed.insert(entry.questId);
          propagated = true;
          break;
        }
      }
    }
  }

  std::vector<const BuildEntry*> successful;
  int recompiled = 0;
  for (auto& entry : entries) {
    if (entry.questId.empty() || !entry.parsed) continue;
    const bool mustCompile = options.force || changed.count(entry.questId) != 0;
    if (mustCompile) {
      mfc::DiagnosticSink sink;
      CompiledContent compiled = compileContentText(entry.path.string(), entry.text, schema, sink);
      if (!sink.all().empty()) std::printf("%s", sink.formatText().c_str());
      errors += sink.errorCount();
      warnings += sink.warningCount();
      if (!compiled.ok) continue;
      const std::string artifactName = entry.questId + ".mfb";
      const fs::path artifactPath = outDir / artifactName;
      std::string writeError;
      if (!mfc::writeFile(artifactPath.string(), compiled.bytes, &writeError)) {
        std::fprintf(stderr, "%s\n", writeError.c_str());
        return 2;
      }
      entry.artifactName = artifactName;
      entry.artifactHash = mfc::sha256Hex(compiled.bytes);
      entry.kind = compiled.kind;
      entry.chunks.clear();
      for (const auto& chunk : compiled.module.chunks) entry.chunks.push_back(chunk.name);
      entry.recompiled = true;
      ++recompiled;
    } else {
      auto it = cache.find(entry.relPath);
      if (it == cache.end()) continue;
      entry.artifactName = it->second.artifact;
      entry.artifactHash = it->second.artifactHash;
      entry.chunks = it->second.chunks;
    }
    if (entry.artifactName.empty()) continue;
    successful.push_back(&entry);
  }

  // Manifest (sorted by quest id for stable diffs).
  std::sort(successful.begin(), successful.end(),
            [](const BuildEntry* a, const BuildEntry* b) { return a->questId < b->questId; });
  mfc::json::Value manifest = mfc::json::Value::object();
  manifest["formatVersion"] = mfc::json::Value::integer(1);
  mfc::json::Value modules = mfc::json::Value::array();
  mfc::json::Value cacheFiles = mfc::json::Value::object();
  for (const BuildEntry* entry : successful) {
    mfc::json::Value module = mfc::json::Value::object();
    module["id"] = mfc::json::Value::string(entry->questId);
    module["kind"] = mfc::json::Value::string(entry->kind);
    module["source"] = mfc::json::Value::string(entry->relPath);
    module["sourceHash"] = mfc::json::Value::string(entry->hash);
    module["artifact"] = mfc::json::Value::string(entry->artifactName);
    module["artifactHash"] = mfc::json::Value::string(entry->artifactHash);
    mfc::json::Value chunks = mfc::json::Value::array();
    for (const auto& chunk : entry->chunks) chunks.push(mfc::json::Value::string(chunk));
    module["chunks"] = std::move(chunks);
    modules.push(std::move(module));

    mfc::json::Value cacheEntry = mfc::json::Value::object();
    cacheEntry["hash"] = mfc::json::Value::string(entry->hash);
    cacheEntry["quest"] = mfc::json::Value::string(entry->questId);
    cacheEntry["kind"] = mfc::json::Value::string(entry->kind);
    cacheEntry["artifact"] = mfc::json::Value::string(entry->artifactName);
    cacheEntry["artifactHash"] = mfc::json::Value::string(entry->artifactHash);
    mfc::json::Value cacheChunks = mfc::json::Value::array();
    for (const auto& chunk : entry->chunks) cacheChunks.push(mfc::json::Value::string(chunk));
    cacheEntry["chunks"] = std::move(cacheChunks);
    mfc::json::Value deps = mfc::json::Value::array();
    for (const auto& dep : entry->deps) deps.push(mfc::json::Value::string(dep));
    cacheEntry["deps"] = std::move(deps);
    cacheFiles[entry->relPath] = std::move(cacheEntry);
  }
  manifest["modules"] = std::move(modules);

  mfc::json::Value cacheRoot = mfc::json::Value::object();
  cacheRoot["version"] = mfc::json::Value::integer(1);
  cacheRoot["compiler"] = mfc::json::Value::string(kBuildCacheVersion);
  cacheRoot["files"] = std::move(cacheFiles);

  std::string writeError;
  if (!mfc::writeFile((outDir / "manifest.json").string(),
                      mfc::json::dump(manifest) + "\n", &writeError)) {
    std::fprintf(stderr, "%s\n", writeError.c_str());
    return 2;
  }
  fs::create_directories(cachePath.parent_path(), ec);
  if (!mfc::writeFile(cachePath.string(), mfc::json::dump(cacheRoot) + "\n", &writeError)) {
    std::fprintf(stderr, "%s\n", writeError.c_str());
    return 2;
  }

  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - startTime)
                           .count();
  std::printf("build: %zu file(s), %d compiled, %d warning(s), %d error(s), %lld ms -> %s\n",
              entries.size(), recompiled, warnings, errors,
              static_cast<long long>(elapsed), outDir.string().c_str());
  return errors == 0 ? 0 : 1;
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
    (void)args;
    return mfvm::Value::nil();
  }

  void emit(const std::string& event, const std::vector<mfvm::Value>& args) override {
    (void)args;
    emits.push_back(event);
  }
};

mfvm::Value jsonToValue(const mfc::json::Value& value, mfvm::VM* vm) {
  if (value.isString()) return vm->stringValue(value.asString());
  if (value.isBool()) return mfvm::Value::boolean(value.asBool());
  const double number = value.asDouble();
  if (number == static_cast<double>(value.asInt())) return mfvm::Value::integer(value.asInt());
  return mfvm::Value::number(number);
}

mfc::json::Value diagnosticsToJson(const mfc::DiagnosticSink& sink) {
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

int cmdCheck(const Options& options) {
  if (options.positional.empty()) {
    std::fprintf(stderr, "check requires a graph file\n");
    return 2;
  }
  mfc::json::Value schema;
  if (!loadSchema(options, schema)) return 2;
  std::string text;
  if (!loadFile(options.positional[0], &text)) return 2;
  mfc::DiagnosticSink sink;
  const CompiledContent compiled =
      compileContentText(options.positional[0], text, schema, sink);
  if (options.jsonOutput) {
    mfc::json::Value report = mfc::json::Value::object();
    report["ok"] = mfc::json::Value::boolean(compiled.ok);
    report["error"] = mfc::json::Value::string(
        compiled.ok ? "" : (sink.hasErrors() ? "compilation failed" : "unknown error"));
    report["diagnostics"] = diagnosticsToJson(sink);
    if (compiled.ok) {
      mfc::json::Value chunks = mfc::json::Value::array();
      for (const auto& chunk : compiled.module.chunks) {
        chunks.push(mfc::json::Value::string(chunk.name));
      }
      report["chunks"] = std::move(chunks);
    }
    std::printf("%s\n", mfc::json::dump(report).c_str());
  } else {
    std::printf("%s", sink.formatText().c_str());
    std::printf("check: %s\n", compiled.ok ? "ok" : "failed");
  }
  return compiled.ok ? 0 : 1;
}

int cmdSim(const Options& options) {
  if (options.positional.empty() || options.eventsPath.empty()) {
    std::fprintf(stderr, "sim requires a graph file and --events <file>\n");
    return 2;
  }
  mfc::json::Value schema;
  if (!loadSchema(options, schema)) return 2;
  std::string questText;
  if (!loadFile(options.positional[0], &questText)) return 2;

  mfc::DiagnosticSink sink;
  CompiledContent compiled = compileContentText(options.positional[0], questText, schema, sink);
  if (!compiled.ok) {
    if (options.jsonOutput) {
      mfc::json::Value report = mfc::json::Value::object();
      report["ok"] = mfc::json::Value::boolean(false);
      report["error"] = mfc::json::Value::string("compilation failed");
      report["emits"] = mfc::json::Value::array();
      report["calls"] = mfc::json::Value::array();
      report["diagnostics"] = diagnosticsToJson(sink);
      std::printf("%s\n", mfc::json::dump(report).c_str());
    } else {
      std::printf("%s", sink.formatText().c_str());
      std::printf("sim: compilation failed\n");
    }
    return 1;
  }

  std::string eventsText;
  if (!loadFile(options.eventsPath, &eventsText)) return 2;
  mfc::json::Value eventsRoot;
  std::string parseError;
  if (!mfc::json::parse(eventsText, eventsRoot, &parseError)) {
    std::fprintf(stderr, "invalid events JSON: %s\n", parseError.c_str());
    return 2;
  }

  mfvm::VM vm;
  std::string loadError;
  if (!vm.loadModule(compiled.module, &loadError)) {
    std::fprintf(stderr, "load failed: %s\n", loadError.c_str());
    return 2;
  }
  RecordingHost host;
  host.vm = &vm;
  vm.setHost(&host);
  vm.startQuest();
  vm.runUntilAllIdle();
  if (const mfc::json::Value* events = eventsRoot.find("events"); events && events->isArray()) {
    for (const auto& event : events->items()) {
      std::string name;
      event.getString("name", &name);
      std::vector<std::pair<std::string, mfvm::Value>> fields;
      if (const mfc::json::Value* fieldObject = event.find("fields");
          fieldObject && fieldObject->isObject()) {
        for (const auto& field : fieldObject->members()) {
          fields.emplace_back(field.first, jsonToValue(field.second, &vm));
        }
      }
      vm.emitEvent(name, fields);
      vm.runUntilAllIdle();
    }
  }
  vm.runUntilAllIdle();

  mfc::json::Value report = mfc::json::Value::object();
  report["ok"] = mfc::json::Value::boolean(vm.lastError().empty());
  report["error"] = mfc::json::Value::string(vm.lastError());
  mfc::json::Value emits = mfc::json::Value::array();
  for (const auto& emit : host.emits) emits.push(mfc::json::Value::string(emit));
  report["emits"] = std::move(emits);
  mfc::json::Value calls = mfc::json::Value::array();
  for (const auto& call : host.calls) calls.push(mfc::json::Value::string(call));
  report["calls"] = std::move(calls);
  report["nowMicros"] = mfc::json::Value::integer(vm.nowMicros());
  report["diagnostics"] = diagnosticsToJson(sink);

  if (options.jsonOutput) {
    std::printf("%s\n", mfc::json::dump(report).c_str());
  } else {
    for (const auto& emit : host.emits) std::printf("emit %s\n", emit.c_str());
    for (const auto& call : host.calls) std::printf("call %s\n", call.c_str());
    if (!vm.lastError().empty()) std::printf("error: %s\n", vm.lastError().c_str());
  }
  return vm.lastError().empty() ? 0 : 1;
}

int cmdTest(const Options& options) {
  if (options.positional.empty()) {
    std::fprintf(stderr, "test requires a content directory\n");
    return 2;
  }
  mfc::json::Value schema;
  if (!loadSchema(options, schema)) return 2;

  const fs::path contentDir = options.positional[0];
  std::map<std::string, mfc::Module> modules;
  int buildErrors = 0;
  for (const auto& fileEntry : collectContentFiles(contentDir)) {
    const fs::path& path = fileEntry.path();
    std::string text;
    if (!loadFile(path.string(), &text)) return 2;
    mfc::DiagnosticSink sink;
    CompiledContent compiled = compileContentText(path.string(), text, schema, sink);
    if (!sink.all().empty()) std::printf("%s", sink.formatText().c_str());
    buildErrors += sink.errorCount();
    if (compiled.ok) modules[compiled.module.id] = std::move(compiled.module);
  }
  if (buildErrors != 0) {
    std::printf("test: %d build error(s), aborting\n", buildErrors);
    return 1;
  }

  const fs::path testsDir = contentDir / "tests";
  std::vector<fs::path> caseFiles;
  std::error_code ec;
  if (fs::is_directory(testsDir, ec)) {
    for (const auto& entry : fs::recursive_directory_iterator(testsDir, ec)) {
      if (entry.is_regular_file() && entry.path().extension() == ".json") {
        caseFiles.push_back(entry.path());
      }
    }
  }
  std::sort(caseFiles.begin(), caseFiles.end());
  if (caseFiles.empty()) {
    std::printf("test: no case files under '%s'\n", testsDir.string().c_str());
    return 2;
  }

  int passed = 0;
  int failed = 0;
  for (const auto& casePath : caseFiles) {
    std::string text;
    if (!loadFile(casePath.string(), &text)) return 2;
    mfc::json::Value root;
    std::string error;
    if (!mfc::json::parse(text, root, &error)) {
      std::printf("%s: invalid JSON: %s\n", casePath.string().c_str(), error.c_str());
      ++failed;
      continue;
    }
    const mfc::json::Value* cases = root.find("cases");
    if (!cases || !cases->isArray()) {
      std::printf("%s: missing 'cases' array\n", casePath.string().c_str());
      ++failed;
      continue;
    }
    for (const auto& testCase : cases->items()) {
      std::string name;
      std::string questId;
      testCase.getString("name", &name);
      // "quest" is the historical key; "content" and "id" accept any module
      // (dialogue/skill included).
      testCase.getString("quest", &questId);
      if (questId.empty()) testCase.getString("content", &questId);
      if (questId.empty()) testCase.getString("id", &questId);
      auto moduleIt = modules.find(questId);
      if (moduleIt == modules.end()) {
        std::printf("[FAIL] %s: unknown content '%s'\n", name.c_str(), questId.c_str());
        ++failed;
        continue;
      }
      mfvm::VM vm;
      std::string loadError;
      if (!vm.loadModule(moduleIt->second, &loadError)) {
        std::printf("[FAIL] %s: %s\n", name.c_str(), loadError.c_str());
        ++failed;
        continue;
      }
      RecordingHost host;
      host.vm = &vm;
      vm.setHost(&host);
      vm.startQuest();
      vm.runUntilAllIdle();
      if (const mfc::json::Value* events = testCase.find("events"); events && events->isArray()) {
        for (const auto& event : events->items()) {
          std::string eventName;
          event.getString("name", &eventName);
          std::vector<std::pair<std::string, mfvm::Value>> fields;
          if (const mfc::json::Value* fieldObject = event.find("fields");
              fieldObject && fieldObject->isObject()) {
            for (const auto& field : fieldObject->members()) {
              fields.emplace_back(field.first, jsonToValue(field.second, &vm));
            }
          }
          vm.emitEvent(eventName, fields);
          vm.runUntilAllIdle();
        }
      }
      vm.runUntilAllIdle();

      std::vector<std::string> expected;
      if (const mfc::json::Value* expect = testCase.find("expectEmits");
          expect && expect->isArray()) {
        for (const auto& item : expect->items()) {
          if (item.isString()) expected.push_back(item.asString());
        }
      }
      const bool ok = host.emits == expected && vm.lastError().empty();
      if (ok) {
        std::printf("[PASS] %s\n", name.c_str());
        ++passed;
      } else {
        std::printf("[FAIL] %s\n", name.c_str());
        if (!vm.lastError().empty()) {
          std::printf("       vm error: %s\n", vm.lastError().c_str());
        }
        std::printf("       expected emits:");
        for (const auto& item : expected) std::printf(" %s", item.c_str());
        std::printf("\n       actual emits:  ");
        for (const auto& item : host.emits) std::printf(" %s", item.c_str());
        std::printf("\n");
        if (!host.calls.empty()) {
          std::printf("       host calls:   ");
          for (const auto& item : host.calls) std::printf(" %s;", item.c_str());
          std::printf("\n");
        }
        ++failed;
      }
    }
  }
  std::printf("test: %d passed, %d failed\n", passed, failed);
  return failed == 0 ? 0 : 1;
}

int cmdStats(const Options& options) {
  if (options.positional.empty()) {
    std::fprintf(stderr, "stats requires a content directory\n");
    return 2;
  }
  const fs::path contentDir = options.positional[0];
  std::map<std::string, int> nodeTypes;
  size_t totalNodes = 0;
  size_t totalEdges = 0;
  size_t totalBytes = 0;
  int files = 0;
  for (const auto& fileEntry : collectContentFiles(contentDir)) {
    const fs::path& path = fileEntry.path();
    std::string text;
    if (!loadFile(path.string(), &text)) return 2;
    totalBytes += text.size();
    mfc::json::Value root;
    std::string error;
    if (!mfc::json::parse(text, root, &error)) continue;
    ++files;
    if (const mfc::json::Value* nodes = root.find("nodes"); nodes && nodes->isArray()) {
      totalNodes += nodes->size();
      for (const auto& node : nodes->items()) {
        std::string type;
        node.getString("type", &type);
        nodeTypes[type] += 1;
      }
    }
    if (const mfc::json::Value* edges = root.find("edges"); edges && edges->isArray()) {
      totalEdges += edges->size();
    }
  }
  std::printf("stats: %d file(s), %zu node(s), %zu edge(s), %zu bytes\n", files, totalNodes,
              totalEdges, totalBytes);
  for (const auto& item : nodeTypes) {
    std::printf("  %-28s %d\n", item.first.c_str(), item.second);
  }
  return 0;
}

int cmdDsl(const Options& options) {
  if (options.positional.empty()) {
    std::fprintf(stderr, "dsl requires a graph file\n");
    return 2;
  }
  mfc::json::Value schema;
  if (!loadSchema(options, schema)) return 2;
  const std::string path = options.positional[0];
  std::string text;
  if (!loadFile(path, &text)) return 2;
  mfc::DiagnosticSink sink;
  const CompiledContent compiled = compileContentText(path, text, schema, sink);
  if (!sink.all().empty()) std::printf("%s", sink.formatText().c_str());
  if (!compiled.ok) return 1;
  std::printf("%s", compiled.dsl.c_str());
  return 0;
}

int cmdAgent(const Options& options) {
  if (options.positional.size() < 2 || options.positional[0] != "fix") {
    std::fprintf(stderr, "agent requires: mf agent fix <graph-file> [--write] [--json]\n");
    return 2;
  }
  mfc::json::Value schema;
  if (!loadSchema(options, schema)) return 2;
  const std::string path = options.positional[1];
  std::string text;
  if (!loadFile(path, &text)) return 2;

  mfc::DiagnosticSink sink;
  mfagent::FixReport report = mfagent::fixGraphText(text, path, schema, sink);
  bool written = false;
  if (options.write && report.changed) {
    std::string writeError;
    if (!mfc::writeFile(path, report.text, &writeError)) {
      std::fprintf(stderr, "%s\n", writeError.c_str());
      return 2;
    }
    written = true;
  }
  report.report["written"] = mfc::json::Value::boolean(written);

  if (options.jsonOutput) {
    std::printf("%s\n", mfc::json::dump(report.report).c_str());
  } else {
    std::printf("agent: %d fix(es), %d remaining error(s)%s\n", report.fixedCount,
                report.remainingErrors, written ? ", file updated" : "");
    const mfc::json::Value* fixed = report.report.find("fixed");
    if (fixed != nullptr) {
      for (const auto& item : fixed->items()) {
        std::printf("  fixed: %s\n", item.asString().c_str());
      }
    }
    for (const auto& diagnostic : sink.all()) {
      if (diagnostic.severity != mfc::Severity::Error) continue;
      std::printf("  left:  %s %s\n", diagnostic.code.c_str(), diagnostic.message.c_str());
    }
  }
  return report.remainingErrors == 0 ? 0 : 1;
}

int cmdGenContent(const Options& options) {
  const fs::path outDir = options.outDir.empty() ? fs::path("content_gen") : fs::path(options.outDir);
  std::error_code ec;
  fs::create_directories(outDir / "quests", ec);
  const char* kinds[] = {"quest.condition.kill", "quest.condition.collect",
                         "quest.condition.reach", "quest.condition.talk", "quest.log",
                         "quest.reward", "quest.timer"};
  for (int i = 0; i < options.files; ++i) {
    mfc::json::Value root = mfc::json::Value::object();
    root["schemaVersion"] = mfc::json::Value::integer(1);
    root["kind"] = mfc::json::Value::string("quest");
    char idBuffer[64];
    std::snprintf(idBuffer, sizeof(idBuffer), "q_gen_%05d", i);
    const std::string questId = idBuffer;
    root["id"] = mfc::json::Value::string(questId);
    root["title"] = mfc::json::Value::string("Generated " + questId);
    mfc::json::Value nodes = mfc::json::Value::array();
    mfc::json::Value edges = mfc::json::Value::array();
    auto addNode = [&](const std::string& id, const std::string& type,
                       mfc::json::Value props) {
      mfc::json::Value node = mfc::json::Value::object();
      node["id"] = mfc::json::Value::string(id);
      node["type"] = mfc::json::Value::string(type);
      node["props"] = std::move(props);
      nodes.push(std::move(node));
    };
    auto addEdge = [&](const std::string& from, const std::string& to) {
      mfc::json::Value edge = mfc::json::Value::object();
      edge["from"] = mfc::json::Value::string(from);
      edge["to"] = mfc::json::Value::string(to);
      edges.push(std::move(edge));
    };
    addNode("n_start", "quest.start", mfc::json::Value::object());
    int previous = 0;
    for (int n = 1; n < options.nodesPerFile - 1; ++n) {
      const std::string nodeId = "n_" + std::to_string(n);
      const std::string type = kinds[(i + n) % 7];
      mfc::json::Value props = mfc::json::Value::object();
      if (type == "quest.condition.kill") {
        props["target"] = mfc::json::Value::string("Wolf");
        props["count"] = mfc::json::Value::integer(1);
      } else if (type == "quest.condition.collect") {
        props["item"] = mfc::json::Value::string("Wool");
        props["count"] = mfc::json::Value::integer(1);
      } else if (type == "quest.condition.reach") {
        props["area"] = mfc::json::Value::string("Hilltop");
      } else if (type == "quest.condition.talk") {
        props["npc"] = mfc::json::Value::string("Elder");
      } else if (type == "quest.log") {
        props["text"] = mfc::json::Value::string("log " + std::to_string(n));
      } else if (type == "quest.reward") {
        props["exp"] = mfc::json::Value::integer(10);
        props["item"] = mfc::json::Value::string("Token");
      } else {
        props["seconds"] = mfc::json::Value::number(0.1);
      }
      addNode(nodeId, type, std::move(props));
      addEdge(previous == 0 ? "n_start" : "n_" + std::to_string(previous), nodeId);
      previous = n;
    }
    mfc::json::Value endProps = mfc::json::Value::object();
    endProps["result"] = mfc::json::Value::string("success");
    addNode("n_end", "quest.end", std::move(endProps));
    addEdge(previous == 0 ? "n_start" : "n_" + std::to_string(previous), "n_end");
    root["nodes"] = std::move(nodes);
    root["edges"] = std::move(edges);
    const fs::path path = outDir / "quests" / (questId + ".json");
    std::string error;
    if (!mfc::writeFile(path.string(), mfc::json::dump(root) + "\n", &error)) {
      std::fprintf(stderr, "%s\n", error.c_str());
      return 2;
    }
  }
  std::printf("gen-content: %d file(s) -> %s\n", options.files, outDir.string().c_str());
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  const Options options = parseOptions(argc, argv);
  if (options.command.empty() || options.command == "help" || options.command == "--help") {
    printUsage();
    return options.command.empty() ? 2 : 0;
  }
  if (options.command == "validate") return cmdValidate(options);
  if (options.command == "build") return cmdBuild(options);
  if (options.command == "test") return cmdTest(options);
  if (options.command == "stats") return cmdStats(options);
  if (options.command == "dsl") return cmdDsl(options);
  if (options.command == "check") return cmdCheck(options);
  if (options.command == "sim") return cmdSim(options);
  if (options.command == "agent") return cmdAgent(options);
  if (options.command == "gen-content") return cmdGenContent(options);
  std::fprintf(stderr, "unknown command '%s'\n", options.command.c_str());
  printUsage();
  return 2;
}
