using System;

namespace MythForge.Editor
{
    public enum MfSeverity
    {
        Error,
        Warning,
        Note
    }

    /// <summary>One diagnostic coming from the C# pre-check or from `mf check`.</summary>
    public sealed class MfDiagnostic
    {
        public string Code = string.Empty;
        public MfSeverity Severity = MfSeverity.Error;
        public string Message = string.Empty;
        public string File = string.Empty;
        public string NodeId = string.Empty;
        public string Hint = string.Empty;
        public int Line;
        public int Col;

        public static MfDiagnostic FromJson(MfJsonValue value)
        {
            var diagnostic = new MfDiagnostic();
            if (value == null || !value.IsObject) return diagnostic;
            diagnostic.Code = value.GetString("code");
            string severity = value.GetString("severity", "error");
            if (severity == "warning") diagnostic.Severity = MfSeverity.Warning;
            else if (severity == "note") diagnostic.Severity = MfSeverity.Note;
            diagnostic.Message = value.GetString("message");
            diagnostic.File = value.GetString("file");
            diagnostic.NodeId = value.GetString("nodeId");
            diagnostic.Hint = value.GetString("hint");
            diagnostic.Line = (int)value.GetInt("line");
            diagnostic.Col = (int)value.GetInt("col");
            return diagnostic;
        }

        public string ToDisplayString()
        {
            string prefix = Severity == MfSeverity.Error ? "E" : Severity == MfSeverity.Warning ? "W" : "N";
            string location = Line > 0 ? string.Format(" ({0}:{1})", Line, Col) : string.Empty;
            return string.Format("[{0}] {1}{2}: {3}", prefix, Code, location, Message);
        }
    }
}
