using System.Collections.Generic;
using System.Linq;
using NUnit.Framework;
using UnityEditor.Experimental.GraphView;
using UnityEngine;

namespace MythForge.Editor.Tests
{
    public class DialogueGraphTests
    {
        private static MfGraphDocument BuildDialogue()
        {
            MfGraphDocument document = MfGraphDocument.CreateNew("dialogue", "d_test");
            Assert.AreEqual(2, document.Nodes.Count);
            MfGraphNode start = document.Nodes[0];
            MfGraphNode end = document.Nodes[1];

            MfGraphNode line = document.AddNode("dialogue.line", 200f, 0f);
            line.Props["speaker"] = MfJsonValue.String("Elder");
            line.Props["text"] = MfJsonValue.String("你好");
            MfGraphNode choice = document.AddNode("dialogue.choice", 400f, 0f);
            choice.Props["options"] = MfJsonValue.String("是|否");
            MfGraphNode timeline = document.AddNode("dialogue.timeline", 600f, 0f);
            timeline.Props["asset"] = MfJsonValue.String("Assets/TL_Test.playable");

            document.Edges.Clear();
            document.Edges.Add(new MfGraphEdge { From = start.Id, To = line.Id });
            document.Edges.Add(new MfGraphEdge { From = line.Id, To = choice.Id });
            document.Edges.Add(new MfGraphEdge { From = choice.Id, To = timeline.Id });
            document.Edges.Add(new MfGraphEdge { From = timeline.Id, To = end.Id });
            return document;
        }

        [Test]
        public void CatalogContainsDialogueNodeTypes()
        {
            Assert.AreEqual(7, MfDialogueNodeCatalog.All.Count);
            string[] expected =
            {
                "dialogue.start", "dialogue.line", "dialogue.choice", "dialogue.flag",
                "dialogue.timeline", "dialogue.wait", "dialogue.end"
            };
            for (int i = 0; i < expected.Length; i++)
            {
                MfNodeTypeSpec spec = MfDialogueNodeCatalog.Find(expected[i]);
                Assert.IsNotNull(spec, expected[i]);
                Assert.IsNotEmpty(spec.Display);
                Assert.IsNotEmpty(spec.Category);
                Assert.IsNotEmpty(spec.SearchKeys);
            }
            Assert.AreEqual("dialogue.start", MfDialogueNodeCatalog.Catalog.Entry.Id);
            Assert.AreEqual("dialogue.end", MfDialogueNodeCatalog.Catalog.Exit.Id);
        }

        [Test]
        public void CreateNewUsesDialogueEntryAndExit()
        {
            MfGraphDocument document = MfGraphDocument.CreateNew("dialogue", "d_new");
            Assert.AreEqual("dialogue", document.Kind);
            Assert.AreEqual("dialogue.start", document.Nodes[0].Type);
            Assert.AreEqual("dialogue.end", document.Nodes[1].Type);
            Assert.AreEqual(1, document.Edges.Count);
        }

        [Test]
        public void ValidateAcceptsTimelineFlow()
        {
            List<MfDiagnostic> diagnostics = BuildDialogue().Validate();
            Assert.AreEqual(0, diagnostics.Count(d => d.Severity == MfSeverity.Error),
                string.Join("\n", diagnostics.Select(d => d.ToDisplayString()).ToArray()));
        }

        [Test]
        public void ValidateReportsEmptyTimelineAsset()
        {
            MfGraphDocument document = BuildDialogue();
            MfGraphNode timeline = document.Nodes.First(n => n.Type == "dialogue.timeline");
            timeline.Props["asset"] = MfJsonValue.String("");
            List<MfDiagnostic> diagnostics = document.Validate();
            Assert.IsTrue(diagnostics.Any(d => d.Code == "MFD022"));
        }

        [Test]
        public void ValidateReportsSingleChoiceOption()
        {
            MfGraphDocument document = BuildDialogue();
            MfGraphNode choice = document.Nodes.First(n => n.Type == "dialogue.choice");
            choice.Props["options"] = MfJsonValue.String("只有一个选项");
            List<MfDiagnostic> diagnostics = document.Validate();
            Assert.IsTrue(diagnostics.Any(d => d.Code == "MFD021"));
        }

        [Test]
        public void JsonRoundTripKeepsDialogueKind()
        {
            MfGraphDocument document = BuildDialogue();
            string error;
            MfGraphDocument loaded = MfGraphDocument.FromJson(document.ToJson(), out error);
            Assert.IsNotNull(loaded, error);
            Assert.AreEqual("dialogue", loaded.Kind);
            Assert.AreEqual("dialogue.start", loaded.Catalog.Entry.Id);
            Assert.IsNotNull(loaded.FindNode(document.Nodes[2].Id));
        }

        [Test]
        public void QuestNodesAreRejectedInDialogueDocument()
        {
            MfGraphDocument document = MfGraphDocument.CreateNew("dialogue", "d_kind");
            Assert.IsNull(document.AddNode("quest.condition.kill", 0f, 0f));
        }

        [Test]
        public void GraphViewBuildsDialogueNodes()
        {
            MfGraphDocument document = BuildDialogue();
            var view = new MfGraphView(MfDialogueNodeCatalog.Catalog);
            view.Rebuild(document);

            Assert.AreEqual(5, view.NodeViews.Count());
            int edges = 0;
            foreach (GraphElement element in view.graphElements)
            {
                if (element is Edge) edges++;
            }
            Assert.AreEqual(4, edges);
        }

        [Test]
        public void TimelineBridgeResolvesAssetsPaths()
        {
            string projectPath = MfTimelineBridge.ToProjectPath(
                Application.dataPath + "/MythForgeContent/Timelines/TL_Test.playable");
            Assert.AreEqual("Assets/MythForgeContent/Timelines/TL_Test.playable", projectPath);
        }
    }
}
