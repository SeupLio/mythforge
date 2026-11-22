#include "mfc/graph.h"

#include <algorithm>
#include <map>
#include <memory>
#include <queue>
#include <set>
#include <utility>

namespace mfc {

namespace {

bool validQuestId(const std::string& id) {
  if (id.empty()) return false;
  for (char c : id) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == '_';
    if (!ok) return false;
  }
  return true;
}

std::string varNameFor(const std::string& nodeId) {
  std::string out;
  for (char c : nodeId) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
      out += c;
    } else {
      out += '_';
    }
  }
  if (out.empty() || (out[0] >= '0' && out[0] <= '9')) out = "n_" + out;
  return out;
}

const json::Value* findNodeType(const json::Value& schema, const std::string& kind,
                                const std::string& type) {
  const json::Value* kinds = schema.find("kinds");
  if (!kinds) return nullptr;
  const json::Value* kindValue = kinds->find(kind);
  if (!kindValue) return nullptr;
  const json::Value* nodeTypes = kindValue->find("nodeTypes");
  if (!nodeTypes) return nullptr;
  return nodeTypes->find(type);
}

std::string propType(const json::Value& spec) {
  std::string type;
  spec.getString("type", &type);
  return type;
}

bool propMatches(const json::Value& value, const std::string& type) {
  if (type == "string") return value.isString();
  if (type == "int") return value.isNumber() && value.asInt() == value.asDouble();
  if (type == "float") return value.isNumber();
  if (type == "bool") return value.isBool();
  return true;
}

bool boolProp(const GraphNode& node, const std::string& name, bool fallback) {
  const json::Value* value = node.props.find(name);
  if (!value || !value->isBool()) return fallback;
  return value->asBool();
}

bool doubleProp(const GraphNode& node, const std::string& name, double fallback, double* out) {
  const json::Value* value = node.props.find(name);
  if (!value || !value->isNumber()) {
    *out = fallback;
    return false;
  }
  *out = value->asDouble();
  return true;
}

bool isKnownKind(const std::string& kind) {
  return kind == "quest" || kind == "dialogue" || kind == "skill";
}

std::vector<std::string> splitOptions(const std::string& text) {
  std::vector<std::string> options;
  std::string current;
  for (char c : text) {
    if (c == '|') {
      options.push_back(current);
      current.clear();
    } else {
      current += c;
    }
  }
  options.push_back(current);
  return options;
}

int64_t intProp(const GraphNode& node, const std::string& name, int64_t fallback) {
  const json::Value* value = node.props.find(name);
  if (!value || !value->isNumber()) return fallback;
  return value->asInt();
}

std::string stringProp(const GraphNode& node, const std::string& name,
                       const std::string& fallback = "") {
  const json::Value* value = node.props.find(name);
  if (!value || !value->isString()) return fallback;
  return value->asString();
}

ExprPtr intLit(int64_t value, const Loc& loc) {
  auto expr = std::make_unique<Expr>();
  expr->kind = ExprKind::IntLit;
  expr->loc = loc;
  expr->intValue = value;
  return expr;
}

ExprPtr floatLit(double value, const Loc& loc) {
  auto expr = std::make_unique<Expr>();
  expr->kind = ExprKind::FloatLit;
  expr->loc = loc;
  expr->floatValue = value;
  return expr;
}

ExprPtr strLit(const std::string& value, const Loc& loc) {
  auto expr = std::make_unique<Expr>();
  expr->kind = ExprKind::StringLit;
  expr->loc = loc;
  expr->text = value;
  return expr;
}

ExprPtr ident(const std::string& name, const Loc& loc) {
  auto expr = std::make_unique<Expr>();
  expr->kind = ExprKind::Ident;
  expr->loc = loc;
  expr->text = name;
  return expr;
}

ExprPtr binary(const std::string& op, ExprPtr lhs, ExprPtr rhs, const Loc& loc) {
  auto expr = std::make_unique<Expr>();
  expr->kind = ExprKind::Binary;
  expr->loc = loc;
  expr->op = op;
  expr->lhs = std::move(lhs);
  expr->rhs = std::move(rhs);
  return expr;
}

StmtPtr makeStmt(StmtKind kind, const Loc& loc, const std::string& nodeId) {
  auto stmt = std::make_unique<Stmt>();
  stmt->kind = kind;
  stmt->loc = loc;
  stmt->nodeId = nodeId;
  return stmt;
}

StmtPtr makeBlock(const Loc& loc) {
  auto stmt = std::make_unique<Stmt>();
  stmt->kind = StmtKind::Block;
  stmt->loc = loc;
  return stmt;
}

// Builds a vector of expression arguments from move-only ExprPtr values.
template <typename... Args>
std::vector<ExprPtr> makeArgs(Args&&... args) {
  std::vector<ExprPtr> out;
  (out.push_back(std::forward<Args>(args)), ...);
  return out;
}

StmtPtr makeEmit(const std::string& event, std::vector<ExprPtr> args, const Loc& loc,
                 const std::string& nodeId) {
  auto stmt = makeStmt(StmtKind::Emit, loc, nodeId);
  stmt->callee = event;
  stmt->args = std::move(args);
  return stmt;
}

StmtPtr makeCall(const std::string& fn, std::vector<ExprPtr> args, const Loc& loc,
                 const std::string& nodeId) {
  auto stmt = makeStmt(StmtKind::Call, loc, nodeId);
  stmt->callee = fn;
  stmt->args = std::move(args);
  return stmt;
}

StmtPtr makeWaitEvent(const std::string& event, const std::string& field,
                      const std::string& value, const Loc& loc, const std::string& nodeId) {
  auto stmt = makeStmt(StmtKind::WaitEvent, loc, nodeId);
  stmt->eventName = event;
  if (!field.empty()) {
    stmt->filter = binary("==", ident(field, loc), strLit(value, loc), loc);
  }
  return stmt;
}

StmtPtr makeWaitTime(double seconds, const Loc& loc, const std::string& nodeId) {
  auto stmt = makeStmt(StmtKind::WaitTime, loc, nodeId);
  stmt->expr = floatLit(seconds, loc);
  return stmt;
}

// Emits:
//   let counter = 0;
//   while (counter < count) { wait event(E, field == value); counter = counter + 1; }
StmtPtr makeCountedWait(const std::string& event, const std::string& field,
                        const std::string& value, int64_t count, const std::string& nodeId,
                        const Loc& loc) {
  const std::string variable = varNameFor(nodeId) + "_count";
  auto declar = makeStmt(StmtKind::Let, loc, nodeId);
  declar->name = variable;
  declar->expr = intLit(0, loc);

  auto loop = makeStmt(StmtKind::While, loc, nodeId);
  loop->expr = binary("<", ident(variable, loc), intLit(count, loc), loc);

  auto body = makeBlock(loc);
  body->body.push_back(makeWaitEvent(event, field, value, loc, nodeId));
  auto increment = makeStmt(StmtKind::Assign, loc, nodeId);
  increment->name = variable;
  increment->expr = binary("+", ident(variable, loc), intLit(1, loc), loc);
  body->body.push_back(std::move(increment));
  loop->body.push_back(std::move(body));

  auto wrapper = makeBlock(loc);
  wrapper->body.push_back(std::move(declar));
  wrapper->body.push_back(std::move(loop));
  return wrapper;
}

}  // namespace

bool parseGraph(const std::string& text, const std::string& file, Graph& out,
                DiagnosticSink& sink) {
  json::Value root;
  std::string error;
  if (!json::parse(text, root, &error)) {
    sink.error("MFC5000", {}, "invalid JSON: " + error, {}, {}, file);
    return false;
  }
  if (!root.isObject()) {
    sink.error("MFC5001", {}, "graph root must be an object", {}, {}, file);
    return false;
  }
  out = Graph();
  out.file = file;
  int64_t version = 1;
  if (root.getInt("schemaVersion", &version)) out.schemaVersion = static_cast<int>(version);
  root.getString("kind", &out.kind);
  root.getString("id", &out.id);
  root.getString("title", &out.title);
  if (const json::Value* meta = root.find("meta")) out.meta = *meta;

  const json::Value* nodes = root.find("nodes");
  if (!nodes || !nodes->isArray()) {
    sink.error("MFC5002", {}, "graph must have a 'nodes' array", {}, {}, file);
    return false;
  }
  for (const auto& nodeValue : nodes->items()) {
    if (!nodeValue.isObject()) {
      sink.error("MFC5003", {}, "each node must be an object", {}, {}, file);
      continue;
    }
    GraphNode node;
    nodeValue.getString("id", &node.id);
    nodeValue.getString("type", &node.type);
    if (const json::Value* props = nodeValue.find("props")) node.props = *props;
    if (node.id.empty()) {
      sink.error("MFC5004", {}, "node is missing 'id'", {}, {}, file);
      continue;
    }
    if (node.type.empty()) {
      sink.error("MFC5005", {}, "node '" + node.id + "' is missing 'type'", node.id, {}, file);
      continue;
    }
    out.nodes.push_back(std::move(node));
  }

  const json::Value* edges = root.find("edges");
  if (edges) {
    if (!edges->isArray()) {
      sink.error("MFC5006", {}, "'edges' must be an array", {}, {}, file);
      return false;
    }
    for (const auto& edgeValue : edges->items()) {
      if (!edgeValue.isObject()) {
        sink.error("MFC5007", {}, "each edge must be an object", {}, {}, file);
        continue;
      }
      GraphEdge edge;
      edgeValue.getString("from", &edge.from);
      edgeValue.getString("to", &edge.to);
      if (edge.from.empty() || edge.to.empty()) {
        sink.error("MFC5008", {}, "edge is missing 'from' or 'to'", {}, {}, file);
        continue;
      }
      out.edges.push_back(std::move(edge));
    }
  }
  return !sink.hasErrors();
}

bool loadNodeSchema(const std::string& text, json::Value& out, DiagnosticSink& sink) {
  std::string error;
  if (!json::parse(text, out, &error)) {
    sink.error("MFC5009", {}, "invalid node schema JSON: " + error, {}, {}, "<schema>");
    return false;
  }
  const json::Value* kinds = out.find("kinds");
  if (!kinds || !kinds->isObject()) {
    sink.error("MFC5010", {}, "schema must contain a 'kinds' object", {}, {}, "<schema>");
    return false;
  }
  return true;
}

bool validateGraph(const Graph& graph, const json::Value& schema, DiagnosticSink& sink) {
  const std::string& file = graph.file;
  if (graph.schemaVersion != 1) {
    sink.error("MFC5011", {},
               "unsupported schemaVersion " + std::to_string(graph.schemaVersion), {}, 
               "this build supports version 1", file);
    return false;
  }
  if (!isKnownKind(graph.kind)) {
    sink.error("MFC5012", {}, "unsupported graph kind '" + graph.kind + "'", {},
               "supported kinds: quest, dialogue, skill", file);
    return false;
  }
  if (!validQuestId(graph.id)) {
    sink.error("MFC5013", {},
               "invalid content id '" + graph.id + "'", {},
               "use letters, digits and '_'", file);
    return false;
  }

  const bool isQuest = graph.kind == "quest";
  const bool isDialogue = graph.kind == "dialogue";
  const bool isSkill = graph.kind == "skill";
  const std::string entryType = isSkill ? "skill.cast" : graph.kind + ".start";
  const std::string exitType = graph.kind + ".end";

  std::map<std::string, const GraphNode*> nodes;
  for (const auto& node : graph.nodes) {
    if (nodes.count(node.id) != 0) {
      sink.error("MFC5014", {}, "duplicate node id '" + node.id + "'", node.id, {}, file);
      continue;
    }
    nodes[node.id] = &node;
  }

  std::map<std::string, int> inDegree;
  std::map<std::string, int> outDegree;
  std::map<std::string, std::vector<std::string>> outEdges;
  std::map<std::string, std::vector<std::string>> inEdges;
  std::set<std::pair<std::string, std::string>> seenEdges;
  for (const auto& edge : graph.edges) {
    if (nodes.count(edge.from) == 0) {
      sink.error("MFC5015", {}, "edge references unknown node '" + edge.from + "'", edge.from,
                 {}, file);
      continue;
    }
    if (nodes.count(edge.to) == 0) {
      sink.error("MFC5016", {}, "edge references unknown node '" + edge.to + "'", edge.to, {},
                 file);
      continue;
    }
    if (edge.from == edge.to) {
      sink.error("MFC5017", {}, "self-loop on node '" + edge.from + "'", edge.from, {}, file);
      continue;
    }
    if (!seenEdges.insert({edge.from, edge.to}).second) {
      sink.warning("MFC5100", {}, "duplicate edge " + edge.from + " -> " + edge.to, edge.from,
                   {}, file);
      continue;
    }
    outEdges[edge.from].push_back(edge.to);
    inEdges[edge.to].push_back(edge.from);
    ++inDegree[edge.to];
    ++outDegree[edge.from];
  }

  std::vector<const GraphNode*> starts;
  std::vector<const GraphNode*> ends;
  for (const auto& node : graph.nodes) {
    const json::Value* typeSchema = findNodeType(schema, graph.kind, node.type);
    if (typeSchema == nullptr) {
      sink.error("MFC5018", {}, "unknown node type '" + node.type + "'", node.id,
                 "add it to schema/node_types.json", file);
      continue;
    }
    const int in = inDegree[node.id];
    const int out = outDegree[node.id];
    int64_t minIn = 0;
    int64_t maxIn = 64;
    int64_t minOut = 0;
    int64_t maxOut = 64;
    typeSchema->getInt("minInputs", &minIn);
    typeSchema->getInt("maxInputs", &maxIn);
    typeSchema->getInt("minOutputs", &minOut);
    typeSchema->getInt("maxOutputs", &maxOut);
    if (in < minIn || in > maxIn) {
      sink.error("MFC5019", {},
                 "node '" + node.id + "' (" + node.type + ") has " + std::to_string(in) +
                     " input edge(s), expected " + std::to_string(minIn) + ".." +
                     std::to_string(maxIn),
                 node.id, {}, file);
    }
    if (out < minOut || out > maxOut) {
      sink.error("MFC5020", {},
                 "node '" + node.id + "' (" + node.type + ") has " + std::to_string(out) +
                     " output edge(s), expected " + std::to_string(minOut) + ".." +
                     std::to_string(maxOut),
                 node.id, {}, file);
    }

    const json::Value* propsSchema = typeSchema->find("props");
    if (propsSchema != nullptr && propsSchema->isObject()) {
      for (const auto& spec : propsSchema->members()) {
        const json::Value* value = node.props.find(spec.first);
        bool required = true;
        spec.second.getBool("required", &required);
        if (value == nullptr) {
          if (required) {
            sink.error("MFC5021", {},
                       "node '" + node.id + "' is missing required prop '" + spec.first + "'",
                       node.id, {}, file);
          }
          continue;
        }
        const std::string expected = propType(spec.second);
        if (!expected.empty() && !propMatches(*value, expected)) {
          sink.error("MFC5022", {},
                     "prop '" + spec.first + "' of node '" + node.id + "' must be " + expected,
                     node.id, {}, file);
        }
      }
      for (const auto& prop : node.props.members()) {
        if (propsSchema->find(prop.first) == nullptr) {
          sink.warning("MFC5101", {},
                       "unknown prop '" + prop.first + "' on node '" + node.id + "'", node.id,
                       "check for a typo; it will be ignored", file);
        }
      }
    }

    if (node.type == entryType) {
      starts.push_back(&node);
    } else if (node.type == exitType) {
      ends.push_back(&node);
    }
    if (isQuest) {
      if (node.type == "quest.condition.kill" || node.type == "quest.condition.collect") {
        if (intProp(node, "count", 1) <= 0) {
          sink.error("MFC5023", {}, "count must be positive on node '" + node.id + "'", node.id,
                     {}, file);
        }
      }
      if (node.type == "quest.timer") {
        double seconds = 1.0;
        doubleProp(node, "seconds", 1.0, &seconds);
        if (seconds < 0.0) {
          sink.error("MFC5024", {}, "timer seconds must be >= 0 on node '" + node.id + "'",
                     node.id, {}, file);
        }
      }
      if (node.type == "quest.end") {
        const std::string result = stringProp(node, "result", "success");
        if (result != "success" && result != "failure") {
          sink.warning("MFC5102", {},
                       "unknown quest result '" + result + "', expected success|failure",
                       node.id, {}, file);
        }
      }
    } else if (isDialogue) {
      if (node.type == "dialogue.line") {
        if (stringProp(node, "speaker").empty()) {
          sink.error("MFC5034", {}, "dialogue line '" + node.id + "' has an empty speaker",
                     node.id, "set the speaker prop", file);
        }
        if (stringProp(node, "text").empty()) {
          sink.error("MFC5034", {}, "dialogue line '" + node.id + "' has empty text", node.id,
                     "set the text or l10nKey prop", file);
        }
      } else if (node.type == "dialogue.choice") {
        const std::vector<std::string> options = splitOptions(stringProp(node, "options"));
        int nonEmpty = 0;
        for (const auto& option : options) {
          if (!option.empty()) ++nonEmpty;
        }
        if (nonEmpty < 2) {
          sink.error("MFC5035", {},
                     "choice node '" + node.id +
                         "' needs at least two options separated by '|'",
                     node.id, {}, file);
        }
      } else if (node.type == "dialogue.flag") {
        if (stringProp(node, "flag").empty()) {
          sink.error("MFC5036", {}, "flag node '" + node.id + "' has an empty flag", node.id,
                     {}, file);
        }
      } else if (node.type == "dialogue.timeline") {
        if (stringProp(node, "asset").empty()) {
          sink.error("MFC5037", {}, "timeline node '" + node.id + "' has no asset", node.id,
                     {}, file);
        }
      } else if (node.type == "dialogue.wait") {
        double seconds = 1.0;
        doubleProp(node, "seconds", 1.0, &seconds);
        if (seconds < 0.0) {
          sink.error("MFC5024", {}, "wait seconds must be >= 0 on node '" + node.id + "'",
                     node.id, {}, file);
        }
      }
    } else if (isSkill) {
      if (node.type == "skill.frame") {
        if (intProp(node, "timeMs", 0) < 0) {
          sink.error("MFC5038", {}, "frame timeMs must be >= 0 on node '" + node.id + "'",
                     node.id, {}, file);
        }
        const std::string event = stringProp(node, "event");
        if (event != "damage" && event != "buff" && event != "vfx" && event != "sfx" &&
            event != "move") {
          sink.warning("MFC5103", {},
                       "unknown skill event '" + event +
                           "', expected damage|buff|vfx|sfx|move",
                       node.id, {}, file);
        }
      }
    }
    if (outDegree[node.id] > 1) {
      sink.error("MFC5025", {},
                 "node '" + node.id + "' has multiple outgoing edges; branching lowering "
                 "arrives with parallel/race support (v1 is a linear chain)",
                 node.id, {}, file);
    }
  }

  if (starts.empty()) {
    sink.error("MFC5026", {}, "graph has no '" + entryType + "' node", {}, {}, file);
    return false;
  }
  if (starts.size() > 1) {
    sink.error("MFC5027", {},
               "graph has more than one '" + entryType + "' node", {}, {}, file);
  }
  if (ends.empty()) {
    sink.error("MFC5028", {}, "graph has no '" + exitType + "' node", {}, {}, file);
  }

  // Reachability from start.
  if (!starts.empty()) {
    std::set<std::string> reachable;
    std::vector<std::string> stack{starts.front()->id};
    while (!stack.empty()) {
      const std::string current = stack.back();
      stack.pop_back();
      if (!reachable.insert(current).second) continue;
      for (const auto& next : outEdges[current]) stack.push_back(next);
    }
    for (const auto& node : graph.nodes) {
      if (reachable.count(node.id) == 0) {
        sink.error("MFC5029", {}, "node '" + node.id + "' is unreachable from start", node.id,
                   {}, file);
      }
    }
  }

  // Every node must be able to reach an end node.
  {
    std::set<std::string> canFinish;
    std::vector<std::string> stack;
    for (const auto* end : ends) stack.push_back(end->id);
    while (!stack.empty()) {
      const std::string current = stack.back();
      stack.pop_back();
      if (!canFinish.insert(current).second) continue;
      for (const auto& previous : inEdges[current]) stack.push_back(previous);
    }
    for (const auto& node : graph.nodes) {
      if (canFinish.count(node.id) == 0) {
        sink.error("MFC5030", {}, "node '" + node.id + "' can never reach the '" + exitType + "' node", node.id,
                   {}, file);
      }
    }
  }

  // Cycle detection (Kahn).
  {
    std::map<std::string, int> degree = inDegree;
    std::queue<std::string> queue;
    for (const auto& node : graph.nodes) {
      if (degree[node.id] == 0) queue.push(node.id);
    }
    size_t visited = 0;
    while (!queue.empty()) {
      const std::string current = queue.front();
      queue.pop();
      ++visited;
      for (const auto& next : outEdges[current]) {
        if (--degree[next] == 0) queue.push(next);
      }
    }
    if (visited < nodes.size()) {
      for (const auto& node : graph.nodes) {
        if (degree[node.id] > 0) {
          sink.error("MFC5031", {}, "node '" + node.id + "' participates in a cycle", node.id,
                     "quest/dialogue/skill graphs must be acyclic", file);
        }
      }
    }
  }

  // Skill frames are a time-ordered chain. Lowering turns each frame into a
  // relative `wait time`, so an out-of-order frame would silently rewind the
  // animation; reject it at edit time instead.
  if (isSkill && !starts.empty()) {
    int64_t previousTime = -1;
    std::string previousId;
    const GraphNode* current = starts.front();
    std::set<std::string> visited;
    while (current != nullptr && visited.insert(current->id).second) {
      if (current->type == "skill.frame") {
        const int64_t time = intProp(*current, "timeMs", 0);
        if (previousTime >= 0 && time < previousTime) {
          sink.error("MFC5039", {},
                     "frame '" + current->id + "' at " + std::to_string(time) +
                         "ms is earlier than previous frame '" + previousId + "' (" +
                         std::to_string(previousTime) + "ms)",
                     current->id, "sort the skill timeline by time", file);
        }
        previousTime = time;
        previousId = current->id;
      }
      const auto it = outEdges.find(current->id);
      if (it == outEdges.end() || it->second.empty()) break;
      current = nodes.count(it->second.front()) ? nodes[it->second.front()] : nullptr;
    }
  }

  return !sink.hasErrors();
}

std::vector<std::string> questDependencies(const Graph& graph) {
  std::vector<std::string> result;
  for (const auto& node : graph.nodes) {
    if (node.type != "quest.depends") continue;
    const std::string id = stringProp(node, "questId");
    if (!id.empty()) result.push_back(id);
  }
  return result;
}

Program lowerQuestGraph(const Graph& graph, DiagnosticSink& sink) {
  Program program;
  QuestDecl quest;
  quest.name = graph.id;
  quest.loc = Loc{1, 1};

  std::map<std::string, const GraphNode*> nodes;
  std::map<std::string, std::string> next;
  for (const auto& node : graph.nodes) nodes[node.id] = &node;
  for (const auto& edge : graph.edges) next[edge.from] = edge.to;

  const GraphNode* start = nullptr;
  for (const auto& node : graph.nodes) {
    if (node.type == "quest.start") {
      start = &node;
      break;
    }
  }
  if (start == nullptr) {
    sink.error("MFC5032", {}, "cannot lower a quest without a start node", {}, {}, graph.file);
    return program;
  }

  Handler handler;
  handler.isStart = true;
  handler.loc = quest.loc;
  auto body = makeBlock(quest.loc);

  const GraphNode* current = start;
  std::set<std::string> visited;
  while (current != nullptr && visited.insert(current->id).second) {
    const std::string nodeId = current->id;
    const Loc loc{1, 1};
    if (current->type == "quest.start") {
      body->body.push_back(makeEmit("QuestStarted", makeArgs(strLit(graph.id, loc)), loc,
                                    nodeId));
    } else if (current->type == "quest.end") {
      const std::string result = stringProp(*current, "result", "success");
      body->body.push_back(makeEmit(result == "failure" ? "QuestFailed" : "QuestCompleted",
                                    makeArgs(strLit(graph.id, loc)), loc, nodeId));
    } else if (current->type == "quest.condition.kill") {
      const std::string target = stringProp(*current, "target");
      const int64_t count = intProp(*current, "count", 1);
      if (!target.empty() && count > 0) {
        body->body.push_back(
            makeCountedWait("Kill", "target", target, count, nodeId, loc));
        body->body.push_back(makeEmit(
            "ObjectiveProgress",
            makeArgs(strLit(nodeId, loc), intLit(count, loc), intLit(count, loc)), loc,
            nodeId));
      }
    } else if (current->type == "quest.condition.collect") {
      const std::string item = stringProp(*current, "item");
      const int64_t count = intProp(*current, "count", 1);
      if (!item.empty() && count > 0) {
        body->body.push_back(
            makeCountedWait("Collect", "item", item, count, nodeId, loc));
        body->body.push_back(makeEmit(
            "ObjectiveProgress",
            makeArgs(strLit(nodeId, loc), intLit(count, loc), intLit(count, loc)), loc,
            nodeId));
      }
    } else if (current->type == "quest.condition.reach") {
      const std::string area = stringProp(*current, "area");
      if (!area.empty()) {
        body->body.push_back(makeWaitEvent("Reach", "area", area, loc, nodeId));
      }
    } else if (current->type == "quest.condition.talk") {
      const std::string npc = stringProp(*current, "npc");
      if (!npc.empty()) {
        body->body.push_back(makeWaitEvent("Talk", "npc", npc, loc, nodeId));
      }
    } else if (current->type == "quest.timer") {
      double seconds = 1.0;
      current->props.getDouble("seconds", &seconds);
      body->body.push_back(makeWaitTime(seconds, loc, nodeId));
    } else if (current->type == "quest.log") {
      body->body.push_back(
          makeEmit("Log", makeArgs(strLit(stringProp(*current, "text"), loc)), loc, nodeId));
    } else if (current->type == "quest.reward") {
      body->body.push_back(makeCall(
          "GrantReward",
          makeArgs(intLit(intProp(*current, "exp", 0), loc),
                   strLit(stringProp(*current, "item"), loc)),
          loc, nodeId));
    } else if (current->type == "quest.depends") {
      // Compile-time only: the dependency is consumed by the build graph.
    } else {
      sink.error("MFC5033", {}, "cannot lower node type '" + current->type + "'", nodeId, {},
                 graph.file);
    }

    auto it = next.find(current->id);
    current = (it == next.end()) ? nullptr : nodes[it->second];
  }

  handler.body = std::move(body);
  quest.handlers.push_back(std::move(handler));
  program.quests.push_back(std::move(quest));
  return program;
}

Program lowerDialogueGraph(const Graph& graph, DiagnosticSink& sink) {
  Program program;
  QuestDecl dialogue;
  dialogue.name = graph.id;
  dialogue.loc = Loc{1, 1};

  std::map<std::string, const GraphNode*> nodes;
  std::map<std::string, std::string> next;
  for (const auto& node : graph.nodes) nodes[node.id] = &node;
  for (const auto& edge : graph.edges) next[edge.from] = edge.to;

  const GraphNode* start = nullptr;
  for (const auto& node : graph.nodes) {
    if (node.type == "dialogue.start") {
      start = &node;
      break;
    }
  }
  if (start == nullptr) {
    sink.error("MFC5032", {}, "cannot lower a dialogue without a start node", {}, {}, graph.file);
    return program;
  }

  Handler handler;
  handler.isStart = true;
  handler.loc = dialogue.loc;
  auto body = makeBlock(dialogue.loc);

  const GraphNode* current = start;
  std::set<std::string> visited;
  while (current != nullptr && visited.insert(current->id).second) {
    const std::string nodeId = current->id;
    const Loc loc{1, 1};
    if (current->type == "dialogue.start") {
      body->body.push_back(makeEmit("DialogueStarted", makeArgs(strLit(graph.id, loc)), loc,
                                    nodeId));
    } else if (current->type == "dialogue.line") {
      body->body.push_back(makeEmit(
          "DialogueLine",
          makeArgs(strLit(stringProp(*current, "speaker"), loc),
                   strLit(stringProp(*current, "text"), loc)),
          loc, nodeId));
      body->body.push_back(makeWaitEvent("DialogueAdvance", "", "", loc, nodeId));
    } else if (current->type == "dialogue.choice") {
      body->body.push_back(makeEmit(
          "DialogueChoice", makeArgs(strLit(stringProp(*current, "options"), loc)), loc, nodeId));
      body->body.push_back(makeWaitEvent("DialogueChoiceSelected", "", "", loc, nodeId));
    } else if (current->type == "dialogue.flag") {
      const std::string flag = stringProp(*current, "flag");
      if (!flag.empty()) {
        body->body.push_back(makeWaitEvent("DialogueFlag", "flag", flag, loc, nodeId));
      }
    } else if (current->type == "dialogue.timeline") {
      const std::string asset = stringProp(*current, "asset");
      body->body.push_back(
          makeCall("PlayTimeline", makeArgs(strLit(asset, loc)), loc, nodeId));
      if (boolProp(*current, "waitForFinish", true)) {
        body->body.push_back(makeWaitEvent("TimelineFinished", "asset", asset, loc, nodeId));
      }
    } else if (current->type == "dialogue.wait") {
      double seconds = 1.0;
      doubleProp(*current, "seconds", 1.0, &seconds);
      body->body.push_back(makeWaitTime(seconds, loc, nodeId));
    } else if (current->type == "dialogue.end") {
      body->body.push_back(
          makeEmit("DialogueEnded", makeArgs(strLit(graph.id, loc)), loc, nodeId));
    } else {
      sink.error("MFC5033", {}, "cannot lower node type '" + current->type + "'", nodeId, {},
                 graph.file);
    }

    auto it = next.find(current->id);
    current = (it == next.end()) ? nullptr : nodes[it->second];
  }

  handler.body = std::move(body);
  dialogue.handlers.push_back(std::move(handler));
  program.quests.push_back(std::move(dialogue));
  return program;
}

Program lowerSkillGraph(const Graph& graph, DiagnosticSink& sink) {
  Program program;
  QuestDecl skill;
  skill.name = graph.id;
  skill.loc = Loc{1, 1};

  std::map<std::string, const GraphNode*> nodes;
  std::map<std::string, std::string> next;
  for (const auto& node : graph.nodes) nodes[node.id] = &node;
  for (const auto& edge : graph.edges) next[edge.from] = edge.to;

  const GraphNode* start = nullptr;
  for (const auto& node : graph.nodes) {
    if (node.type == "skill.cast") {
      start = &node;
      break;
    }
  }
  if (start == nullptr) {
    sink.error("MFC5032", {}, "cannot lower a skill without a cast node", {}, {}, graph.file);
    return program;
  }

  Handler handler;
  handler.isStart = true;
  handler.loc = skill.loc;
  auto body = makeBlock(skill.loc);

  const GraphNode* current = start;
  std::set<std::string> visited;
  int64_t previousTime = 0;
  while (current != nullptr && visited.insert(current->id).second) {
    const std::string nodeId = current->id;
    const Loc loc{1, 1};
    if (current->type == "skill.cast") {
      body->body.push_back(makeEmit("SkillCast", makeArgs(strLit(graph.id, loc)), loc, nodeId));
    } else if (current->type == "skill.frame") {
      const int64_t time = intProp(*current, "timeMs", 0);
      const int64_t delta = time - previousTime;
      if (delta > 0) {
        body->body.push_back(makeWaitTime(static_cast<double>(delta) / 1000.0, loc, nodeId));
      }
      previousTime = time;
      const std::string event = stringProp(*current, "event");
      const std::string target = stringProp(*current, "target");
      const std::string asset = stringProp(*current, "asset");
      const int64_t amount = intProp(*current, "amount", 0);
      body->body.push_back(makeCall(
          "SkillEvent",
          makeArgs(strLit(event, loc), strLit(target, loc), intLit(amount, loc),
                   strLit(asset, loc)),
          loc, nodeId));
      body->body.push_back(makeEmit(
          "SkillFrame",
          makeArgs(strLit(graph.id, loc), intLit(time, loc), strLit(event, loc)), loc, nodeId));
    } else if (current->type == "skill.end") {
      body->body.push_back(
          makeEmit("SkillEnded", makeArgs(strLit(graph.id, loc)), loc, nodeId));
    } else {
      sink.error("MFC5033", {}, "cannot lower node type '" + current->type + "'", nodeId, {},
                 graph.file);
    }

    auto it = next.find(current->id);
    current = (it == next.end()) ? nullptr : nodes[it->second];
  }

  handler.body = std::move(body);
  skill.handlers.push_back(std::move(handler));
  program.quests.push_back(std::move(skill));
  return program;
}

std::string chunkNameFor(const Graph& graph, const Handler& handler) {
  if (!handler.isStart) return graph.id + "/on_" + handler.eventName;
  if (graph.kind == "skill") return graph.id + "/cast";
  return graph.id + "/start";
}

Program lowerGraph(const Graph& graph, DiagnosticSink& sink) {
  if (graph.kind == "dialogue") return lowerDialogueGraph(graph, sink);
  if (graph.kind == "skill") return lowerSkillGraph(graph, sink);
  return lowerQuestGraph(graph, sink);
}

}  // namespace mfc
