using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

namespace MythForge.Runtime
{
    /// <summary>
    /// Thin, disposable wrapper over one native mf_vm. All execution semantics
    /// stay in C++; this class only loads a .mfb, forwards calls and parses the
    /// structured trace queue.
    /// </summary>
    public sealed class MfNativeVm : IDisposable
    {
        private const int ErrorBufferSize = 4096;
        private const int PollBufferSize = 64 * 1024;
        private IntPtr _vm;
        private readonly byte[] _error = new byte[ErrorBufferSize];
        private readonly byte[] _poll = new byte[PollBufferSize];

        public MfNativeVm()
        {
            MfNativeLibrary.EnsureLoaded();
            try
            {
                _vm = MfNativeLibrary.mf_vm_create();
            }
            catch (DllNotFoundException exception)
            {
                throw new DllNotFoundException(
                    "找不到 mythforge.dll。先执行 cmake --build build --target mythforge_sdk，或用环境变量 MYTHFORGE_SDK 指定完整路径。",
                    exception);
            }
            if (_vm == IntPtr.Zero) throw new InvalidOperationException("mf_vm_create 返回空指针");
        }

        public bool IsValid
        {
            get { return _vm != IntPtr.Zero; }
        }

        public bool LoadModuleFile(string path, out string error)
        {
            error = null;
            if (!IsValid || string.IsNullOrEmpty(path)) return false;
            byte[] pathBytes = Utf8Z(path);
            int ok = MfNativeLibrary.mf_vm_load_module_file(_vm, pathBytes, _error, _error.Length);
            if (ok == 0)
            {
                error = ReadError();
                return false;
            }
            return true;
        }

        public int Start()
        {
            return IsValid ? MfNativeLibrary.mf_vm_start(_vm) : -1;
        }

        public bool Emit(string name, IDictionary<string, object> fields)
        {
            if (!IsValid || string.IsNullOrEmpty(name)) return false;
            string json = SerializeFields(fields);
            return MfNativeLibrary.mf_vm_emit(_vm, Utf8Z(name), Utf8Z(json)) == 1;
        }

        public bool Advance(long micros)
        {
            return IsValid && MfNativeLibrary.mf_vm_advance_time(_vm, Math.Max(0, micros)) == 1;
        }

        public bool Run()
        {
            return IsValid && MfNativeLibrary.mf_vm_run(_vm) == 1;
        }

        public bool Running
        {
            get { return IsValid && MfNativeLibrary.mf_vm_running(_vm) == 1; }
        }

        public long NowMicros
        {
            get { return IsValid ? MfNativeLibrary.mf_vm_now_micros(_vm) : 0; }
        }

        public string LastError
        {
            get { return IsValid ? MfNativeLibrary.PtrToUtf8(MfNativeLibrary.mf_vm_last_error(_vm)) : "null vm"; }
        }

        /// <summary>Drains every queued trace line (oldest first).</summary>
        public List<MfTraceLine> Poll()
        {
            var lines = new List<MfTraceLine>();
            if (!IsValid) return lines;
            while (MfNativeLibrary.mf_vm_poll(_vm, _poll, _poll.Length) == 1)
            {
                int length = 0;
                while (length < _poll.Length && _poll[length] != 0) length++;
                lines.Add(MfTraceLine.Parse(Encoding.UTF8.GetString(_poll, 0, length)));
            }
            return lines;
        }

        public void Dispose()
        {
            if (_vm == IntPtr.Zero) return;
            MfNativeLibrary.mf_vm_destroy(_vm);
            _vm = IntPtr.Zero;
        }

        private string ReadError()
        {
            int length = 0;
            while (length < _error.Length && _error[length] != 0) length++;
            return length == 0 ? "unknown error" : Encoding.UTF8.GetString(_error, 0, length);
        }

        private static byte[] Utf8Z(string text)
        {
            byte[] bytes = Encoding.UTF8.GetBytes(text ?? string.Empty);
            var terminated = new byte[bytes.Length + 1];
            Buffer.BlockCopy(bytes, 0, terminated, 0, bytes.Length);
            return terminated;
        }

        private static string SerializeFields(IDictionary<string, object> fields)
        {
            if (fields == null || fields.Count == 0) return "{}";
            var builder = new StringBuilder();
            builder.Append('{');
            bool first = true;
            foreach (var pair in fields)
            {
                if (!first) builder.Append(',');
                first = false;
                builder.Append('"').Append(Escape(pair.Key)).Append("\":");
                builder.Append(SerializeValue(pair.Value));
            }
            builder.Append('}');
            return builder.ToString();
        }

        private static string SerializeValue(object value)
        {
            if (value == null) return "null";
            if (value is bool) return (bool)value ? "true" : "false";
            if (value is string) return "\"" + Escape((string)value) + "\"";
            if (value is int) return ((int)value).ToString(System.Globalization.CultureInfo.InvariantCulture);
            if (value is long) return ((long)value).ToString(System.Globalization.CultureInfo.InvariantCulture);
            if (value is float) return ((float)value).ToString("R", System.Globalization.CultureInfo.InvariantCulture);
            if (value is double) return ((double)value).ToString("R", System.Globalization.CultureInfo.InvariantCulture);
            return "\"" + Escape(value.ToString()) + "\"";
        }

        private static string Escape(string text)
        {
            var builder = new StringBuilder(text.Length);
            for (int i = 0; i < text.Length; i++)
            {
                char c = text[i];
                switch (c)
                {
                    case '"': builder.Append("\\\""); break;
                    case '\\': builder.Append("\\\\"); break;
                    case '\n': builder.Append("\\n"); break;
                    case '\r': builder.Append("\\r"); break;
                    case '\t': builder.Append("\\t"); break;
                    default: builder.Append(c); break;
                }
            }
            return builder.ToString();
        }
    }
}
