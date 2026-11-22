using System;
using System.Collections.Generic;

namespace MythForge.Editor
{
    /// <summary>
    /// Pure-C# model behind the skill timeline editor. A skill graph is a linear
    /// chain: cast -> frames sorted by timeMs -> end. This class owns the sorting
    /// and edge rebuilding so the UI and tests share one implementation.
    /// </summary>
    public static class MfSkillTimeline
    {
        public static readonly string[] EventTypes = { "damage", "buff", "vfx", "sfx", "move" };

        public static MfGraphDocument CreateDocument(string id)
        {
            return MfGraphDocument.CreateNew("skill", id);
        }

        public static List<MfGraphNode> Frames(MfGraphDocument document)
        {
            var frames = new List<MfGraphNode>();
            if (document == null) return frames;
            for (int i = 0; i < document.Nodes.Count; i++)
            {
                if (document.Nodes[i].Type == MfSkillNodeCatalog.FrameType) frames.Add(document.Nodes[i]);
            }
            frames.Sort((a, b) =>
            {
                int byTime = TimeMs(a).CompareTo(TimeMs(b));
                return byTime != 0 ? byTime : string.CompareOrdinal(a.Id, b.Id);
            });
            return frames;
        }

        public static long TimeMs(MfGraphNode node)
        {
            MfJsonValue value;
            if (node != null && node.Props.TryGetValue("timeMs", out value) && value.IsNumber)
            {
                return value.AsInt();
            }
            return 0;
        }

        public static void SetTimeMs(MfGraphNode node, long timeMs)
        {
            if (node == null) return;
            node.Props["timeMs"] = MfJsonValue.Number(Math.Max(0, timeMs));
            node.X = Math.Max(0, timeMs) * 0.2f;
        }

        public static long DurationMs(MfGraphDocument document)
        {
            List<MfGraphNode> frames = Frames(document);
            long duration = 0;
            for (int i = 0; i < frames.Count; i++)
            {
                duration = Math.Max(duration, TimeMs(frames[i]));
            }
            return duration;
        }

        public static MfGraphNode AddFrame(MfGraphDocument document, long timeMs)
        {
            if (document == null) return null;
            MfGraphNode node = document.AddNode(MfSkillNodeCatalog.FrameType, 0f, 0f);
            if (node == null) return null;
            SetTimeMs(node, timeMs);
            node.Props["event"] = MfJsonValue.String("damage");
            node.Props["target"] = MfJsonValue.String("");
            node.Props["amount"] = MfJsonValue.Number(0);
            node.Props["asset"] = MfJsonValue.String("");
            RebuildChain(document);
            return node;
        }

        public static bool RemoveFrame(MfGraphDocument document, string nodeId)
        {
            if (document == null) return false;
            MfGraphNode node = document.FindNode(nodeId);
            if (node == null || node.Type != MfSkillNodeCatalog.FrameType) return false;
            document.RemoveNode(nodeId);
            RebuildChain(document);
            return true;
        }

        public static bool MoveFrame(MfGraphDocument document, string nodeId, long timeMs)
        {
            if (document == null) return false;
            MfGraphNode node = document.FindNode(nodeId);
            if (node == null || node.Type != MfSkillNodeCatalog.FrameType) return false;
            SetTimeMs(node, timeMs);
            RebuildChain(document);
            return true;
        }

        /// <summary>
        /// Rewires cast -> frames (time order) -> end. Frame order in the graph is
        /// the source of truth for lowering, so every time edit rebuilds it.
        /// </summary>
        public static void RebuildChain(MfGraphDocument document)
        {
            if (document == null) return;
            MfNodeTypeSpec entry = document.Catalog.Entry;
            MfNodeTypeSpec exit = document.Catalog.Exit;
            if (entry == null || exit == null) return;
            string entryId = null;
            string exitId = null;
            for (int i = 0; i < document.Nodes.Count; i++)
            {
                MfGraphNode node = document.Nodes[i];
                if (node.Type == entry.Id) entryId = node.Id;
                else if (node.Type == exit.Id) exitId = node.Id;
            }
            if (string.IsNullOrEmpty(entryId) || string.IsNullOrEmpty(exitId)) return;

            document.Edges.Clear();
            List<MfGraphNode> frames = Frames(document);
            string previous = entryId;
            for (int i = 0; i < frames.Count; i++)
            {
                document.Edges.Add(new MfGraphEdge { From = previous, To = frames[i].Id });
                previous = frames[i].Id;
            }
            document.Edges.Add(new MfGraphEdge { From = previous, To = exitId });
        }
    }
}
