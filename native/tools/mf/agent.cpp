#include "agent.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "mfc/graph.h"
#include "mfc/sema.h"

namespace mfagent {

namespace {

using mfc::json::Value;

const Value* findNodeType(const Value& schema, const std::string& kind, const std::string& type) {
  const Value* kinds = schema.find("kinds");
  if (!kinds) return nullptr;
  const Value* kindValue = kinds->find(kind);
  if (!kindValue) return nullptr;
  const Value* nodeTypes = kindValue->find("nodeTypes");
  if (!nodeTypes) return nullptr;
  return nodeTypes->find(type);
}

std::string propType(const Value& spec) {
  std::string type;
  spec.getString("type", &type);
  return type;
}

std::string stringOf(const Value& object, const std::string& key) {
  std::string value;
  object.getString(key, &value);
  return value;
}

int64_t intOf(const Value& object, const std::string& key, int64_t fallback) {
  int64_t value = fallback;
  object.getInt(key, &value);
  return value;
}

bool needsTwoOptions(const std::string& options) {
  int count = 0;
  bool any = false;
  for (char c : options) {
    if (c == '|') {
      if (any) ++count;
      any = false;
    } else if (c != ' ') {
      any = true;
    }
  }
  if (any) ++count;
  return count < 2;
}

Value defaultProp(const std::string& nodeType, const std::string& prop, const std::string& type) {
  if (type == "int") return Value::integer(0);
  if (type == "float") return Value::number(0.0);
  if (type == "bool") return Value::boolean(false);
  // Small semantic defaults for required identity fields. Values that encode
  // design intent (timeline asset, flag names, dialogue text) are left empty
  // so the remaining error stays visible.
  if (nodeType == "dialogue.line" && prop == "speaker") return Value::string("Speaker");
  if (nodeType == "dialogue.line" && prop == "text") return Value::string("...");
  if (nodeType == "quest.condition.kill" && prop == "target") return Value::string("Target");
  if (nodeType == "quest.condition.collect" && prop == "item") return Value::string("Item");
  if (nodeType == "quest.condition.reach" && prop == "area") return Value::string("Area");
  if (nodeType == "quest.condition.talk" && prop == "npc") return Value::string("NPC");
  if (nodeType == "quest.log" && prop == "text") return Value::string("Log");
  if (nodeType == "quest.depends" && prop == "questId") return Value::string("q_other");
  if (nodeType == "dialogue.choice" && prop == "options") return Value::string("选项 A|选项 B");
  if (nodeType == "dialogue.flag" && prop == "flag") return Value::string("flag");
  if (nodeType == "skill.frame" && prop == "event") return Value::string("damage");
  return Value::string("");
}

bool isKnownEvent(const std::string& event) {
  return event == "damage" || event == "buff" || event == "vfx" || event == "sfx" ||
         event == "move";
}

struct Fixer {
  const Value& schema;
  std::vector<std::string> fixed;
  bool changed = false;

  void record(const std::string& message) {
    fixed.push_back(message);
    changed = true;
  }

  void fixProps(const std::string& nodeId, const std::string& nodeType, Value& node,
                const Value* typeSchema) {
    if (typeSchema == nullptr) return;
    const Value* propsSchema = typeSchema->find("props");
    if (propsSchema == nullptr || !propsSchema->isObject()) return;
    Value& props = node["props"];
    if (!props.isObject()) {
      props = Value::object();
      record("node " + nodeId + ": created props object");
    }
    for (const auto& spec : propsSchema->members()) {
      const Value* value = props.find(spec.first);
      bool required = true;
      spec.second.getBool("required", &required);
      if (value == nullptr) {
        if (!required) continue;
        if (const Value* def = spec.second.find("default")) {
          props[spec.first] = *def;
        } else {
          props[spec.first] = defaultProp(nodeType, spec.first, propType(spec.second));
        }
        record("node " + nodeId + ": filled required prop '" + spec.first + "'");
        continue;
      }
      const std::string type = propType(spec.second);
      bool badType = false;
      if (type == "string") badType = !value->isString();
      else if (type == "int") badType = !value->isNumber();
      else if (type == "float") badType = !value->isNumber();
      else if (type == "bool") badType = !value->isBool();
      if (badType) {
        props[spec.first] = defaultProp(nodeType, spec.first, type);
        record("node " + nodeId + ": coerce prop '" + spec.first + "' to " + type);
      }
    }
  }

  void fixValues(const std::string& nodeId, const std::string& nodeType, Value& node) {
    Value& props = node["props"];
    if (nodeType == "quest.condition.kill" || nodeType == "quest.condition.collect") {
      if (intOf(props, "count", 1) < 1) {
        props["count"] = Value::integer(1);
        record("node " + nodeId + ": count clamped to 1");
      }
    }
    if (nodeType == "quest.timer" || nodeType == "dialogue.wait") {
      double seconds = 0.0;
      if (props.getDouble("seconds", &seconds) && seconds < 0.0) {
        props["seconds"] = Value::number(0.0);
        record("node " + nodeId + ": negative seconds clamped to 0");
      }
    }
    if (nodeType == "quest.end") {
      const std::string result = stringOf(props, "result");
      if (!result.empty() && result != "success" && result != "failure") {
        props["result"] = Value::string("success");
        record("node " + nodeId + ": unknown result coerced to success");
      }
    }
    if (nodeType == "dialogue.line") {
      if (stringOf(props, "speaker").empty()) {
        props["speaker"] = Value::string("Speaker");
        record("node " + nodeId + ": empty speaker filled");
      }
      if (stringOf(props, "text").empty()) {
        props["text"] = Value::string("...");
        record("node " + nodeId + ": empty text filled");
      }
    }
    if (nodeType == "dialogue.choice" && needsTwoOptions(stringOf(props, "options"))) {
      props["options"] = Value::string("选项 A|选项 B");
      record("node " + nodeId + ": choice options reset to two defaults");
    }
    if (nodeType == "dialogue.flag" && stringOf(props, "flag").empty()) {
      props["flag"] = Value::string("flag");
      record("node " + nodeId + ": empty flag filled");
    }
    if (nodeType == "skill.frame") {
      if (intOf(props, "timeMs", 0) < 0) {
        props["timeMs"] = Value::integer(0);
        record("node " + nodeId + ": negative frame time clamped to 0");
      }
      const std::string event = stringOf(props, "event");
      if (!isKnownEvent(event)) {
        props["event"] = Value::string("damage");
        record("node " + nodeId + ": unknown skill event coerced to damage");
      }
    }
  }
};

std::string uniqueId(const Value& nodes, const std::string& prefix) {
  for (int i = 1; i < 100000; ++i) {
    const std::string candidate = prefix + std::to_string(i);
    bool used = false;
    for (const auto& node : nodes.items()) {
      if (stringOf(node, "id") == candidate) {
        used = true;
        break;
      }
    }
    if (!used) return candidate;
  }
  return prefix + "_overflow";
}

Value makeNode(const std::string& id, const std::string& type, double x, double y) {
  Value node = Value::object();
  node["id"] = Value::string(id);
  node["type"] = Value::string(type);
  node["props"] = Value::object();
  Value editor = Value::object();
  editor["x"] = Value::number(x);
  editor["y"] = Value::number(y);
  node["editor"] = std::move(editor);
  return node;
}

Value makeEdge(const std::string& from, const std::string& to) {
  Value edge = Value::object();
  edge["from"] = Value::string(from);
  edge["to"] = Value::string(to);
  return edge;
}

void addEdge(Value& edges, const std::string& from, const std::string& to) {
  for (const auto& edge : edges.items()) {
    if (stringOf(edge, "from") == from && stringOf(edge, "to") == to) return;
  }
  edges.push(makeEdge(from, to));
}

int degree(const Value& edges, const std::string& id, bool outgoing) {
  int count = 0;
  for (const auto& edge : edges.items()) {
    if (stringOf(edge, outgoing ? "from" : "to") == id) ++count;
  }
  return count;
}

void sortSkillFrames(Value& nodes, Value& edges, const std::string& entryId,
                     const std::string& exitId, Fixer& fixer) {
  std::vector<std::pair<int64_t, std::string>> frames;
  for (const auto& node : nodes.items()) {
    if (stringOf(node, "type") != "skill.frame") continue;
    const Value* props = node.find("props");
    frames.emplace_back(props != nullptr ? intOf(*props, "timeMs", 0) : 0, stringOf(node, "id"));
  }
  std::sort(frames.begin(), frames.end(), [](const auto& a, const auto& b) {
    if (a.first != b.first) return a.first < b.first;
    return a.second < b.second;
  });
  edges = Value::array();
  std::string previous = entryId;
  for (const auto& frame : frames) {
    addEdge(edges, previous, frame.second);
    previous = frame.second;
  }
  addEdge(edges, previous, exitId);
  fixer.record("skill: rebuilt frame chain in time order");
}

}  // namespace

FixReport fixGraphText(const std::string& text, const std::string& path, const Value& schema,
                       mfc::DiagnosticSink& sink) {
  FixReport out;
  Value root;
  std::string error;
  if (!mfc::json::parse(text, root, &error)) {
    out.report["ok"] = Value::boolean(false);
    out.report["error"] = Value::string("invalid JSON: " + error);
    return out;
  }
  if (!root.isObject()) {
    out.report["ok"] = Value::boolean(false);
    out.report["error"] = Value::string("graph root must be an object");
    return out;
  }
  std::string kind = "quest";
  root.getString("kind", &kind);
  if (kind != "quest" && kind != "dialogue" && kind != "skill") kind = "quest";
  root["kind"] = Value::string(kind);
  if (root.find("schemaVersion") == nullptr) root["schemaVersion"] = Value::integer(1);
  if (root.find("id") == nullptr) root["id"] = Value::string("q_repaired");
  if (root.find("nodes") == nullptr || !root["nodes"].isArray()) root["nodes"] = Value::array();
  if (root.find("edges") == nullptr || !root["edges"].isArray()) root["edges"] = Value::array();

  Fixer fixer{schema, std::vector<std::string>{}, false};
  Value& nodes = root["nodes"];
  Value fixedNodes = Value::array();
  for (const auto& node : nodes.items()) {
    Value copy = node;
    const std::string type = stringOf(copy, "type");
    const std::string id = stringOf(copy, "id");
    const Value* typeSchema = findNodeType(schema, kind, type);
    if (typeSchema != nullptr) {
      fixer.fixProps(id, type, copy, typeSchema);
      fixer.fixValues(id, type, copy);
    }
    fixedNodes.push(std::move(copy));
  }
  nodes = std::move(fixedNodes);

  const std::string entryType = kind == "skill" ? "skill.cast" : kind + ".start";
  const std::string exitType = kind + ".end";
  Value& edges = root["edges"];
  std::string entryId;
  std::string exitId;
  int entryCount = 0;
  int exitCount = 0;
  for (const auto& node : nodes.items()) {
    const std::string type = stringOf(node, "type");
    if (type == entryType) {
      ++entryCount;
      if (entryId.empty()) entryId = stringOf(node, "id");
    } else if (type == exitType) {
      ++exitCount;
      if (exitId.empty()) exitId = stringOf(node, "id");
    }
  }

  if (entryCount == 0) {
    entryId = uniqueId(nodes, "n_start");
    nodes.push(makeNode(entryId, entryType, 60.0, 180.0));
    std::string target;
    for (const auto& node : nodes.items()) {
      const std::string id = stringOf(node, "id");
      const std::string type = stringOf(node, "type");
      if (id == entryId || type == exitType) continue;
      if (degree(edges, id, false) == 0) {
        target = id;
        break;
      }
    }
    if (target.empty()) {
      for (const auto& node : nodes.items()) {
        const std::string id = stringOf(node, "id");
        if (id != entryId && stringOf(node, "type") != exitType) {
          target = id;
          break;
        }
      }
    }
    if (!target.empty()) addEdge(edges, entryId, target);
    fixer.record("graph: added missing " + entryType);
  }
  if (exitCount == 0) {
    exitId = uniqueId(nodes, "n_end");
    nodes.push(makeNode(exitId, exitType, 900.0, 180.0));
    std::string source;
    for (const auto& node : nodes.items()) {
      const std::string id = stringOf(node, "id");
      const std::string type = stringOf(node, "type");
      if (id == exitId || type == entryType) continue;
      if (degree(edges, id, true) == 0) {
        source = id;
        break;
      }
    }
    if (source.empty()) source = entryId;
    if (!source.empty()) addEdge(edges, source, exitId);
    fixer.record("graph: added missing " + exitType);
  }

  if (kind == "skill" && !entryId.empty() && !exitId.empty()) {
    sortSkillFrames(nodes, edges, entryId, exitId, fixer);
  }

  out.changed = fixer.changed;
  out.fixedCount = static_cast<int>(fixer.fixed.size());
  out.text = mfc::json::dump(root) + "\n";

  // Re-validate the repaired document (including lowering/sema) so the report
  // reflects what `mf check` would say.
  mfc::Graph graph;
  mfc::DiagnosticSink parseSink;
  if (!mfc::parseGraph(out.text, path, graph, parseSink)) {
    for (const auto& diagnostic : parseSink.all()) sink.add(diagnostic);
  } else {
    mfc::validateGraph(graph, schema, sink);
    if (!sink.hasErrors()) {
      mfc::Program program = mfc::lowerGraph(graph, sink);
      if (!sink.hasErrors()) {
        mfc::Sema sema;
        sema.analyze(program, sink, path);
      }
    }
  }
  out.remainingErrors = sink.errorCount();

  Value fixed = Value::array();
  for (const auto& item : fixer.fixed) fixed.push(Value::string(item));
  Value remaining = Value::array();
  for (const auto& diagnostic : sink.all()) {
    if (diagnostic.severity != mfc::Severity::Error) continue;
    Value item = Value::object();
    item["code"] = Value::string(diagnostic.code);
    item["message"] = Value::string(diagnostic.message);
    item["node"] = Value::string(diagnostic.nodeId);
    remaining.push(std::move(item));
  }
  out.report["ok"] = Value::boolean(out.remainingErrors == 0);
  out.report["changed"] = Value::boolean(out.changed);
  out.report["fixed"] = std::move(fixed);
  out.report["remaining"] = std::move(remaining);
  return out;
}

}  // namespace mfagent
