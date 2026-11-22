using System;
using System.IO;
using NUnit.Framework;

namespace MythForge.Editor.Tests
{
    public class AgentIntegrationTests
    {
        [Test]
        public void AgentFixesBrokenSkillFile()
        {
            string path = Path.Combine(Path.GetTempPath(),
                "mythforge_agent_" + Guid.NewGuid().ToString("N") + ".json");
            const string broken = @"{
  ""schemaVersion"": 1,
  ""kind"": ""skill"",
  ""id"": ""sk_agent_test"",
  ""nodes"": [
    { ""id"": ""a"", ""type"": ""skill.frame"", ""props"": { ""timeMs"": 300 } },
    { ""id"": ""b"", ""type"": ""skill.frame"", ""props"": { ""timeMs"": -5, ""event"": ""explode"" } }
  ],
  ""edges"": []
}";
            File.WriteAllText(path, broken);
            try
            {
                MfAgentResult result = MfCli.AgentFix(path);
                Assert.IsEmpty(result.Error, result.RawOutput);
                Assert.IsTrue(result.Changed);
                Assert.GreaterOrEqual(result.FixedCount, 4);
                Assert.AreEqual(0, result.RemainingCount);

                MfCheckResult check = MfCli.Check(path);
                Assert.IsEmpty(check.Error, check.RawOutput);
                Assert.IsTrue(check.Ok, check.RawOutput);
            }
            finally
            {
                if (File.Exists(path)) File.Delete(path);
            }
        }

        [Test]
        public void AgentKeepsUnresolvableDesignErrors()
        {
            string path = Path.Combine(Path.GetTempPath(),
                "mythforge_agent_dialogue_" + Guid.NewGuid().ToString("N") + ".json");
            const string broken = @"{
  ""schemaVersion"": 1,
  ""kind"": ""dialogue"",
  ""id"": ""d_agent_test"",
  ""nodes"": [
    { ""id"": ""s"", ""type"": ""dialogue.start"", ""props"": {} },
    { ""id"": ""t"", ""type"": ""dialogue.timeline"", ""props"": {} },
    { ""id"": ""e"", ""type"": ""dialogue.end"", ""props"": {} }
  ],
  ""edges"": [
    { ""from"": ""s"", ""to"": ""t"" },
    { ""from"": ""t"", ""to"": ""e"" }
  ]
}";
            File.WriteAllText(path, broken);
            try
            {
                MfAgentResult result = MfCli.AgentFix(path);
                Assert.IsEmpty(result.Error, result.RawOutput);
                Assert.Greater(result.RemainingCount, 0);
                Assert.IsFalse(result.Ok);
            }
            finally
            {
                if (File.Exists(path)) File.Delete(path);
            }
        }
    }
}
