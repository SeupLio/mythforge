using System.Collections.Generic;
using System.Linq;
using NUnit.Framework;

namespace MythForge.Editor.Tests
{
    public class SkillTimelineTests
    {
        [Test]
        public void CreateDocumentHasCastAndEnd()
        {
            MfGraphDocument document = MfSkillTimeline.CreateDocument("sk_new");
            Assert.AreEqual("skill", document.Kind);
            Assert.AreEqual(2, document.Nodes.Count);
            Assert.AreEqual("skill.cast", document.Nodes[0].Type);
            Assert.AreEqual("skill.end", document.Nodes[1].Type);
            Assert.AreEqual(1, document.Edges.Count);
        }

        [Test]
        public void FramesAreSortedByTime()
        {
            MfGraphDocument document = MfSkillTimeline.CreateDocument("sk_sort");
            MfSkillTimeline.AddFrame(document, 300);
            MfSkillTimeline.AddFrame(document, 100);
            MfSkillTimeline.AddFrame(document, 200);

            List<MfGraphNode> frames = MfSkillTimeline.Frames(document);
            Assert.AreEqual(3, frames.Count);
            Assert.AreEqual(100, MfSkillTimeline.TimeMs(frames[0]));
            Assert.AreEqual(200, MfSkillTimeline.TimeMs(frames[1]));
            Assert.AreEqual(300, MfSkillTimeline.TimeMs(frames[2]));
            Assert.AreEqual(300, MfSkillTimeline.DurationMs(document));
        }

        [Test]
        public void RebuildChainFollowsTimeOrder()
        {
            MfGraphDocument document = MfSkillTimeline.CreateDocument("sk_chain");
            MfSkillTimeline.AddFrame(document, 300);
            MfSkillTimeline.AddFrame(document, 100);
            List<MfGraphNode> frames = MfSkillTimeline.Frames(document);

            Assert.AreEqual(3, document.Edges.Count);
            Assert.AreEqual("skill.cast", document.FindNode(document.Edges[0].From).Type);
            Assert.AreEqual(frames[0].Id, document.Edges[0].To);
            Assert.AreEqual(frames[0].Id, document.Edges[1].From);
            Assert.AreEqual(frames[1].Id, document.Edges[1].To);
            Assert.AreEqual("skill.end", document.FindNode(document.Edges[2].To).Type);
        }

        [Test]
        public void MoveFrameReordersChain()
        {
            MfGraphDocument document = MfSkillTimeline.CreateDocument("sk_move");
            MfGraphNode first = MfSkillTimeline.AddFrame(document, 100);
            MfSkillTimeline.AddFrame(document, 200);
            Assert.IsTrue(MfSkillTimeline.MoveFrame(document, first.Id, 500));

            List<MfGraphNode> frames = MfSkillTimeline.Frames(document);
            Assert.AreEqual(500, MfSkillTimeline.TimeMs(frames[frames.Count - 1]));
            Assert.AreEqual(3, document.Edges.Count);
            Assert.AreEqual(frames[0].Id, document.Edges[0].To);
        }

        [Test]
        public void RemoveFrameRebuildsChain()
        {
            MfGraphDocument document = MfSkillTimeline.CreateDocument("sk_remove");
            MfGraphNode first = MfSkillTimeline.AddFrame(document, 100);
            MfSkillTimeline.AddFrame(document, 200);
            Assert.IsTrue(MfSkillTimeline.RemoveFrame(document, first.Id));
            Assert.AreEqual(2, document.Edges.Count);
            Assert.AreEqual(1, MfSkillTimeline.Frames(document).Count);
        }

        [Test]
        public void NewFramesUseSchemaDefaults()
        {
            MfGraphDocument document = MfSkillTimeline.CreateDocument("sk_defaults");
            MfGraphNode frame = MfSkillTimeline.AddFrame(document, 120);
            Assert.AreEqual("damage", frame.Props["event"].AsString());
            Assert.AreEqual(0, frame.Props["amount"].AsInt());
            Assert.AreEqual(120, frame.Props["timeMs"].AsInt());
        }

        [Test]
        public void JsonRoundTripKeepsKindAndFrames()
        {
            MfGraphDocument document = MfSkillTimeline.CreateDocument("sk_json");
            MfSkillTimeline.AddFrame(document, 100);
            string error;
            MfGraphDocument loaded = MfGraphDocument.FromJson(document.ToJson(), out error);
            Assert.IsNotNull(loaded, error);
            Assert.AreEqual("skill", loaded.Kind);
            Assert.AreEqual(3, loaded.Nodes.Count);
            Assert.AreEqual("skill.cast", loaded.Catalog.Entry.Id);
            Assert.AreEqual(0, loaded.Validate().Count(d => d.Severity == MfSeverity.Error));
        }

        [Test]
        public void ValidateReportsOutOfOrderFrames()
        {
            MfGraphDocument document = MfSkillTimeline.CreateDocument("sk_bad_order");
            MfGraphNode late = MfSkillTimeline.AddFrame(document, 300);
            MfGraphNode early = MfSkillTimeline.AddFrame(document, 100);
            // Force a hand-edited (invalid) chain: cast -> late -> early -> end.
            document.Edges.Clear();
            document.Edges.Add(new MfGraphEdge { From = document.Nodes[0].Id, To = late.Id });
            document.Edges.Add(new MfGraphEdge { From = late.Id, To = early.Id });
            document.Edges.Add(new MfGraphEdge { From = early.Id, To = document.Nodes[1].Id });

            List<MfDiagnostic> diagnostics = document.Validate();
            Assert.IsTrue(diagnostics.Any(d => d.Code == "MFD024"));
        }
    }
}
