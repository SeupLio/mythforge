using System.IO;
using NUnit.Framework;
using UnityEngine;

namespace MythForge.Editor.Tests
{
    public class CliTests
    {
        [Test]
        public void ProjectRootIsDiscoverable()
        {
            string root = MfCli.FindProjectRoot();
            if (root == null) Assert.Ignore("Unity 工程不在 mythforge 仓库内");
            Assert.IsTrue(File.Exists(Path.Combine(root, "schema", "node_types.json")));
        }

        [Test]
        public void ContentRootIsFoundFromQuestFile()
        {
            string quest = Path.Combine(Application.dataPath, "MythForgeContent", "quests",
                "q_forest_wolves.json");
            if (!File.Exists(quest)) Assert.Ignore("示例任务不存在");
            string contentRoot = MfCli.FindContentRoot(quest);
            Assert.AreEqual(
                Path.GetFullPath(Path.Combine(Application.dataPath, "MythForgeContent")),
                Path.GetFullPath(contentRoot));
        }

        [Test]
        public void CheckRunsOnDemoQuest()
        {
            string mf = MfCli.FindMfExecutable();
            if (!File.Exists(mf)) Assert.Ignore("mf 尚未构建: " + mf);
            string quest = Path.Combine(Application.dataPath, "MythForgeContent", "quests",
                "q_forest_wolves.json");
            if (!File.Exists(quest)) Assert.Ignore("示例任务不存在");

            MfCheckResult result = MfCli.Check(quest);
            Assert.IsTrue(result.Ok, result.Error + "\n" + result.RawOutput);
            Assert.AreEqual(0, result.Diagnostics.Count);
        }

        [Test]
        public void CheckReportsDiagnosticsForBrokenQuest()
        {
            string mf = MfCli.FindMfExecutable();
            if (!File.Exists(mf)) Assert.Ignore("mf 尚未构建: " + mf);

            string temp = Path.Combine(Path.GetTempPath(), "mythforge_broken_" +
                                                       System.Guid.NewGuid().ToString("N") + ".json");
            try
            {
                File.WriteAllText(temp,
                    "{\"schemaVersion\":1,\"kind\":\"quest\",\"id\":\"q_bad\"," +
                    "\"nodes\":[{\"id\":\"s\",\"type\":\"quest.start\",\"props\":{}}," +
                    "{\"id\":\"k\",\"type\":\"quest.condition.kill\",\"props\":{\"count\":1}}," +
                    "{\"id\":\"e\",\"type\":\"quest.end\",\"props\":{}}]," +
                    "\"edges\":[{\"from\":\"s\",\"to\":\"k\"},{\"from\":\"k\",\"to\":\"e\"}]}");
                MfCheckResult result = MfCli.Check(temp);
                Assert.IsFalse(result.Ok);
                Assert.IsTrue(result.Diagnostics.Exists(d => d.Code == "MFC5021"),
                    result.RawOutput);
            }
            finally
            {
                if (File.Exists(temp)) File.Delete(temp);
            }
        }
    }
}
