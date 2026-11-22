using System.Diagnostics;
using System.Linq;
using NUnit.Framework;
using UnityEngine;

namespace MythForge.Editor.Tests
{
    /// <summary>
    /// Headless performance measurement for the editor data path. It builds a
    /// 2000-node linear quest, then times model construction, JSON round trip,
    /// local validation and GraphView rebuild. These are CPU timings in
    /// batchmode, not in-editor frame rates.
    /// </summary>
    public class EditorPerformanceTests
    {
        private const int NodeCount = 2000;

        private static MfGraphDocument BuildChain(int nodeCount)
        {
            var document = new MfGraphDocument { Id = "q_perf_2000", Title = "perf" };
            MfGraphNode start = document.AddNode("quest.start", 0f, 0f);
            MfGraphNode previous = start;
            for (int i = 2; i < nodeCount; i++)
            {
                MfGraphNode node = document.AddNode("quest.log", (i - 2) * 240f, 0f);
                Assert.IsNotNull(node);
                node.Props["text"] = MfJsonValue.String("log " + i);
                string reason;
                Assert.IsTrue(document.Connect(previous.Id, node.Id, out reason), reason);
                previous = node;
            }
            MfGraphNode end = document.AddNode("quest.end", (nodeCount - 2) * 240f, 0f);
            string finalReason;
            Assert.IsTrue(document.Connect(previous.Id, end.Id, out finalReason), finalReason);
            return document;
        }

        [Test]
        public void BuildsAndRendersTwoThousandNodes()
        {
            var stopwatch = Stopwatch.StartNew();
            MfGraphDocument document = BuildChain(NodeCount);
            long buildMs = stopwatch.ElapsedMilliseconds;

            stopwatch.Restart();
            string json = document.ToJson();
            long toJsonMs = stopwatch.ElapsedMilliseconds;

            stopwatch.Restart();
            string error;
            MfGraphDocument loaded = MfGraphDocument.FromJson(json, out error);
            long fromJsonMs = stopwatch.ElapsedMilliseconds;
            Assert.IsNotNull(loaded, error);

            stopwatch.Restart();
            var diagnostics = loaded.Validate();
            long validateMs = stopwatch.ElapsedMilliseconds;

            stopwatch.Restart();
            var view = new MfGraphView();
            view.Rebuild(loaded);
            long rebuildMs = stopwatch.ElapsedMilliseconds;

            int nodeViews = view.NodeViews.Count();
            long jsonBytes = System.Text.Encoding.UTF8.GetByteCount(json);

            string report = string.Format(
                "nodes={0} build={1}ms toJson={2}ms fromJson={3}ms validate={4}ms rebuild={5}ms json={6}KB diagnostics={7}",
                NodeCount, buildMs, toJsonMs, fromJsonMs, validateMs, rebuildMs,
                jsonBytes / 1024, diagnostics.Count);
            TestContext.WriteLine(report);
            UnityEngine.Debug.Log("[MythForge][perf] " + report);
            Assert.AreEqual(NodeCount, loaded.Nodes.Count);
            Assert.AreEqual(NodeCount - 1, loaded.Edges.Count);
            Assert.AreEqual(NodeCount, nodeViews);
            Assert.IsTrue(rebuildMs < 15000,
                "GraphView rebuild took " + rebuildMs + "ms for " + NodeCount + " nodes");
            Assert.IsTrue(validateMs < 3000,
                "Validate took " + validateMs + "ms for " + NodeCount + " nodes");
        }
    }
}
