using NUnit.Framework;

namespace MythForge.Editor.Tests
{
    public class NodeCatalogTests
    {
        [Test]
        public void ContainsAllQuestNodeTypes()
        {
            Assert.AreEqual(10, MfQuestNodeCatalog.All.Count);
            string[] expected =
            {
                "quest.start", "quest.end", "quest.condition.kill", "quest.condition.collect",
                "quest.condition.reach", "quest.condition.talk", "quest.timer", "quest.log",
                "quest.reward", "quest.depends"
            };
            for (int i = 0; i < expected.Length; i++)
            {
                Assert.IsNotNull(MfQuestNodeCatalog.Find(expected[i]), expected[i]);
            }
        }

        [Test]
        public void KillDefaultsMatchSchema()
        {
            MfNodeTypeSpec spec = MfQuestNodeCatalog.Find("quest.condition.kill");
            Assert.IsNotNull(spec);
            MfPropSpec target = spec.FindProp("target");
            MfPropSpec count = spec.FindProp("count");
            Assert.IsNotNull(target);
            Assert.IsNotNull(count);
            Assert.IsTrue(target.Required);
            Assert.AreEqual("Wolf", target.Default.AsString());
            Assert.AreEqual(1, count.Default.AsInt());
        }

        [Test]
        public void SummaryUsesNodeProps()
        {
            MfNodeTypeSpec spec = MfQuestNodeCatalog.Find("quest.condition.kill");
            var node = new MfGraphNode { Id = "n1", Type = spec.Id };
            node.Props["target"] = MfJsonValue.String("Boar");
            node.Props["count"] = MfJsonValue.Number(3);
            Assert.AreEqual("击杀 Boar ×3", spec.Summarize(node));
        }

        [Test]
        public void EveryTypeHasDisplayCategoryAndPorts()
        {
            for (int i = 0; i < MfQuestNodeCatalog.All.Count; i++)
            {
                MfNodeTypeSpec spec = MfQuestNodeCatalog.All[i];
                Assert.IsNotEmpty(spec.Display);
                Assert.IsNotEmpty(spec.Category);
                Assert.IsNotEmpty(spec.SearchKeys);
                Assert.LessOrEqual(spec.MinInputs, spec.MaxInputs);
                Assert.LessOrEqual(spec.MinOutputs, spec.MaxOutputs);
            }
        }
    }
}
