using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;

namespace MythForge.Runtime
{
    /// <summary>One structured line from mf_vm_poll.</summary>
    public sealed class MfTraceLine
    {
        public string Type = string.Empty;
        public string Name = string.Empty;
        public string Node = string.Empty;
        public long AtMicros;
        public readonly List<object> Args = new List<object>();
        public string Raw = string.Empty;

        public static MfTraceLine Parse(string json)
        {
            var line = new MfTraceLine { Raw = json ?? string.Empty };
            object root = MfMiniJson.Parse(json);
            var members = root as Dictionary<string, object>;
            if (members == null) return line;
            line.Type = AsString(members, "type");
            line.Name = AsString(members, "name");
            line.Node = AsString(members, "node");
            object at;
            if (members.TryGetValue("at", out at))
            {
                if (at is long) line.AtMicros = (long)at;
                else if (at is double) line.AtMicros = (long)(double)at;
            }
            object args;
            if (members.TryGetValue("args", out args))
            {
                var list = args as List<object>;
                if (list != null) line.Args.AddRange(list);
            }
            return line;
        }

        private static string AsString(Dictionary<string, object> members, string key)
        {
            object value;
            return members.TryGetValue(key, out value) && value is string ? (string)value : string.Empty;
        }

        public override string ToString()
        {
            string args = Args.Count == 0 ? string.Empty : "(" + string.Join(", ", Args.ConvertAll(Format)) + ")";
            return string.Format("[{0,6}ms] {1} {2}{3}", AtMicros / 1000, Type, Name, args);
        }

        private static string Format(object value)
        {
            return value == null ? "nil" : Convert.ToString(value, CultureInfo.InvariantCulture);
        }
    }

    /// <summary>
    /// Minimal JSON reader for the SDK's flat trace lines. It intentionally
    /// supports only the types the ABI can emit; the editor's full MfJsonValue
    /// is not available to runtime assemblies.
    /// </summary>
    internal static class MfMiniJson
    {
        public static object Parse(string text)
        {
            int index = 0;
            object value = ParseValue(text ?? string.Empty, ref index);
            SkipWhitespace(text ?? string.Empty, ref index);
            return value;
        }

        private static object ParseValue(string text, ref int index)
        {
            SkipWhitespace(text, ref index);
            if (index >= text.Length) return null;
            char c = text[index];
            if (c == '{') return ParseObject(text, ref index);
            if (c == '[') return ParseArray(text, ref index);
            if (c == '"') return ParseString(text, ref index);
            if (c == 't' && Matches(text, index, "true")) { index += 4; return true; }
            if (c == 'f' && Matches(text, index, "false")) { index += 5; return false; }
            if (c == 'n' && Matches(text, index, "null")) { index += 4; return null; }
            return ParseNumber(text, ref index);
        }

        private static Dictionary<string, object> ParseObject(string text, ref int index)
        {
            var result = new Dictionary<string, object>(StringComparer.Ordinal);
            index++; // {
            while (index < text.Length)
            {
                SkipWhitespace(text, ref index);
                if (index < text.Length && text[index] == '}') { index++; break; }
                string key = ParseString(text, ref index);
                SkipWhitespace(text, ref index);
                if (index < text.Length && text[index] == ':') index++;
                result[key] = ParseValue(text, ref index);
                SkipWhitespace(text, ref index);
                if (index < text.Length && text[index] == ',') { index++; continue; }
                if (index < text.Length && text[index] == '}') { index++; break; }
            }
            return result;
        }

        private static List<object> ParseArray(string text, ref int index)
        {
            var result = new List<object>();
            index++; // [
            while (index < text.Length)
            {
                SkipWhitespace(text, ref index);
                if (index < text.Length && text[index] == ']') { index++; break; }
                result.Add(ParseValue(text, ref index));
                SkipWhitespace(text, ref index);
                if (index < text.Length && text[index] == ',') { index++; continue; }
                if (index < text.Length && text[index] == ']') { index++; break; }
            }
            return result;
        }

        private static string ParseString(string text, ref int index)
        {
            if (index >= text.Length || text[index] != '"') return string.Empty;
            index++;
            var builder = new StringBuilder();
            while (index < text.Length)
            {
                char c = text[index++];
                if (c == '"') break;
                if (c != '\\')
                {
                    builder.Append(c);
                    continue;
                }
                if (index >= text.Length) break;
                char escape = text[index++];
                switch (escape)
                {
                    case '"': builder.Append('"'); break;
                    case '\\': builder.Append('\\'); break;
                    case '/': builder.Append('/'); break;
                    case 'b': builder.Append('\b'); break;
                    case 'f': builder.Append('\f'); break;
                    case 'n': builder.Append('\n'); break;
                    case 'r': builder.Append('\r'); break;
                    case 't': builder.Append('\t'); break;
                    case 'u':
                        if (index + 4 <= text.Length)
                        {
                            int code;
                            if (int.TryParse(text.Substring(index, 4), NumberStyles.HexNumber,
                                    CultureInfo.InvariantCulture, out code))
                            {
                                builder.Append((char)code);
                            }
                            index += 4;
                        }
                        break;
                    default: builder.Append(escape); break;
                }
            }
            return builder.ToString();
        }

        private static object ParseNumber(string text, ref int index)
        {
            int start = index;
            if (index < text.Length && (text[index] == '-' || text[index] == '+')) index++;
            bool real = false;
            while (index < text.Length)
            {
                char c = text[index];
                if (c >= '0' && c <= '9') { index++; continue; }
                if (c == '.' || c == 'e' || c == 'E' || c == '-' || c == '+') { real = true; index++; continue; }
                break;
            }
            string token = text.Substring(start, index - start);
            long integer;
            if (!real && long.TryParse(token, NumberStyles.Integer, CultureInfo.InvariantCulture, out integer))
            {
                return integer;
            }
            double number;
            if (double.TryParse(token, NumberStyles.Float, CultureInfo.InvariantCulture, out number))
            {
                return number;
            }
            return 0L;
        }

        private static bool Matches(string text, int index, string token)
        {
            return index + token.Length <= text.Length &&
                   string.CompareOrdinal(text, index, token, 0, token.Length) == 0;
        }

        private static void SkipWhitespace(string text, ref int index)
        {
            while (index < text.Length && char.IsWhiteSpace(text[index])) index++;
        }
    }
}
