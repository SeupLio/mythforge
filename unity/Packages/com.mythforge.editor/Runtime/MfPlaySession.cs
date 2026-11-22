using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

namespace MythForge.Runtime
{
    /// <summary>
    /// Live-play/telemetry session used by the editor preview window and the
    /// demo game. It owns one VM, keeps a bounded trace history and optionally
    /// mirrors every line into a JSONL file (M10).
    /// </summary>
    public sealed class MfPlaySession : IDisposable
    {
        public const int MaxLines = 2000;

        private readonly MfNativeVm _vm = new MfNativeVm();
        private readonly List<MfTraceLine> _lines = new List<MfTraceLine>();

        public string ModulePath = string.Empty;
        public MfTelemetryWriter Telemetry;
        public event Action<MfTraceLine> LineAdded;

        public IReadOnlyList<MfTraceLine> Lines
        {
            get { return _lines; }
        }

        public int EmitCount { get; private set; }
        public int CallCount { get; private set; }
        public int FaultCount { get; private set; }
        public int WaitCount { get; private set; }
        public string LastNodeId { get; private set; }
        public string LastError
        {
            get { return _vm.LastError; }
        }
        public long NowMicros
        {
            get { return _vm.NowMicros; }
        }
        public bool Running
        {
            get { return _vm.Running; }
        }

        public bool Load(string modulePath, out string error)
        {
            error = null;
            if (!_vm.LoadModuleFile(modulePath, out error)) return false;
            ModulePath = modulePath;
            _lines.Clear();
            EmitCount = 0;
            CallCount = 0;
            FaultCount = 0;
            WaitCount = 0;
            LastNodeId = null;
            int started = _vm.Start();
            Drain();
            if (started < 1 && string.IsNullOrEmpty(LastError))
            {
                error = "模块没有可启动的入口 chunk（/start 或 /cast）";
                return false;
            }
            if (!string.IsNullOrEmpty(LastError))
            {
                error = LastError;
                return false;
            }
            return true;
        }

        public void Advance(long micros)
        {
            _vm.Advance(Math.Max(0, micros));
            Drain();
        }

        public bool Emit(string name, IDictionary<string, object> fields)
        {
            bool ok = _vm.Emit(name, fields);
            Drain();
            return ok;
        }

        public void Run()
        {
            _vm.Run();
            Drain();
        }

        public void Drain()
        {
            List<MfTraceLine> fresh = _vm.Poll();
            for (int i = 0; i < fresh.Count; i++)
            {
                MfTraceLine line = fresh[i];
                switch (line.Type)
                {
                    case "emit": EmitCount++; break;
                    case "call": CallCount++; break;
                    case "wait": WaitCount++; break;
                    case "fault": FaultCount++; break;
                }
                if (!string.IsNullOrEmpty(line.Node)) LastNodeId = line.Node;
                _lines.Add(line);
                if (Telemetry != null) Telemetry.Write(line);
                if (LineAdded != null) LineAdded(line);
            }
            if (_lines.Count > MaxLines)
            {
                _lines.RemoveRange(0, _lines.Count - MaxLines);
            }
        }

        public string RecentText(int maxLines)
        {
            int start = Math.Max(0, _lines.Count - maxLines);
            var builder = new StringBuilder();
            for (int i = start; i < _lines.Count; i++)
            {
                builder.AppendLine(_lines[i].ToString());
            }
            return builder.ToString();
        }

        public void Dispose()
        {
            _vm.Dispose();
        }
    }

    /// <summary>Appends structured trace lines as JSONL for offline analysis.</summary>
    public sealed class MfTelemetryWriter : IDisposable
    {
        private readonly StreamWriter _writer;
        private bool _closed;

        public string Path { get; private set; }
        public int LineCount { get; private set; }

        public MfTelemetryWriter(string path)
        {
            Path = path;
            string directory = System.IO.Path.GetDirectoryName(path);
            if (!string.IsNullOrEmpty(directory) && !Directory.Exists(directory))
            {
                Directory.CreateDirectory(directory);
            }
            _writer = new StreamWriter(path, false, new UTF8Encoding(false));
        }

        public void Write(MfTraceLine line)
        {
            if (_closed || line == null) return;
            _writer.WriteLine(line.Raw);
            _writer.Flush();
            LineCount++;
        }

        public void Dispose()
        {
            if (_closed) return;
            _closed = true;
            _writer.Dispose();
        }
    }
}
