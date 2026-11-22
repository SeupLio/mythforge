using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;

namespace MythForge.Editor
{
    public enum MfJsonKind
    {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object
    }

    /// <summary>
    /// Minimal ordered JSON value. Object keys are kept in ordinal order so the
    /// editor writes byte-stable content (clean Git diffs), matching the C++
    /// pipeline's std::map based serializer.
    /// </summary>
    public sealed class MfJsonValue
    {
        private bool _bool;
        private double _number;
        private bool _isInteger;
        private long _integer;
        private string _string;
        private List<MfJsonValue> _array;
        private SortedDictionary<string, MfJsonValue> _object;

        public MfJsonKind Kind { get; private set; }

        private MfJsonValue(MfJsonKind kind)
        {
            Kind = kind;
        }

        public static MfJsonValue Null() { return new MfJsonValue(MfJsonKind.Null); }

        public static MfJsonValue Bool(bool value)
        {
            var v = new MfJsonValue(MfJsonKind.Bool);
            v._bool = value;
            return v;
        }

        public static MfJsonValue String(string value)
        {
            var v = new MfJsonValue(MfJsonKind.String);
            v._string = value ?? string.Empty;
            return v;
        }

        public static MfJsonValue Number(long value)
        {
            var v = new MfJsonValue(MfJsonKind.Number);
            v._isInteger = true;
            v._integer = value;
            v._number = value;
            return v;
        }

        public static MfJsonValue Number(double value)
        {
            var v = new MfJsonValue(MfJsonKind.Number);
            v._isInteger = false;
            v._number = value;
            return v;
        }

        public static MfJsonValue Array()
        {
            var v = new MfJsonValue(MfJsonKind.Array);
            v._array = new List<MfJsonValue>();
            return v;
        }

        public static MfJsonValue Object()
        {
            var v = new MfJsonValue(MfJsonKind.Object);
            v._object = new SortedDictionary<string, MfJsonValue>(StringComparer.Ordinal);
            return v;
        }

        public bool IsNull { get { return Kind == MfJsonKind.Null; } }
        public bool IsBool { get { return Kind == MfJsonKind.Bool; } }
        public bool IsNumber { get { return Kind == MfJsonKind.Number; } }
        public bool IsString { get { return Kind == MfJsonKind.String; } }
        public bool IsArray { get { return Kind == MfJsonKind.Array; } }
        public bool IsObject { get { return Kind == MfJsonKind.Object; } }

        public bool AsBool()
        {
            return Kind == MfJsonKind.Bool && _bool;
        }

        public long AsInt()
        {
            if (Kind != MfJsonKind.Number) return 0;
            return _isInteger ? _integer : (long)_number;
        }

        public double AsDouble()
        {
            if (Kind != MfJsonKind.Number) return 0.0;
            return _isInteger ? _integer : _number;
        }

        public string AsString()
        {
            return Kind == MfJsonKind.String ? _string : string.Empty;
        }

        public int Count
        {
            get
            {
                if (Kind == MfJsonKind.Array) return _array.Count;
                if (Kind == MfJsonKind.Object) return _object.Count;
                return 0;
            }
        }

        public IReadOnlyList<MfJsonValue> Items
        {
            get
            {
                if (Kind != MfJsonKind.Array)
                {
                    throw new InvalidOperationException("not an array");
                }
                return _array;
            }
        }

        public IEnumerable<KeyValuePair<string, MfJsonValue>> Members
        {
            get
            {
                if (Kind != MfJsonKind.Object)
                {
                    throw new InvalidOperationException("not an object");
                }
                return _object;
            }
        }

        public void Add(MfJsonValue value)
        {
            if (Kind != MfJsonKind.Array)
            {
                throw new InvalidOperationException("not an array");
            }
            _array.Add(value);
        }

        public MfJsonValue this[string key]
        {
            get
            {
                MfJsonValue value;
                if (Kind == MfJsonKind.Object && _object.TryGetValue(key, out value)) return value;
                return Null();
            }
            set
            {
                if (Kind != MfJsonKind.Object)
                {
                    throw new InvalidOperationException("not an object");
                }
                _object[key] = value ?? Null();
            }
        }

        public bool TryGet(string key, out MfJsonValue value)
        {
            value = null;
            if (Kind != MfJsonKind.Object) return false;
            return _object.TryGetValue(key, out value);
        }

        public string GetString(string key, string fallback = "")
        {
            MfJsonValue value;
            return TryGet(key, out value) && value.IsString ? value.AsString() : fallback;
        }

        public long GetInt(string key, long fallback = 0)
        {
            MfJsonValue value;
            return TryGet(key, out value) && value.IsNumber ? value.AsInt() : fallback;
        }

        public double GetDouble(string key, double fallback = 0.0)
        {
            MfJsonValue value;
            return TryGet(key, out value) && value.IsNumber ? value.AsDouble() : fallback;
        }

        public bool GetBool(string key, bool fallback = false)
        {
            MfJsonValue value;
            return TryGet(key, out value) && value.IsBool ? value.AsBool() : fallback;
        }

        public MfJsonValue Clone()
        {
            switch (Kind)
            {
                case MfJsonKind.Null: return Null();
                case MfJsonKind.Bool: return Bool(_bool);
                case MfJsonKind.String: return String(_string);
                case MfJsonKind.Number: return _isInteger ? Number(_integer) : Number(_number);
                case MfJsonKind.Array:
                {
                    var copy = Array();
                    for (int i = 0; i < _array.Count; i++) copy.Add(_array[i].Clone());
                    return copy;
                }
                case MfJsonKind.Object:
                {
                    var copy = Object();
                    foreach (var pair in _object) copy[pair.Key] = pair.Value.Clone();
                    return copy;
                }
            }
            return Null();
        }

        public bool DeepEquals(MfJsonValue other)
        {
            if (other == null || other.Kind != Kind) return false;
            switch (Kind)
            {
                case MfJsonKind.Null: return true;
                case MfJsonKind.Bool: return _bool == other._bool;
                case MfJsonKind.String: return string.Equals(_string, other._string, StringComparison.Ordinal);
                case MfJsonKind.Number:
                    if (_isInteger && other._isInteger) return _integer == other._integer;
                    return Math.Abs(_number - other._number) < 1e-12;
                case MfJsonKind.Array:
                    if (_array.Count != other._array.Count) return false;
                    for (int i = 0; i < _array.Count; i++)
                    {
                        if (!_array[i].DeepEquals(other._array[i])) return false;
                    }
                    return true;
                case MfJsonKind.Object:
                    if (_object.Count != other._object.Count) return false;
                    foreach (var pair in _object)
                    {
                        MfJsonValue otherValue;
                        if (!other._object.TryGetValue(pair.Key, out otherValue)) return false;
                        if (!pair.Value.DeepEquals(otherValue)) return false;
                    }
                    return true;
            }
            return false;
        }

        public string ToJson(bool pretty = true)
        {
            var builder = new StringBuilder();
            Write(builder, pretty ? 0 : -1);
            return builder.ToString();
        }

        public override string ToString()
        {
            return ToJson(false);
        }

        private void Write(StringBuilder builder, int indent)
        {
            switch (Kind)
            {
                case MfJsonKind.Null:
                    builder.Append("null");
                    break;
                case MfJsonKind.Bool:
                    builder.Append(_bool ? "true" : "false");
                    break;
                case MfJsonKind.Number:
                    if (_isInteger)
                    {
                        builder.Append(_integer.ToString(CultureInfo.InvariantCulture));
                    }
                    else
                    {
                        builder.Append(_number.ToString("R", CultureInfo.InvariantCulture));
                    }
                    break;
                case MfJsonKind.String:
                    WriteString(builder, _string);
                    break;
                case MfJsonKind.Array:
                {
                    if (_array.Count == 0)
                    {
                        builder.Append("[]");
                        break;
                    }
                    builder.Append('[');
                    for (int i = 0; i < _array.Count; i++)
                    {
                        if (i > 0) builder.Append(',');
                        NewLine(builder, indent + 1);
                        _array[i].Write(builder, indent < 0 ? -1 : indent + 1);
                    }
                    NewLine(builder, indent);
                    builder.Append(']');
                    break;
                }
                case MfJsonKind.Object:
                {
                    if (_object.Count == 0)
                    {
                        builder.Append("{}");
                        break;
                    }
                    builder.Append('{');
                    bool first = true;
                    foreach (var pair in _object)
                    {
                        if (!first) builder.Append(',');
                        first = false;
                        NewLine(builder, indent + 1);
                        WriteString(builder, pair.Key);
                        builder.Append(indent < 0 ? ":" : ": ");
                        pair.Value.Write(builder, indent < 0 ? -1 : indent + 1);
                    }
                    NewLine(builder, indent);
                    builder.Append('}');
                    break;
                }
            }
        }

        private static void NewLine(StringBuilder builder, int indent)
        {
            if (indent < 0) return;
            builder.Append('\n');
            builder.Append(' ', indent * 2);
        }

        private static void WriteString(StringBuilder builder, string value)
        {
            builder.Append('"');
            for (int i = 0; i < value.Length; i++)
            {
                char c = value[i];
                switch (c)
                {
                    case '"': builder.Append("\\\""); break;
                    case '\\': builder.Append("\\\\"); break;
                    case '\n': builder.Append("\\n"); break;
                    case '\r': builder.Append("\\r"); break;
                    case '\t': builder.Append("\\t"); break;
                    case '\b': builder.Append("\\b"); break;
                    case '\f': builder.Append("\\f"); break;
                    default:
                        if (c < 0x20)
                        {
                            builder.Append("\\u");
                            builder.Append(((int)c).ToString("x4", CultureInfo.InvariantCulture));
                        }
                        else
                        {
                            builder.Append(c);
                        }
                        break;
                }
            }
            builder.Append('"');
        }

        public static bool Parse(string text, out MfJsonValue value, out string error)
        {
            value = null;
            error = null;
            if (text == null)
            {
                error = "input is null";
                return false;
            }
            var parser = new Parser(text);
            return parser.Run(out value, out error);
        }

        private sealed class Parser
        {
            private readonly string _text;
            private int _pos;
            private int _line = 1;
            private int _col = 1;

            public Parser(string text)
            {
                _text = text;
            }

            public bool Run(out MfJsonValue value, out string error)
            {
                value = null;
                error = null;
                SkipWhitespace();
                if (!ParseValue(out value, 0))
                {
                    error = string.Format("line {0}, column {1}: {2}", _line, _col, _error);
                    return false;
                }
                SkipWhitespace();
                if (_pos != _text.Length)
                {
                    _error = "unexpected trailing characters";
                    error = string.Format("line {0}, column {1}: {2}", _line, _col, _error);
                    return false;
                }
                return true;
            }

            private string _error = "invalid JSON";

            private bool Fail(string message)
            {
                _error = message;
                return false;
            }

            private void SkipWhitespace()
            {
                while (_pos < _text.Length)
                {
                    char c = _text[_pos];
                    if (c == '\n')
                    {
                        _line++;
                        _col = 1;
                        _pos++;
                    }
                    else if (c == ' ' || c == '\t' || c == '\r')
                    {
                        _col++;
                        _pos++;
                    }
                    else
                    {
                        break;
                    }
                }
            }

            private bool ParseValue(out MfJsonValue value, int depth)
            {
                value = null;
                if (depth > 200) return Fail("nesting too deep");
                SkipWhitespace();
                if (_pos >= _text.Length) return Fail("unexpected end of input");
                char c = _text[_pos];
                if (c == '{') return ParseObject(out value, depth);
                if (c == '[') return ParseArray(out value, depth);
                if (c == '"')
                {
                    string s;
                    if (!ParseString(out s)) return false;
                    value = String(s);
                    return true;
                }
                if (c == 't')
                {
                    if (Match("true"))
                    {
                        value = Bool(true);
                        return true;
                    }
                    return Fail("invalid literal");
                }
                if (c == 'f')
                {
                    if (Match("false"))
                    {
                        value = Bool(false);
                        return true;
                    }
                    return Fail("invalid literal");
                }
                if (c == 'n')
                {
                    if (Match("null"))
                    {
                        value = Null();
                        return true;
                    }
                    return Fail("invalid literal");
                }
                if (c == '-' || (c >= '0' && c <= '9')) return ParseNumber(out value);
                return Fail("unexpected character");
            }

            private bool Match(string literal)
            {
                if (_pos + literal.Length > _text.Length) return false;
                if (string.CompareOrdinal(_text, _pos, literal, 0, literal.Length) != 0) return false;
                _pos += literal.Length;
                _col += literal.Length;
                return true;
            }

            private bool ParseObject(out MfJsonValue value, int depth)
            {
                value = Object();
                _pos++;
                _col++;
                SkipWhitespace();
                if (_pos < _text.Length && _text[_pos] == '}')
                {
                    _pos++;
                    _col++;
                    return true;
                }
                while (true)
                {
                    SkipWhitespace();
                    if (_pos >= _text.Length || _text[_pos] != '"') return Fail("expected object key");
                    string key;
                    if (!ParseString(out key)) return false;
                    SkipWhitespace();
                    if (_pos >= _text.Length || _text[_pos] != ':') return Fail("expected ':'");
                    _pos++;
                    _col++;
                    MfJsonValue member;
                    if (!ParseValue(out member, depth + 1)) return false;
                    value[key] = member;
                    SkipWhitespace();
                    if (_pos >= _text.Length) return Fail("unterminated object");
                    if (_text[_pos] == ',')
                    {
                        _pos++;
                        _col++;
                        continue;
                    }
                    if (_text[_pos] == '}')
                    {
                        _pos++;
                        _col++;
                        return true;
                    }
                    return Fail("expected ',' or '}'");
                }
            }

            private bool ParseArray(out MfJsonValue value, int depth)
            {
                value = Array();
                _pos++;
                _col++;
                SkipWhitespace();
                if (_pos < _text.Length && _text[_pos] == ']')
                {
                    _pos++;
                    _col++;
                    return true;
                }
                while (true)
                {
                    MfJsonValue item;
                    if (!ParseValue(out item, depth + 1)) return false;
                    value.Add(item);
                    SkipWhitespace();
                    if (_pos >= _text.Length) return Fail("unterminated array");
                    if (_text[_pos] == ',')
                    {
                        _pos++;
                        _col++;
                        continue;
                    }
                    if (_text[_pos] == ']')
                    {
                        _pos++;
                        _col++;
                        return true;
                    }
                    return Fail("expected ',' or ']'");
                }
            }

            private bool ParseString(out string value)
            {
                value = string.Empty;
                _pos++;
                _col++;
                var builder = new StringBuilder();
                while (_pos < _text.Length)
                {
                    char c = _text[_pos];
                    if (c == '"')
                    {
                        _pos++;
                        _col++;
                        value = builder.ToString();
                        return true;
                    }
                    if (c == '\\')
                    {
                        _pos++;
                        _col++;
                        if (_pos >= _text.Length) return Fail("unterminated escape");
                        char escape = _text[_pos];
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
                            {
                                if (_pos + 4 >= _text.Length) return Fail("bad \\u escape");
                                int code = 0;
                                for (int i = 1; i <= 4; i++)
                                {
                                    char h = _text[_pos + i];
                                    code <<= 4;
                                    if (h >= '0' && h <= '9') code |= h - '0';
                                    else if (h >= 'a' && h <= 'f') code |= h - 'a' + 10;
                                    else if (h >= 'A' && h <= 'F') code |= h - 'A' + 10;
                                    else return Fail("bad hex digit in \\u escape");
                                }
                                builder.Append((char)code);
                                _pos += 4;
                                _col += 4;
                                break;
                            }
                            default: return Fail("unknown escape");
                        }
                        _pos++;
                        _col++;
                        continue;
                    }
                    if (c == '\n') return Fail("newline in string");
                    builder.Append(c);
                    _pos++;
                    _col++;
                }
                return Fail("unterminated string");
            }

            private bool ParseNumber(out MfJsonValue value)
            {
                value = null;
                int start = _pos;
                bool isFloat = false;
                if (_text[_pos] == '-')
                {
                    _pos++;
                    _col++;
                }
                while (_pos < _text.Length)
                {
                    char c = _text[_pos];
                    if (c >= '0' && c <= '9')
                    {
                        _pos++;
                        _col++;
                    }
                    else if (c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-')
                    {
                        isFloat = true;
                        _pos++;
                        _col++;
                    }
                    else
                    {
                        break;
                    }
                }
                string token = _text.Substring(start, _pos - start);
                if (token.Length == 0) return Fail("invalid number");
                if (!isFloat)
                {
                    long integer;
                    if (long.TryParse(token, NumberStyles.Integer, CultureInfo.InvariantCulture, out integer))
                    {
                        value = Number(integer);
                        return true;
                    }
                }
                double number;
                if (!double.TryParse(token, NumberStyles.Float, CultureInfo.InvariantCulture, out number))
                {
                    return Fail("invalid number");
                }
                value = Number(number);
                return true;
            }
        }
    }
}
