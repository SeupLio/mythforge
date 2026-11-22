using System.IO;
using NUnit.Framework;
using MythForge.Runtime;

namespace MythForge.Editor.Tests
{
    /// <summary>
    /// M11 integration: one session drives the three demo modules through the
    /// native SDK exactly like the playable scene does.
    /// </summary>
    public class DemoSessionTests
    {
        private static string _root;

        [OneTimeSetUp]
        public void EnsureArtifacts()
        {
            _root = MfCli.FindProjectRoot();
            Assert.IsNotNull(_root);
            string outDir = Path.Combine(_root, "content", "out");
            if (!File.Exists(Path.Combine(outDir, "q_forest_wolves.mfb")) ||
                !File.Exists(Path.Combine(outDir, "d_elder_intro.mfb")) ||
                !File.Exists(Path.Combine(outDir, "sk_wolf_bite.mfb")))
            {
                MfProcessResult result = MfCli.Build(Path.Combine(_root, "content"));
                Assert.AreEqual(0, result.ExitCode, result.StdOut + result.StdErr);
            }
        }

        private static string Module(string id)
        {
            return Path.Combine(_root, "content", "out", id + ".mfb");
        }

        [Test]
        public void QuestDialogueAndSkillRunInOneSession()
        {
            using (var session = new MfDemoSession())
            {
                Assert.IsTrue(session.Load(Module("q_forest_wolves"), Module("d_elder_intro"),
                    Module("sk_wolf_bite")), session.Error);

                session.KillWolf();
                session.KillWolf();
                session.TalkElder();
                Assert.IsTrue(session.QuestCompleted);
                Assert.IsTrue(session.QuestLog.Count >= 2);

                session.DialogueAdvance();
                session.DialogueChoose();
                session.TimelineFinished(
                    "Assets/MythForgeContent/Timelines/TL_ElderIntro.playable");
                session.DialogueAdvance();
                session.Advance(1000);
                Assert.IsTrue(session.DialogueEnded);
                StringAssert.Contains("剧情结束", session.CurrentDialogueLine);

                session.Advance(1000);
                Assert.IsTrue(session.SkillFinished);
                StringAssert.Contains("sfx", session.LastSkillEvent);
            }
        }

        [Test]
        public void SessionReportsMissingModules()
        {
            using (var session = new MfDemoSession())
            {
                Assert.IsFalse(session.Load("missing_a.mfb", "missing_b.mfb", "missing_c.mfb"));
                Assert.IsNotEmpty(session.Error);
            }
        }
    }
}
