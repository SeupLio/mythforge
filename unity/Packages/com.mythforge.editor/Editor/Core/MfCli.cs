using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Text;
using System.Threading.Tasks;
using UnityEngine;

namespace MythForge.Editor
{
    public sealed class MfProcessResult
    {
        public int ExitCode = -1;
        public string StdOut = string.Empty;
        public string StdErr = string.Empty;
        public bool TimedOut;
        public string Failure = string.Empty;
    }

    public sealed class MfCheckResult
    {
        public bool Ok;
        public List<MfDiagnostic> Diagnostics = new List<MfDiagnostic>();
        public string Error = string.Empty;
        public string RawOutput = string.Empty;
    }

    public sealed class MfSimResult
    {
        public bool Ok;
        public string Error = string.Empty;
        public List<string> Emits = new List<string>();
        public List<string> Calls = new List<string>();
        public List<MfDiagnostic> Diagnostics = new List<MfDiagnostic>();
        public string RawOutput = string.Empty;
    }

    public sealed class MfAgentResult
    {
        public bool Ok;
        public bool Changed;
        public int FixedCount;
        public int RemainingCount;
        public string Error = string.Empty;
        public string RawOutput = string.Empty;
    }

    /// <summary>
    /// Bridge to the native `mf` CLI. The editor never re-implements compilation:
    /// validation and preview always go through the same code path as CI.
    /// </summary>
    public static class MfCli
    {
        private const string SchemaRelative = "schema/node_types.json";

        public static string FindProjectRoot()
        {
            var directory = new DirectoryInfo(Application.dataPath);
            for (int depth = 0; depth < 10 && directory != null; depth++)
            {
                string candidate = Path.Combine(directory.FullName, SchemaRelative);
                if (File.Exists(candidate)) return directory.FullName;
                directory = directory.Parent;
            }
            return null;
        }

        public static string FindMfExecutable()
        {
            string configured = Environment.GetEnvironmentVariable("MYTHFORGE_MF");
            if (!string.IsNullOrEmpty(configured) && File.Exists(configured)) return configured;

            string root = FindProjectRoot();
            if (root != null)
            {
                string[] candidates =
                {
                    Path.Combine(root, "build", "native", "mf.exe"),
                    Path.Combine(root, "build", "native", "mf"),
                    Path.Combine(root, "out", "mf.exe")
                };
                for (int i = 0; i < candidates.Length; i++)
                {
                    if (File.Exists(candidates[i])) return candidates[i];
                }
            }
            return "mf";
        }

        public static string SchemaPath()
        {
            string root = FindProjectRoot();
            if (root == null) throw new InvalidOperationException(
                "找不到 MythForge 仓库根目录（缺少 schema/node_types.json）。请把 Unity 工程放在 mythforge/unity/DemoGame 下。");
            return Path.Combine(root, SchemaRelative);
        }

        public static string FindContentRoot(string graphPath)
        {
            if (string.IsNullOrEmpty(graphPath)) return null;
            var directory = new DirectoryInfo(Path.GetDirectoryName(Path.GetFullPath(graphPath)) ?? string.Empty);
            while (directory != null)
            {
                if (string.Equals(directory.Name, "quests", StringComparison.OrdinalIgnoreCase) ||
                    string.Equals(directory.Name, "dialogues", StringComparison.OrdinalIgnoreCase) ||
                    string.Equals(directory.Name, "skills", StringComparison.OrdinalIgnoreCase))
                {
                    return directory.Parent != null ? directory.Parent.FullName : directory.FullName;
                }
                directory = directory.Parent;
            }
            return Path.GetDirectoryName(Path.GetFullPath(graphPath));
        }

        public static string Quote(string value)
        {
            return "\"" + (value ?? string.Empty).Replace("\"", "\\\"") + "\"";
        }

        public static MfProcessResult Run(string arguments, int timeoutMs = 120000)
        {
            var result = new MfProcessResult();
            try
            {
                var startInfo = new ProcessStartInfo
                {
                    FileName = FindMfExecutable(),
                    Arguments = arguments,
                    WorkingDirectory = FindProjectRoot() ?? Environment.CurrentDirectory,
                    UseShellExecute = false,
                    RedirectStandardOutput = true,
                    RedirectStandardError = true,
                    CreateNoWindow = true,
                    StandardOutputEncoding = Encoding.UTF8,
                    StandardErrorEncoding = Encoding.UTF8
                };
                using (var process = Process.Start(startInfo))
                {
                    if (process == null)
                    {
                        result.Failure = "无法启动 mf";
                        return result;
                    }
                    Task<string> errorTask = Task.Run(() => process.StandardError.ReadToEnd());
                    result.StdOut = process.StandardOutput.ReadToEnd();
                    result.StdErr = errorTask.Result;
                    if (!process.WaitForExit(timeoutMs))
                    {
                        result.TimedOut = true;
                        try { process.Kill(); } catch { }
                    }
                    result.ExitCode = result.TimedOut ? -1 : process.ExitCode;
                }
            }
            catch (Exception exception)
            {
                result.Failure = exception.Message;
            }
            return result;
        }

        public static MfCheckResult Check(string graphPath)
        {
            var report = new MfCheckResult();
            if (string.IsNullOrEmpty(graphPath))
            {
                report.Error = "当前没有打开的任务文件";
                return report;
            }
            MfProcessResult process = Run("check " + Quote(graphPath) + " --schema " + Quote(SchemaPath()) + " --json");
            report.RawOutput = process.StdOut + process.StdErr;
            if (!string.IsNullOrEmpty(process.Failure))
            {
                report.Error = process.Failure;
                return report;
            }
            if (process.TimedOut)
            {
                report.Error = "mf check 超时";
                return report;
            }
            if (string.IsNullOrEmpty(process.StdOut))
            {
                report.Error = string.IsNullOrEmpty(process.StdErr) ? "mf check 没有输出" : process.StdErr;
                return report;
            }

            MfJsonValue root;
            string parseError;
            if (!MfJsonValue.Parse(process.StdOut, out root, out parseError))
            {
                report.Error = "无法解析 mf 输出: " + parseError + "\n" + process.StdOut;
                return report;
            }
            report.Ok = root.GetBool("ok");
            report.Diagnostics = ReadDiagnostics(root);
            if (!string.IsNullOrEmpty(root.GetString("error")))
            {
                report.Error = root.GetString("error");
            }
            return report;
        }

        public static MfSimResult Simulate(string graphPath, string eventsJson)
        {
            var report = new MfSimResult();
            if (string.IsNullOrEmpty(graphPath))
            {
                report.Error = "当前没有打开的任务文件";
                return report;
            }
            string tempPath = Path.Combine(Path.GetTempPath(),
                "mythforge_preview_" + Guid.NewGuid().ToString("N") + ".json");
            try
            {
                File.WriteAllText(tempPath, eventsJson, new UTF8Encoding(false));
                MfProcessResult process = Run("sim " + Quote(graphPath) + " --events " + Quote(tempPath) +
                                              " --schema " + Quote(SchemaPath()) + " --json");
                report.RawOutput = process.StdOut + process.StdErr;
                if (!string.IsNullOrEmpty(process.Failure))
                {
                    report.Error = process.Failure;
                    return report;
                }
                if (process.TimedOut)
                {
                    report.Error = "mf sim 超时";
                    return report;
                }
                MfJsonValue root;
                string parseError;
                if (!MfJsonValue.Parse(process.StdOut, out root, out parseError))
                {
                    report.Error = "无法解析 mf 输出: " + parseError + "\n" + process.StdOut;
                    return report;
                }
                report.Ok = root.GetBool("ok");
                report.Error = root.GetString("error");
                MfJsonValue emits;
                if (root.TryGet("emits", out emits) && emits.IsArray)
                {
                    for (int i = 0; i < emits.Items.Count; i++)
                    {
                        if (emits.Items[i].IsString) report.Emits.Add(emits.Items[i].AsString());
                    }
                }
                MfJsonValue calls;
                if (root.TryGet("calls", out calls) && calls.IsArray)
                {
                    for (int i = 0; i < calls.Items.Count; i++)
                    {
                        if (calls.Items[i].IsString) report.Calls.Add(calls.Items[i].AsString());
                    }
                }
                report.Diagnostics = ReadDiagnostics(root);
                return report;
            }
            catch (Exception exception)
            {
                report.Error = exception.Message;
                return report;
            }
            finally
            {
                try
                {
                    if (File.Exists(tempPath)) File.Delete(tempPath);
                }
                catch { }
            }
        }

        public static MfProcessResult Build(string contentRoot)
        {
            return Run("build " + Quote(contentRoot) + " --schema " + Quote(SchemaPath()));
        }

        /// <summary>Force-builds every module into an explicit output folder.</summary>
        public static MfProcessResult BuildTo(string contentRoot, string outDir)
        {
            return Run("build " + Quote(contentRoot) + " --out " + Quote(outDir) +
                       " --force --schema " + Quote(SchemaPath()));
        }

        /// <summary>
        /// Runs the deterministic repair agent and writes the fixed file. The
        /// agent only fills/coerces values it can infer; unsolved diagnostics
        /// stay visible so a designer (or a future LLM) can decide.
        /// </summary>
        public static MfAgentResult AgentFix(string graphPath)
        {
            var report = new MfAgentResult();
            if (string.IsNullOrEmpty(graphPath))
            {
                report.Error = "当前没有打开的内容文件";
                return report;
            }
            MfProcessResult process = Run("agent fix " + Quote(graphPath) + " --write --schema " +
                                          Quote(SchemaPath()) + " --json");
            report.RawOutput = process.StdOut + process.StdErr;
            if (!string.IsNullOrEmpty(process.Failure))
            {
                report.Error = process.Failure;
                return report;
            }
            if (process.TimedOut)
            {
                report.Error = "mf agent fix 超时";
                return report;
            }
            MfJsonValue root;
            string parseError;
            if (!MfJsonValue.Parse(process.StdOut, out root, out parseError))
            {
                report.Error = "无法解析 mf agent 输出: " + parseError + "\n" + process.StdOut;
                return report;
            }
            report.Ok = root.GetBool("ok");
            report.Changed = root.GetBool("changed");
            MfJsonValue fixedList;
            if (root.TryGet("fixed", out fixedList) && fixedList.IsArray)
            {
                report.FixedCount = fixedList.Items.Count;
            }
            MfJsonValue remaining;
            if (root.TryGet("remaining", out remaining) && remaining.IsArray)
            {
                report.RemainingCount = remaining.Items.Count;
            }
            return report;
        }

        public static List<MfDiagnostic> ReadDiagnostics(MfJsonValue root)
        {
            var diagnostics = new List<MfDiagnostic>();
            MfJsonValue array;
            if (!root.TryGet("diagnostics", out array) || !array.IsArray) return diagnostics;
            for (int i = 0; i < array.Items.Count; i++)
            {
                diagnostics.Add(MfDiagnostic.FromJson(array.Items[i]));
            }
            return diagnostics;
        }
    }
}
