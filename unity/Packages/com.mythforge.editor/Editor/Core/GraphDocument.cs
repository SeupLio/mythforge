using System;
using System.Collections.Generic;
using System.Text;

namespace MythForge.Editor
{
    public sealed class MfGraphNode
    {
        public string Id = string.Empty;
        public string Type = string.Empty;
        public SortedDictionary<string, MfJsonValue> Props =
            new SortedDictionary<string, MfJsonValue>(StringComparer.Ordinal);
        public float X;
        public float Y;

        public MfGraphNode Clone()
        {
            var copy = new MfGraphNode
            {
                Id = Id,
                Type = Type,
                X = X,
                Y = Y
            };
            foreach (var pair in Props) copy.Props[pair.Key] = pair.Value.Clone();
            return copy;
        }
    }

    public sealed class MfGraphEdge
    {
        public string From = string.Empty;
        public string To = string.Empty;

        public MfGraphEdge Clone()
        {
            return new MfGraphEdge { From = From, To = To };
        }
    }

    /// <summary>
    /// Authoritative in-memory content graph. Mirrors content/**/*.json exactly
    /// and keeps editor-only layout data under an "editor" field the compiler
    /// ignores. The same model drives quests, dialogue and skills; Catalog picks
    /// the node type set.
    /// </summary>
    public sealed class MfGraphDocument
    {
        public int SchemaVersion = 1;
        public string Kind = "quest";
        public string Id = "q_new_quest";
        public string Title = "新任务";
        public MfJsonValue Meta;
        public MfNodeCatalog Catalog = MfQuestNodeCatalog.Catalog;
        public List<MfGraphNode> Nodes = new List<MfGraphNode>();
        public List<MfGraphEdge> Edges = new List<MfGraphEdge>();
        private readonly Dictionary<string, int> _idCounters = new Dictionary<string, int>(StringComparer.Ordinal);

        public static MfNodeCatalog CatalogForKind(string kind)
        {
            if (kind == "dialogue") return MfDialogueNodeCatalog.Catalog;
            if (kind == "skill") return MfSkillNodeCatalog.Catalog;
            return MfQuestNodeCatalog.Catalog;
        }

        private static string DefaultIdForKind(string kind)
        {
            if (kind == "dialogue") return "d_new_dialogue";
            if (kind == "skill") return "sk_new_skill";
            return "q_new_quest";
        }

        private static string DefaultTitleForKind(string kind)
        {
            if (kind == "dialogue") return "新剧情";
            if (kind == "skill") return "新技能";
            return "新任务";
        }

        public static MfGraphDocument CreateNew(string id)
        {
            return CreateNew("quest", id);
        }

        public static MfGraphDocument CreateNew(string kind, string id)
        {
            var document = new MfGraphDocument
            {
                Kind = string.IsNullOrEmpty(kind) ? "quest" : kind,
                Catalog = CatalogForKind(kind)
            };
            document.Id = string.IsNullOrEmpty(id) ? DefaultIdForKind(document.Kind) : id;
            document.Title = DefaultTitleForKind(document.Kind);
            MfNodeTypeSpec entrySpec = document.Catalog.Entry;
            MfNodeTypeSpec exitSpec = document.Catalog.Exit;
            MfGraphNode start = entrySpec != null ? document.AddNode(entrySpec.Id, 60f, 180f) : null;
            MfGraphNode end = exitSpec != null ? document.AddNode(exitSpec.Id, 520f, 180f) : null;
            string reason;
            if (start != null && end != null) document.Connect(start.Id, end.Id, out reason);
            return document;
        }

        public MfGraphNode FindNode(string id)
        {
            for (int i = 0; i < Nodes.Count; i++)
            {
                if (Nodes[i].Id == id) return Nodes[i];
            }
            return null;
        }

        public MfGraphNode AddNode(string type, float x, float y)
        {
            MfNodeTypeSpec spec = Catalog.Find(type);
            if (spec == null) return null;
            var node = new MfGraphNode
            {
                Id = GenerateNodeId(spec),
                Type = type,
                X = x,
                Y = y
            };
            for (int i = 0; i < spec.Props.Count; i++)
            {
                node.Props[spec.Props[i].Name] = spec.Props[i].CreateDefault();
            }
            Nodes.Add(node);
            return node;
        }

        public bool RemoveNode(string id)
        {
            int index = Nodes.FindIndex(n => n.Id == id);
            if (index < 0) return false;
            Nodes.RemoveAt(index);
            Edges.RemoveAll(e => e.From == id || e.To == id);
            return true;
        }

        public bool Connect(string from, string to, out string reason)
        {
            reason = string.Empty;
            MfGraphNode fromNode = FindNode(from);
            MfGraphNode toNode = FindNode(to);
            if (fromNode == null || toNode == null)
            {
                reason = "连线端点不存在";
                return false;
            }
            if (from == to)
            {
                reason = "不能连接到自身";
                return false;
            }
            for (int i = 0; i < Edges.Count; i++)
            {
                if (Edges[i].From == from && Edges[i].To == to)
                {
                    reason = "这条连线已经存在";
                    return false;
                }
            }
            MfNodeTypeSpec fromSpec = Catalog.Find(fromNode.Type);
            MfNodeTypeSpec toSpec = Catalog.Find(toNode.Type);
            if (fromSpec == null || toSpec == null)
            {
                reason = "节点类型未注册";
                return false;
            }
            if (OutDegree(from) >= fromSpec.MaxOutputs)
            {
                reason = fromSpec.MaxOutputs <= 1
                    ? "v1 只支持线性内容链：该节点已有出边"
                    : "该节点输出端口已满";
                return false;
            }
            if (InDegree(to) >= toSpec.MaxInputs)
            {
                reason = "该节点输入端口已满";
                return false;
            }
            Edges.Add(new MfGraphEdge { From = from, To = to });
            return true;
        }

        public bool Disconnect(string from, string to)
        {
            int index = Edges.FindIndex(e => e.From == from && e.To == to);
            if (index < 0) return false;
            Edges.RemoveAt(index);
            return true;
        }

        public int OutDegree(string id)
        {
            int count = 0;
            for (int i = 0; i < Edges.Count; i++)
            {
                if (Edges[i].From == id) count++;
            }
            return count;
        }

        public int InDegree(string id)
        {
            int count = 0;
            for (int i = 0; i < Edges.Count; i++)
            {
                if (Edges[i].To == id) count++;
            }
            return count;
        }

        public string FirstSuccessor(string id)
        {
            for (int i = 0; i < Edges.Count; i++)
            {
                if (Edges[i].From == id) return Edges[i].To;
            }
            return null;
        }

        public MfGraphDocument Clone()
        {
            var copy = new MfGraphDocument
            {
                SchemaVersion = SchemaVersion,
                Kind = Kind,
                Id = Id,
                Title = Title,
                Meta = Meta != null ? Meta.Clone() : null
            };
            for (int i = 0; i < Nodes.Count; i++) copy.Nodes.Add(Nodes[i].Clone());
            for (int i = 0; i < Edges.Count; i++) copy.Edges.Add(Edges[i].Clone());
            return copy;
        }

        public string ToJson()
        {
            var root = MfJsonValue.Object();
            root["schemaVersion"] = MfJsonValue.Number(SchemaVersion);
            root["kind"] = MfJsonValue.String(Kind);
            root["id"] = MfJsonValue.String(Id);
            root["title"] = MfJsonValue.String(Title);
            if (Meta != null && Meta.IsObject) root["meta"] = Meta.Clone();

            var nodes = MfJsonValue.Array();
            for (int i = 0; i < Nodes.Count; i++)
            {
                MfGraphNode node = Nodes[i];
                var item = MfJsonValue.Object();
                item["id"] = MfJsonValue.String(node.Id);
                item["type"] = MfJsonValue.String(node.Type);
                var props = MfJsonValue.Object();
                foreach (var pair in node.Props) props[pair.Key] = pair.Value.Clone();
                item["props"] = props;
                var editor = MfJsonValue.Object();
                editor["x"] = MfJsonValue.Number(node.X);
                editor["y"] = MfJsonValue.Number(node.Y);
                item["editor"] = editor;
                nodes.Add(item);
            }
            root["nodes"] = nodes;

            var edges = MfJsonValue.Array();
            for (int i = 0; i < Edges.Count; i++)
            {
                var item = MfJsonValue.Object();
                item["from"] = MfJsonValue.String(Edges[i].From);
                item["to"] = MfJsonValue.String(Edges[i].To);
                edges.Add(item);
            }
            root["edges"] = edges;
            return root.ToJson();
        }

        public static MfGraphDocument FromJson(string text, out string error)
        {
            error = null;
            MfJsonValue root;
            string parseError;
            if (!MfJsonValue.Parse(text, out root, out parseError))
            {
                error = "invalid JSON: " + parseError;
                return null;
            }
            if (!root.IsObject)
            {
                error = "graph root must be an object";
                return null;
            }
            var document = new MfGraphDocument();
            document.SchemaVersion = (int)root.GetInt("schemaVersion", 1);
            document.Kind = root.GetString("kind", "quest");
            document.Catalog = CatalogForKind(document.Kind);
            document.Id = root.GetString("id", string.Empty);
            document.Title = root.GetString("title", string.Empty);
            MfJsonValue meta;
            if (root.TryGet("meta", out meta)) document.Meta = meta.Clone();

            MfJsonValue nodes;
            if (root.TryGet("nodes", out nodes) && nodes.IsArray)
            {
                for (int i = 0; i < nodes.Items.Count; i++)
                {
                    MfJsonValue item = nodes.Items[i];
                    if (!item.IsObject) continue;
                    var node = new MfGraphNode();
                    node.Id = item.GetString("id");
                    node.Type = item.GetString("type");
                    MfJsonValue props;
                    if (item.TryGet("props", out props) && props.IsObject)
                    {
                        foreach (var pair in props.Members)
                        {
                            node.Props[pair.Key] = pair.Value.Clone();
                        }
                    }
                    MfJsonValue editor;
                    if (item.TryGet("editor", out editor) && editor.IsObject)
                    {
                        node.X = (float)editor.GetDouble("x", 0.0);
                        node.Y = (float)editor.GetDouble("y", 0.0);
                    }
                    if (document.FindNode(node.Id) != null)
                    {
                        error = "duplicate node id '" + node.Id + "'";
                        return null;
                    }
                    document.Nodes.Add(node);
                }
            }

            MfJsonValue edges;
            if (root.TryGet("edges", out edges) && edges.IsArray)
            {
                for (int i = 0; i < edges.Items.Count; i++)
                {
                    MfJsonValue item = edges.Items[i];
                    if (!item.IsObject) continue;
                    document.Edges.Add(new MfGraphEdge
                    {
                        From = item.GetString("from"),
                        To = item.GetString("to")
                    });
                }
            }
            return document;
        }

        /// <summary>
        /// Instant structural feedback. Mirrors the C++ validator's rules but is
        /// not a substitute: `mf check` remains the compile gate.
        /// </summary>
        public List<MfDiagnostic> Validate()
        {
            var diagnostics = new List<MfDiagnostic>();
            if (!IsValidId(Id))
            {
                diagnostics.Add(Error("MFD001", "内容 ID 只能包含字母、数字和下划线", string.Empty));
            }
            if (!string.Equals(Kind, "quest", StringComparison.Ordinal) &&
                !string.Equals(Kind, "dialogue", StringComparison.Ordinal) &&
                !string.Equals(Kind, "skill", StringComparison.Ordinal))
            {
                diagnostics.Add(Error("MFD002", "只支持 quest / dialogue / skill 类型的图", string.Empty));
            }
            MfNodeTypeSpec entrySpec = Catalog.Entry;
            MfNodeTypeSpec exitSpec = Catalog.Exit;
            string entryType = entrySpec != null ? entrySpec.Id : Kind + ".start";
            string exitType = exitSpec != null ? exitSpec.Id : Kind + ".end";

            var seen = new HashSet<string>(StringComparer.Ordinal);
            for (int i = 0; i < Nodes.Count; i++)
            {
                if (!seen.Add(Nodes[i].Id))
                {
                    diagnostics.Add(Error("MFD003", "节点 ID 重复: " + Nodes[i].Id, Nodes[i].Id));
                }
            }

            int starts = 0;
            int ends = 0;
            for (int i = 0; i < Nodes.Count; i++)
            {
                MfGraphNode node = Nodes[i];
                MfNodeTypeSpec spec = Catalog.Find(node.Type);
                if (spec == null)
                {
                    diagnostics.Add(Error("MFD004", "未知节点类型 '" + node.Type + "'", node.Id));
                    continue;
                }
                if (node.Type == entryType) starts++;
                if (node.Type == exitType) ends++;

                int inDegree = InDegree(node.Id);
                int outDegree = OutDegree(node.Id);
                if (inDegree < spec.MinInputs || inDegree > spec.MaxInputs)
                {
                    diagnostics.Add(Error("MFD005",
                        string.Format("节点 '{0}' 有 {1} 条入边，期望 {2}..{3}", node.Id, inDegree,
                            spec.MinInputs, spec.MaxInputs), node.Id));
                }
                if (outDegree < spec.MinOutputs || outDegree > spec.MaxOutputs)
                {
                    diagnostics.Add(Error("MFD006",
                        string.Format("节点 '{0}' 有 {1} 条出边，期望 {2}..{3}", node.Id, outDegree,
                            spec.MinOutputs, spec.MaxOutputs), node.Id));
                }

                for (int p = 0; p < spec.Props.Count; p++)
                {
                    MfPropSpec prop = spec.Props[p];
                    MfJsonValue value;
                    if (!node.Props.TryGetValue(prop.Name, out value))
                    {
                        if (prop.Required)
                        {
                            diagnostics.Add(Error("MFD007",
                                "节点 '" + node.Id + "' 缺少必填属性 '" + prop.Name + "'", node.Id));
                        }
                        continue;
                    }
                    if (!PropTypeMatches(prop.Kind, value))
                    {
                        diagnostics.Add(Error("MFD008",
                            "属性 '" + prop.Name + "' 类型应为 " + prop.Kind.ToString().ToLowerInvariant(),
                            node.Id));
                    }
                }
                foreach (var pair in node.Props)
                {
                    if (spec.FindProp(pair.Key) == null)
                    {
                        diagnostics.Add(Warning("MFD101",
                            "节点 '" + node.Id + "' 有未知属性 '" + pair.Key + "'", node.Id));
                    }
                }
                if (node.Type == "quest.condition.kill" || node.Type == "quest.condition.collect")
                {
                    MfJsonValue count;
                    if (node.Props.TryGetValue("count", out count) && count.IsNumber && count.AsInt() < 1)
                    {
                        diagnostics.Add(Error("MFD009", "数量必须大于 0", node.Id));
                    }
                }
                if (node.Type == "quest.timer" || node.Type == "dialogue.wait")
                {
                    MfJsonValue seconds;
                    if (node.Props.TryGetValue("seconds", out seconds) && seconds.IsNumber &&
                        seconds.AsDouble() < 0.0)
                    {
                        diagnostics.Add(Error("MFD010", "等待秒数不能为负", node.Id));
                    }
                }
                if (node.Type == "dialogue.line")
                {
                    if (PropString(node, "speaker").Length == 0 || PropString(node, "text").Length == 0)
                    {
                        diagnostics.Add(Error("MFD020", "台词缺少说话人或文本", node.Id));
                    }
                }
                if (node.Type == "dialogue.choice")
                {
                    string options = PropString(node, "options");
                    int nonEmpty = 0;
                    string[] parts = options.Split('|');
                    for (int p = 0; p < parts.Length; p++)
                    {
                        if (parts[p].Trim().Length > 0) nonEmpty++;
                    }
                    if (nonEmpty < 2)
                    {
                        diagnostics.Add(Error("MFD021", "选项节点至少需要两个用 | 分隔的选项", node.Id));
                    }
                }
                if (node.Type == "dialogue.timeline" && PropString(node, "asset").Length == 0)
                {
                    diagnostics.Add(Error("MFD022", "Timeline 节点没有指定资产", node.Id));
                }
                if (node.Type == "skill.frame")
                {
                    MfJsonValue time;
                    if (node.Props.TryGetValue("timeMs", out time) && time.IsNumber && time.AsInt() < 0)
                    {
                        diagnostics.Add(Error("MFD023", "帧时间不能为负", node.Id));
                    }
                    string evt = PropString(node, "event");
                    if (evt != "damage" && evt != "buff" && evt != "vfx" && evt != "sfx" &&
                        evt != "move")
                    {
                        diagnostics.Add(Warning("MFD103",
                            "未知技能事件 '" + evt + "'，期望 damage|buff|vfx|sfx|move", node.Id));
                    }
                }
                if (outDegree > 1)
                {
                    diagnostics.Add(Error("MFD011",
                        "v1 只支持线性内容链：节点不能有多条出边", node.Id));
                }
            }

            if (starts == 0) diagnostics.Add(Error("MFD012", "缺少入口节点 '" + entryType + "'", string.Empty));
            if (starts > 1) diagnostics.Add(Error("MFD013", "入口节点 '" + entryType + "' 只能有一个", string.Empty));
            if (ends == 0) diagnostics.Add(Error("MFD014", "缺少出口节点 '" + exitType + "'", string.Empty));

            var outgoing = new Dictionary<string, List<string>>(StringComparer.Ordinal);
            var incoming = new Dictionary<string, List<string>>(StringComparer.Ordinal);
            var edgeSeen = new HashSet<string>(StringComparer.Ordinal);
            for (int i = 0; i < Edges.Count; i++)
            {
                MfGraphEdge edge = Edges[i];
                if (FindNode(edge.From) == null || FindNode(edge.To) == null)
                {
                    diagnostics.Add(Error("MFD015", "连线引用了不存在的节点", edge.From));
                    continue;
                }
                if (edge.From == edge.To)
                {
                    diagnostics.Add(Error("MFD016", "节点不能连接到自身", edge.From));
                }
                string key = edge.From + "->" + edge.To;
                if (!edgeSeen.Add(key))
                {
                    diagnostics.Add(Warning("MFD102", "重复连线 " + key, edge.From));
                    continue;
                }
                List<string> list;
                if (!outgoing.TryGetValue(edge.From, out list))
                {
                    list = new List<string>();
                    outgoing[edge.From] = list;
                }
                list.Add(edge.To);
                if (!incoming.TryGetValue(edge.To, out list))
                {
                    list = new List<string>();
                    incoming[edge.To] = list;
                }
                list.Add(edge.From);
            }

            string startId = null;
            for (int i = 0; i < Nodes.Count; i++)
            {
                if (Nodes[i].Type == entryType)
                {
                    startId = Nodes[i].Id;
                    break;
                }
            }
            if (startId != null)
            {
                var reachable = new HashSet<string>(StringComparer.Ordinal);
                var stack = new Stack<string>();
                stack.Push(startId);
                while (stack.Count > 0)
                {
                    string current = stack.Pop();
                    if (!reachable.Add(current)) continue;
                    List<string> next;
                    if (outgoing.TryGetValue(current, out next))
                    {
                        for (int i = 0; i < next.Count; i++) stack.Push(next[i]);
                    }
                }
                for (int i = 0; i < Nodes.Count; i++)
                {
                    if (!reachable.Contains(Nodes[i].Id))
                    {
                        diagnostics.Add(Error("MFD017", "节点不可从开始节点到达", Nodes[i].Id));
                    }
                }
            }

            {
                var canFinish = new HashSet<string>(StringComparer.Ordinal);
                var stack = new Stack<string>();
                for (int i = 0; i < Nodes.Count; i++)
                {
                    if (Nodes[i].Type == exitType) stack.Push(Nodes[i].Id);
                }
                while (stack.Count > 0)
                {
                    string current = stack.Pop();
                    if (!canFinish.Add(current)) continue;
                    List<string> previous;
                    if (incoming.TryGetValue(current, out previous))
                    {
                        for (int i = 0; i < previous.Count; i++) stack.Push(previous[i]);
                    }
                }
                for (int i = 0; i < Nodes.Count; i++)
                {
                    if (!canFinish.Contains(Nodes[i].Id))
                    {
                        diagnostics.Add(Error("MFD018", "节点无法到达任务结束", Nodes[i].Id));
                    }
                }
            }

            {
                var degree = new Dictionary<string, int>(StringComparer.Ordinal);
                for (int i = 0; i < Nodes.Count; i++) degree[Nodes[i].Id] = InDegree(Nodes[i].Id);
                var queue = new Queue<string>();
                for (int i = 0; i < Nodes.Count; i++)
                {
                    if (degree[Nodes[i].Id] == 0) queue.Enqueue(Nodes[i].Id);
                }
                int visited = 0;
                while (queue.Count > 0)
                {
                    string current = queue.Dequeue();
                    visited++;
                    List<string> next;
                    if (outgoing.TryGetValue(current, out next))
                    {
                        for (int i = 0; i < next.Count; i++)
                        {
                            degree[next[i]]--;
                            if (degree[next[i]] == 0) queue.Enqueue(next[i]);
                        }
                    }
                }
                if (visited < Nodes.Count)
                {
                    for (int i = 0; i < Nodes.Count; i++)
                    {
                        if (degree[Nodes[i].Id] > 0)
                        {
                            diagnostics.Add(Error("MFD019", "节点参与了一个环", Nodes[i].Id));
                        }
                    }
                }
            }

            // Skill frames must be non-decreasing along the chain; the compiler
            // lowers frame times into relative waits.
            if (Kind == "skill" && startId != null)
            {
                long previousTime = -1;
                string previousId = null;
                string current = startId;
                var visited = new HashSet<string>(StringComparer.Ordinal);
                while (!string.IsNullOrEmpty(current) && visited.Add(current))
                {
                    MfGraphNode node = FindNode(current);
                    if (node == null) break;
                    if (node.Type == MfSkillNodeCatalog.FrameType)
                    {
                        MfJsonValue value;
                        long time = node.Props.TryGetValue("timeMs", out value) && value.IsNumber
                            ? value.AsInt()
                            : 0;
                        if (previousTime >= 0 && time < previousTime)
                        {
                            diagnostics.Add(Error("MFD024",
                                string.Format("技能帧 '{0}' ({1}ms) 早于前一帧 '{2}' ({3}ms)",
                                    node.Id, time, previousId, previousTime), node.Id));
                        }
                        previousTime = time;
                        previousId = node.Id;
                    }
                    List<string> next;
                    current = outgoing.TryGetValue(current, out next) && next.Count > 0 ? next[0] : null;
                }
            }

            return diagnostics;
        }

        public static bool IsValidId(string id)
        {
            if (string.IsNullOrEmpty(id)) return false;
            for (int i = 0; i < id.Length; i++)
            {
                char c = id[i];
                bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
                if (!ok) return false;
            }
            return true;
        }

        private string GenerateNodeId(MfNodeTypeSpec spec)
        {
            string baseName = spec.Id;
            int dot = baseName.LastIndexOf('.');
            if (dot >= 0) baseName = baseName.Substring(dot + 1);
            var builder = new StringBuilder();
            for (int i = 0; i < baseName.Length; i++)
            {
                char c = baseName[i];
                builder.Append(char.IsLetterOrDigit(c) ? c : '_');
            }
            string prefix = "n_" + builder;
            int counter;
            _idCounters.TryGetValue(prefix, out counter);
            if (counter < 1) counter = 1;
            while (FindNode(prefix + "_" + counter) != null) counter++;
            _idCounters[prefix] = counter + 1;
            return prefix + "_" + counter;
        }

        private static bool PropTypeMatches(MfPropKind kind, MfJsonValue value)
        {
            switch (kind)
            {
                case MfPropKind.String: return value.IsString;
                case MfPropKind.Int: return value.IsNumber;
                case MfPropKind.Float: return value.IsNumber;
                case MfPropKind.Bool: return value.IsBool;
            }
            return false;
        }

        private static string PropString(MfGraphNode node, string name)
        {
            MfJsonValue value;
            if (node.Props.TryGetValue(name, out value) && value.IsString) return value.AsString();
            return string.Empty;
        }

        private static MfDiagnostic Error(string code, string message, string nodeId)
        {
            return new MfDiagnostic
            {
                Code = code,
                Severity = MfSeverity.Error,
                Message = message,
                NodeId = nodeId
            };
        }

        private static MfDiagnostic Warning(string code, string message, string nodeId)
        {
            return new MfDiagnostic
            {
                Code = code,
                Severity = MfSeverity.Warning,
                Message = message,
                NodeId = nodeId
            };
        }
    }
}
