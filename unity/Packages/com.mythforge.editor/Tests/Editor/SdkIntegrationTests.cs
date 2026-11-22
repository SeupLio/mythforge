using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using NUnit.Framework;
using MythForge.Runtime;

namespace MythForge.Editor.Tests
{
    /// <summary>
    /// Cross-language integration: Unity P/Invoke -> mythforge.dll -> real VM.
    /// These tests run the same .mfb artifacts the CLI builds, so a regression in
    /// the ABI, marshaling or trace queue is caught without opening a window.
    /// </summary>
    public class SdkIntegrationTests
    {
        private static string _contentRoot;

        [OneTimeSetUp]
        public void BuildContentOnce()
        {
            string root = MfCli.FindProjectRoot();
            Assert.IsNotNull(root, "找不到仓库根目录");
            _contentRoot = Path.Combine(root, "content");
            string artifact = Path.Combine(_contentRoot, "out", "q_forest_wolves.mfb");
            if (!File.Exists(artifact))
            {
                MfProcessResult result = MfCli.Build(_contentRoot);
                Assert.AreEqual(0, result.ExitCode, result.StdOut + result.StdErr);
            }
            Assert.IsTrue(File.Exists(artifact), artifact);
        }

        private static string Artifact(string id)
        {
            return Path.Combine(_contentRoot, "out", id + ".mfb");
        }

        [Test]
        public void SdkVersionIsReported()
        {
            StringAssert.StartsWith("mythforge-sdk-", MfNativeLibrary.Version());
        }

        [Test]
        public void QuestRunsThroughNativeDll()
        {
            using (var session = new MfPlaySession())
            {
                string error;
                Assert.IsTrue(session.Load(Artifact("q_forest_wolves"), out error), error);
                Assert.IsTrue(session.Lines.Any(l => l.Name == "QuestStarted"));
                Assert.IsTrue(session.Running);

                Assert.IsTrue(session.Emit("Kill", new Dictionary<string, object> { { "target", "Wolf" } }));
                Assert.IsTrue(session.Emit("Kill", new Dictionary<string, object> { { "target", "Wolf" } }));
                Assert.IsTrue(session.Emit("Talk", new Dictionary<string, object> { { "npc", "Elder" } }));

                Assert.IsTrue(session.Lines.Any(l => l.Name == "QuestCompleted"));
                Assert.IsTrue(session.Lines.Any(l => l.Name == "GrantReward" && l.Type == "call"));
                Assert.IsFalse(session.Running);
                Assert.IsEmpty(session.LastError);
                Assert.IsTrue(session.Lines.Any(l => l.Node == "n_end"));
            }
        }

        [Test]
        public void DialogueRunsAndReportsTimelineCall()
        {
            using (var session = new MfPlaySession())
            {
                string error;
                Assert.IsTrue(session.Load(Artifact("d_elder_intro"), out error), error);
                Assert.IsTrue(session.Emit("DialogueAdvance", null));
                Assert.IsTrue(session.Emit("DialogueChoiceSelected", null));
                Assert.IsTrue(session.Lines.Any(l => l.Type == "call" && l.Name == "PlayTimeline"));
                Assert.IsTrue(session.Emit("TimelineFinished",
                    new Dictionary<string, object>
                    {
                        { "asset", "Assets/MythForgeContent/Timelines/TL_ElderIntro.playable" }
                    }));
                Assert.IsTrue(session.Emit("DialogueAdvance", null));
                session.Advance(600000);
                Assert.IsTrue(session.Lines.Any(l => l.Name == "DialogueEnded"));
                Assert.IsFalse(session.Running);
            }
        }

        [Test]
        public void SkillRunsOnVirtualClock()
        {
            using (var session = new MfPlaySession())
            {
                string error;
                Assert.IsTrue(session.Load(Artifact("sk_wolf_bite"), out error), error);
                Assert.IsTrue(session.Lines.Any(l => l.Name == "SkillCast"));
                session.Advance(300000);
                session.Advance(300000);
                Assert.IsTrue(session.Lines.Any(l => l.Name == "SkillEnded"));
                Assert.AreEqual(3, session.Lines.Count(l => l.Name == "SkillEvent"));
                Assert.IsFalse(session.Running);
            }
        }

        [Test]
        public void TelemetryWritesJsonLines()
        {
            string path = Path.Combine(Path.GetTempPath(), "mythforge_telemetry_" +
                                                        Guid.NewGuid().ToString("N") + ".jsonl");
            try
            {
                using (var session = new MfPlaySession())
                {
                    session.Telemetry = new MfTelemetryWriter(path);
                    string error;
                    Assert.IsTrue(session.Load(Artifact("q_forest_wolves"), out error), error);
                    Assert.IsTrue(session.Emit("Kill", new Dictionary<string, object> { { "target", "Wolf" } }));
                    session.Telemetry.Dispose();
                    session.Telemetry = null;
                }
                Assert.IsTrue(File.Exists(path));
                string[] lines = File.ReadAllLines(path);
                Assert.GreaterOrEqual(lines.Length, 3);
                Assert.IsTrue(lines.Any(l => l.Contains("\"type\": \"emit\"")));
                Assert.IsTrue(lines.Any(l => l.Contains("\"node\": \"n_start\"") ||
                                             l.Contains("\"node\": \"n_kill\"")));
            }
            finally
            {
                if (File.Exists(path)) File.Delete(path);
            }
        }

        [Test]
        public void TraceLineParserHandlesEscapesAndNumbers()
        {
            MfTraceLine line = MfTraceLine.Parse(
                "{\"type\": \"emit\", \"name\": \"DialogueLine\", \"node\": \"n_line\", \"at\": 125000, " +
                "\"args\": [\"Elder\", \"你好\\n旅行者\", 12, true, null]}");
            Assert.AreEqual("emit", line.Type);
            Assert.AreEqual("DialogueLine", line.Name);
            Assert.AreEqual("n_line", line.Node);
            Assert.AreEqual(125000, line.AtMicros);
            Assert.AreEqual(5, line.Args.Count);
            Assert.AreEqual("Elder", line.Args[0]);
            Assert.AreEqual("你好\n旅行者", line.Args[1]);
            Assert.AreEqual(12L, line.Args[2]);
            Assert.AreEqual(true, line.Args[3]);
            Assert.IsNull(line.Args[4]);
        }
    }
}
