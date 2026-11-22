using System.Collections.Generic;
using System.Linq;
using NUnit.Framework;

namespace MythForge.Editor.Tests
{
    public class GraphDocumentTests
    {
        private static MfGraphDocument BuildValidDocument()
        {
            var document = new MfGraphDocument { Id = "q_test", Title = "Test" };
            MfGraphNode start = document.AddNode("quest.start", 10f, 20f);
            MfGraphNode kill = document.AddNode("quest.condition.kill", 240f, 20f);
            MfGraphNode end = document.AddNode("quest.end", 470f, 20f);
            string reason;
            Assert.IsTrue(document.Connect(start.Id, kill.Id, out reason), reason);
            Assert.IsTrue(document.Connect(kill.Id, end.Id, out reason), reason);
            return document;
        }

        [Test]
        public void CreateNewHasStartEndAndOneEdge()
        {
            MfGraphDocument document = MfGraphDocument.CreateNew("q_new");
            Assert.AreEqual(2, document.Nodes.Count);
            Assert.AreEqual(1, document.Edges.Count);
            Assert.AreEqual("quest.start", document.Nodes[0].Type);
            Assert.AreEqual("quest.end", document.Nodes[1].Type);
        }

        [Test]
        public void RoundTripPreservesLayoutAndProps()
        {
            MfGraphDocument document = BuildValidDocument();
            document.Nodes[1].Props["target"] = MfJsonValue.String("Boar");
            document.Nodes[1].Props["count"] = MfJsonValue.Number(3);
            document.Nodes[1].X = 123.5f;
            document.Nodes[1].Y = 77.25f;

            string json = document.ToJson();
            string error;
            MfGraphDocument loaded = MfGraphDocument.FromJson(json, out error);
            Assert.IsNotNull(loaded, error);
            Assert.AreEqual(document.Id, loaded.Id);
            Assert.AreEqual(document.Nodes.Count, loaded.Nodes.Count);
            Assert.AreEqual(document.Edges.Count, loaded.Edges.Count);
            MfGraphNode kill = loaded.FindNode(document.Nodes[1].Id);
            Assert.IsNotNull(kill);
            Assert.AreEqual("Boar", kill.Props["target"].AsString());
            Assert.AreEqual(3, kill.Props["count"].AsInt());
            Assert.AreEqual(123.5f, kill.X, 0.001f);
            Assert.AreEqual(77.25f, kill.Y, 0.001f);
        }

        [Test]
        public void ConnectRejectsSelfLoop()
        {
            MfGraphDocument document = BuildValidDocument();
            string reason;
            Assert.IsFalse(document.Connect(document.Nodes[0].Id, document.Nodes[0].Id, out reason));
            Assert.IsNotEmpty(reason);
        }

        [Test]
        public void ConnectRejectsDuplicateEdge()
        {
            MfGraphDocument document = BuildValidDocument();
            string reason;
            Assert.IsFalse(document.Connect(document.Nodes[0].Id, document.Nodes[1].Id, out reason));
        }

        [Test]
        public void ConnectRejectsSecondOutgoingEdge()
        {
            MfGraphDocument document = BuildValidDocument();
            string reason;
            Assert.IsFalse(document.Connect(document.Nodes[0].Id, document.Nodes[2].Id, out reason));
            StringAssert.Contains("线性", reason);
        }

        [Test]
        public void RemoveNodeRemovesIncidentEdges()
        {
            MfGraphDocument document = BuildValidDocument();
            string middle = document.Nodes[1].Id;
            Assert.IsTrue(document.RemoveNode(middle));
            Assert.AreEqual(0, document.Edges.Count);
            Assert.IsNull(document.FindNode(middle));
        }

        [Test]
        public void AddNodeGeneratesUniqueIds()
        {
            MfGraphDocument document = new MfGraphDocument();
            MfGraphNode a = document.AddNode("quest.condition.kill", 0, 0);
            MfGraphNode b = document.AddNode("quest.condition.kill", 0, 0);
            Assert.IsNotNull(a);
            Assert.IsNotNull(b);
            Assert.AreNotEqual(a.Id, b.Id);
        }

        [Test]
        public void ValidateAcceptsValidDocument()
        {
            MfGraphDocument document = BuildValidDocument();
            List<MfDiagnostic> diagnostics = document.Validate();
            Assert.AreEqual(0, diagnostics.Count(d => d.Severity == MfSeverity.Error),
                string.Join("\n", diagnostics.Select(d => d.ToDisplayString()).ToArray()));
        }

        [Test]
        public void ValidateReportsMissingRequiredProp()
        {
            MfGraphDocument document = BuildValidDocument();
            document.Nodes[1].Props.Remove("target");
            List<MfDiagnostic> diagnostics = document.Validate();
            Assert.IsTrue(diagnostics.Any(d => d.Code == "MFD007"));
        }

        [Test]
        public void ValidateReportsUnreachableNode()
        {
            MfGraphDocument document = BuildValidDocument();
            document.AddNode("quest.log", 0f, 0f);
            List<MfDiagnostic> diagnostics = document.Validate();
            Assert.IsTrue(diagnostics.Any(d => d.Code == "MFD017"));
        }

        [Test]
        public void ValidateReportsCycle()
        {
            MfGraphDocument document = BuildValidDocument();
            document.Edges.Add(new MfGraphEdge
            {
                From = document.Nodes[2].Id,
                To = document.Nodes[0].Id
            });
            List<MfDiagnostic> diagnostics = document.Validate();
            Assert.IsTrue(diagnostics.Any(d => d.Code == "MFD019"));
        }

        [Test]
        public void ValidateReportsInvalidQuestId()
        {
            MfGraphDocument document = BuildValidDocument();
            document.Id = "任务 1";
            List<MfDiagnostic> diagnostics = document.Validate();
            Assert.IsTrue(diagnostics.Any(d => d.Code == "MFD001"));
        }

        [Test]
        public void FromJsonRejectsDuplicateNodeIds()
        {
            const string json = "{\"id\":\"q\",\"nodes\":[{\"id\":\"n1\",\"type\":\"quest.start\"},{\"id\":\"n1\",\"type\":\"quest.end\"}]}";
            string error;
            Assert.IsNull(MfGraphDocument.FromJson(json, out error));
            StringAssert.Contains("duplicate", error);
        }
    }
}
