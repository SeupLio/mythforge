using System.Collections.Generic;
using System.Linq;
using NUnit.Framework;
using UnityEditor.Experimental.GraphView;

namespace MythForge.Editor.Tests
{
    /// <summary>
    /// UI construction tests. They build the real GraphView and node views and
    /// assert the visual tree, ports, edges and diagnostic badges. This runs
    /// headless in batchmode, so UI regressions are caught by CI as well.
    /// </summary>
    public class GraphViewUiTests
    {
        private static MfGraphDocument BuildChain()
        {
            var document = new MfGraphDocument { Id = "q_ui" };
            MfGraphNode start = document.AddNode("quest.start", 0f, 0f);
            MfGraphNode kill = document.AddNode("quest.condition.kill", 220f, 0f);
            MfGraphNode end = document.AddNode("quest.end", 440f, 0f);
            string reason;
            Assert.IsTrue(document.Connect(start.Id, kill.Id, out reason), reason);
            Assert.IsTrue(document.Connect(kill.Id, end.Id, out reason), reason);
            return document;
        }

        [Test]
        public void BuildsNodeViewsAndEdges()
        {
            MfGraphDocument document = BuildChain();
            var view = new MfGraphView();
            view.Rebuild(document);

            Assert.AreEqual(3, view.NodeViews.Count());
            int edges = 0;
            foreach (GraphElement element in view.graphElements)
            {
                if (element is Edge) edges++;
            }
            Assert.AreEqual(2, edges);
        }

        [Test]
        public void NodesExposeTypedPorts()
        {
            MfGraphDocument document = BuildChain();
            var view = new MfGraphView();
            view.Rebuild(document);

            MfNodeView kill;
            Assert.IsTrue(view.TryGetNodeView(document.Nodes[1].Id, out kill));
            Assert.IsNotNull(kill.InputPort);
            Assert.IsNotNull(kill.OutputPort);
            Assert.AreEqual(1, kill.InputPort.connections.Count());
            Assert.AreEqual(1, kill.OutputPort.connections.Count());
            Assert.AreEqual(typeof(MfFlow), kill.InputPort.portType);
        }

        [Test]
        public void StartAndEndHaveSingleSidedPorts()
        {
            MfGraphDocument document = BuildChain();
            var view = new MfGraphView();
            view.Rebuild(document);

            MfNodeView start;
            MfNodeView end;
            Assert.IsTrue(view.TryGetNodeView(document.Nodes[0].Id, out start));
            Assert.IsTrue(view.TryGetNodeView(document.Nodes[2].Id, out end));
            Assert.IsNull(start.InputPort);
            Assert.IsNotNull(start.OutputPort);
            Assert.IsNotNull(end.InputPort);
            Assert.IsNull(end.OutputPort);
        }

        [Test]
        public void DiagnosticBadgeAppearsAndClears()
        {
            MfGraphDocument document = BuildChain();
            var view = new MfGraphView();
            view.Rebuild(document);
            MfNodeView kill;
            Assert.IsTrue(view.TryGetNodeView(document.Nodes[1].Id, out kill));

            kill.SetDiagnostics(new List<MfDiagnostic>
            {
                new MfDiagnostic
                {
                    Code = "MFD007",
                    Severity = MfSeverity.Error,
                    Message = "缺少必填属性",
                    NodeId = kill.Data.Id
                }
            });
            StringAssert.Contains("⚠", kill.title);

            kill.SetDiagnostics(new List<MfDiagnostic>());
            Assert.IsFalse(kill.title.Contains("⚠"));
        }

        [Test]
        public void BuildsEveryRegisteredNodeType()
        {
            var document = new MfGraphDocument { Id = "q_all" };
            for (int i = 0; i < MfQuestNodeCatalog.All.Count; i++)
            {
                Assert.IsNotNull(document.AddNode(MfQuestNodeCatalog.All[i].Id, i * 40f, 0f),
                    MfQuestNodeCatalog.All[i].Id);
            }
            var view = new MfGraphView();
            view.Rebuild(document);

            Assert.AreEqual(MfQuestNodeCatalog.All.Count, view.NodeViews.Count());
            for (int i = 0; i < document.Nodes.Count; i++)
            {
                MfNodeView nodeView;
                Assert.IsTrue(view.TryGetNodeView(document.Nodes[i].Id, out nodeView),
                    document.Nodes[i].Type);
                MfNodeTypeSpec spec = MfQuestNodeCatalog.Find(document.Nodes[i].Type);
                Assert.AreEqual(spec.MaxInputs > 0, nodeView.InputPort != null);
                Assert.AreEqual(spec.MaxOutputs > 0, nodeView.OutputPort != null);
            }
        }

        [Test]
        public void CompatiblePortsRejectSameDirectionAndSelf()
        {
            MfGraphDocument document = BuildChain();
            var view = new MfGraphView();
            view.Rebuild(document);
            MfNodeView start;
            MfNodeView kill;
            Assert.IsTrue(view.TryGetNodeView(document.Nodes[0].Id, out start));
            Assert.IsTrue(view.TryGetNodeView(document.Nodes[1].Id, out kill));

            List<Port> compatible = view.GetCompatiblePorts(start.OutputPort, null);
            Assert.IsTrue(compatible.Contains(kill.InputPort));
            Assert.IsFalse(compatible.Contains(start.OutputPort));
            Assert.IsFalse(compatible.Contains(kill.OutputPort));
        }

        [Test]
        public void FocusNodeSelectsIt()
        {
            MfGraphDocument document = BuildChain();
            var view = new MfGraphView();
            view.Rebuild(document);
            view.FocusNode(document.Nodes[1].Id);
            Assert.IsNotNull(view.SelectedNodeView());
            Assert.AreEqual(document.Nodes[1].Id, view.SelectedNodeView().Data.Id);
        }
    }
}
