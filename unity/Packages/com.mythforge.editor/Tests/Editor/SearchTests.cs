using NUnit.Framework;

namespace MythForge.Editor.Tests
{
    public class SearchTests
    {
        [Test]
        public void PinyinKeywordMatchesKillNode()
        {
            MfNodeTypeSpec spec = MfQuestNodeCatalog.Find("quest.condition.kill");
            Assert.IsTrue(MfSearch.MatchesNode("jisha", spec));
            Assert.IsTrue(MfSearch.MatchesNode("shas", spec));
        }

        [Test]
        public void ChineseDisplayNameMatches()
        {
            MfNodeTypeSpec spec = MfQuestNodeCatalog.Find("quest.condition.talk");
            Assert.IsTrue(MfSearch.MatchesNode("对话", spec));
        }

        [Test]
        public void FuzzySubsequenceMatches()
        {
            MfNodeTypeSpec spec = MfQuestNodeCatalog.Find("quest.condition.collect");
            Assert.IsTrue(MfSearch.MatchesNode("cll", spec));
        }

        [Test]
        public void EmptyQueryMatchesEverything()
        {
            for (int i = 0; i < MfQuestNodeCatalog.All.Count; i++)
            {
                Assert.IsTrue(MfSearch.MatchesNode(string.Empty, MfQuestNodeCatalog.All[i]));
            }
        }

        [Test]
        public void NonsenseDoesNotMatch()
        {
            MfNodeTypeSpec spec = MfQuestNodeCatalog.Find("quest.timer");
            Assert.IsFalse(MfSearch.MatchesNode("zzzzzz", spec));
        }
    }
}
