#include "mfc/json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace mfc::json {

Value Value::boolean(bool b) {
  Value v;
  v.kind_ = Kind::Bool;
  v.bool_ = b;
  return v;
}

Value Value::integer(int64_t i) {
  Value v;
  v.kind_ = Kind::Number;
  v.isInt_ = true;
  v.int_ = i;
  v.double_ = static_cast<double>(i);
  return v;
}

Value Value::number(double d) {
  Value v;
  v.kind_ = Kind::Number;
  v.isInt_ = false;
  v.double_ = d;
  return v;
}

Value Value::string(std::string s) {
  Value v;
  v.kind_ = Kind::String;
  v.string_ = std::move(s);
  return v;
}

Value Value::array() {
  Value v;
  v.kind_ = Kind::Array;
  return v;
}

Value Value::object() {
  Value v;
  v.kind_ = Kind::Object;
  return v;
}

const Value* Value::find(const std::string& key) const {
  if (kind_ != Kind::Object) return nullptr;
  auto it = object_.find(key);
  return it == object_.end() ? nullptr : &it->second;
}

Value& Value::operator[](const std::string& key) {
  if (kind_ != Kind::Object) {
    kind_ = Kind::Object;
  }
  return object_[key];
}

void Value::push(Value v) {
  if (kind_ != Kind::Array) {
    kind_ = Kind::Array;
  }
  array_.push_back(std::move(v));
}

bool Value::getString(const std::string& key, std::string* out) const {
  const Value* v = find(key);
  if (!v || !v->isString()) return false;
  *out = v->asString();
  return true;
}

bool Value::getInt(const std::string& key, int64_t* out) const {
  const Value* v = find(key);
  if (!v || !v->isNumber()) return false;
  *out = v->asInt();
  return true;
}

bool Value::getDouble(const std::string& key, double* out) const {
  const Value* v = find(key);
  if (!v || !v->isNumber()) return false;
  *out = v->asDouble();
  return true;
}

bool Value::getBool(const std::string& key, bool* out) const {
  const Value* v = find(key);
  if (!v || !v->isBool()) return false;
  *out = v->asBool();
  return true;
}

namespace {

class Parser {
 public:
  Parser(const std::string& text) : text_(text) {}

  bool run(Value& out, std::string* error) {
    skipWhitespace();
    if (!parseValue(out, 0)) {
      *error = error_;
      return false;
    }
    skipWhitespace();
    if (pos_ != text_.size()) {
      fail("unexpected trailing characters");
      *error = error_;
      return false;
    }
    return true;
  }

 private:
  bool fail(const std::string& message) {
    if (error_.empty()) {
      std::ostringstream os;
      os << "line " << line_ << ", column " << col_ << ": " << message;
      error_ = os.str();
    }
    return false;
  }

  bool skipWhitespace() {
    while (pos_ < text_.size()) {
      const char c = text_[pos_];
      if (c == '\n') {
        ++line_;
        col_ = 1;
        ++pos_;
      } else if (c == ' ' || c == '\t' || c == '\r') {
        ++col_;
        ++pos_;
      } else {
        break;
      }
    }
    return true;
  }

  bool parseValue(Value& out, int depth) {
    if (depth > 200) return fail("nesting too deep");
    skipWhitespace();
    if (pos_ >= text_.size()) return fail("unexpected end of input");
    const char c = text_[pos_];
    switch (c) {
      case '{': return parseObject(out, depth);
      case '[': return parseArray(out, depth);
      case '"': {
        std::string s;
        if (!parseString(s)) return false;
        out = Value::string(std::move(s));
        return true;
      }
      case 't':
        if (text_.compare(pos_, 4, "true") == 0) {
          pos_ += 4;
          col_ += 4;
          out = Value::boolean(true);
          return true;
        }
        return fail("invalid literal");
      case 'f':
        if (text_.compare(pos_, 5, "false") == 0) {
          pos_ += 5;
          col_ += 5;
          out = Value::boolean(false);
          return true;
        }
        return fail("invalid literal");
      case 'n':
        if (text_.compare(pos_, 4, "null") == 0) {
          pos_ += 4;
          col_ += 4;
          out = Value::null();
          return true;
        }
        return fail("invalid literal");
      default:
        if (c == '-' || (c >= '0' && c <= '9')) return parseNumber(out);
        return fail("unexpected character");
    }
  }

  bool parseObject(Value& out, int depth) {
    out = Value::object();
    ++pos_;
    ++col_;
    skipWhitespace();
    if (pos_ < text_.size() && text_[pos_] == '}') {
      ++pos_;
      ++col_;
      return true;
    }
    while (true) {
      skipWhitespace();
      if (pos_ >= text_.size() || text_[pos_] != '"') return fail("expected object key");
      std::string key;
      if (!parseString(key)) return false;
      skipWhitespace();
      if (pos_ >= text_.size() || text_[pos_] != ':') return fail("expected ':'");
      ++pos_;
      ++col_;
      Value v;
      if (!parseValue(v, depth + 1)) return false;
      out[key] = std::move(v);
      skipWhitespace();
      if (pos_ >= text_.size()) return fail("unterminated object");
      if (text_[pos_] == ',') {
        ++pos_;
        ++col_;
        continue;
      }
      if (text_[pos_] == '}') {
        ++pos_;
        ++col_;
        return true;
      }
      return fail("expected ',' or '}'");
    }
  }

  bool parseArray(Value& out, int depth) {
    out = Value::array();
    ++pos_;
    ++col_;
    skipWhitespace();
    if (pos_ < text_.size() && text_[pos_] == ']') {
      ++pos_;
      ++col_;
      return true;
    }
    while (true) {
      Value v;
      if (!parseValue(v, depth + 1)) return false;
      out.push(std::move(v));
      skipWhitespace();
      if (pos_ >= text_.size()) return fail("unterminated array");
      if (text_[pos_] == ',') {
        ++pos_;
        ++col_;
        continue;
      }
      if (text_[pos_] == ']') {
        ++pos_;
        ++col_;
        return true;
      }
      return fail("expected ',' or ']'");
    }
  }

  bool parseString(std::string& out) {
    ++pos_;  // opening quote
    ++col_;
    out.clear();
    while (pos_ < text_.size()) {
      const char c = text_[pos_];
      if (c == '"') {
        ++pos_;
        ++col_;
        return true;
      }
      if (c == '\\') {
        ++pos_;
        ++col_;
        if (pos_ >= text_.size()) return fail("unterminated escape");
        const char e = text_[pos_];
        switch (e) {
          case '"': out += '"'; break;
          case '\\': out += '\\'; break;
          case '/': out += '/'; break;
          case 'b': out += '\b'; break;
          case 'f': out += '\f'; break;
          case 'n': out += '\n'; break;
          case 'r': out += '\r'; break;
          case 't': out += '\t'; break;
          case 'u': {
            if (pos_ + 4 >= text_.size()) return fail("bad \\u escape");
            unsigned code = 0;
            for (int i = 1; i <= 4; ++i) {
              const char h = text_[pos_ + i];
              code <<= 4;
              if (h >= '0' && h <= '9') code |= static_cast<unsigned>(h - '0');
              else if (h >= 'a' && h <= 'f') code |= static_cast<unsigned>(h - 'a' + 10);
              else if (h >= 'A' && h <= 'F') code |= static_cast<unsigned>(h - 'A' + 10);
              else return fail("bad hex digit in \\u escape");
            }
            pos_ += 4;
            col_ += 4;
            appendUtf8(out, code);
            break;
          }
          default: return fail("unknown escape");
        }
        ++pos_;
        ++col_;
        continue;
      }
      if (c == '\n') return fail("newline in string");
      out += c;
      ++pos_;
      ++col_;
    }
    return fail("unterminated string");
  }

  static void appendUtf8(std::string& out, unsigned cp) {
    if (cp < 0x80) {
      out += static_cast<char>(cp);
    } else if (cp < 0x800) {
      out += static_cast<char>(0xC0 | (cp >> 6));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
      out += static_cast<char>(0xE0 | (cp >> 12));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    }
  }

  bool parseNumber(Value& out) {
    const size_t start = pos_;
    bool isFloat = false;
    if (text_[pos_] == '-') {
      ++pos_;
      ++col_;
    }
    while (pos_ < text_.size()) {
      const char c = text_[pos_];
      if (c >= '0' && c <= '9') {
        ++pos_;
        ++col_;
      } else if (c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') {
        isFloat = true;
        ++pos_;
        ++col_;
      } else {
        break;
      }
    }
    const std::string token = text_.substr(start, pos_ - start);
    if (token.empty()) return fail("invalid number");
    if (!isFloat) {
      errno = 0;
      char* end = nullptr;
      const long long v = std::strtoll(token.c_str(), &end, 10);
      if (errno != ERANGE && end && *end == '\0') {
        out = Value::integer(v);
        return true;
      }
    }
    char* end = nullptr;
    const double d = std::strtod(token.c_str(), &end);
    if (!end || *end != '\0') return fail("invalid number");
    out = Value::number(d);
    return true;
  }

  const std::string& text_;
  size_t pos_ = 0;
  int line_ = 1;
  int col_ = 1;
  std::string error_;
};

std::string escapeJsonString(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
          out += buf;
        } else {
          out += c;
        }
    }
  }
  return out;
}

void dumpValue(const Value& v, std::ostringstream& os, int indent, int depth) {
  const std::string pad(static_cast<size_t>(indent) * static_cast<size_t>(depth), ' ');
  const std::string padInner(static_cast<size_t>(indent) * static_cast<size_t>(depth + 1), ' ');
  switch (v.kind()) {
    case Value::Kind::Null: os << "null"; break;
    case Value::Kind::Bool: os << (v.asBool() ? "true" : "false"); break;
    case Value::Kind::Number:
      if (v.asInt() == v.asDouble() && std::llabs(v.asInt()) < (1LL << 53)) {
        os << v.asInt();
      } else {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.17g", v.asDouble());
        os << buf;
      }
      break;
    case Value::Kind::String: os << "\"" << escapeJsonString(v.asString()) << "\""; break;
    case Value::Kind::Array: {
      if (v.items().empty()) {
        os << "[]";
        break;
      }
      os << "[\n";
      for (size_t i = 0; i < v.items().size(); ++i) {
        os << padInner;
        dumpValue(v.items()[i], os, indent, depth + 1);
        os << (i + 1 == v.items().size() ? "\n" : ",\n");
      }
      os << pad << "]";
      break;
    }
    case Value::Kind::Object: {
      if (v.members().empty()) {
        os << "{}";
        break;
      }
      os << "{\n";
      size_t i = 0;
      for (const auto& kv : v.members()) {
        os << padInner << "\"" << escapeJsonString(kv.first) << "\": ";
        dumpValue(kv.second, os, indent, depth + 1);
        os << (++i == v.members().size() ? "\n" : ",\n");
      }
      os << pad << "}";
      break;
    }
  }
}

}  // namespace

bool parse(const std::string& text, Value& out, std::string* error) {
  Parser p(text);
  std::string err;
  if (!p.run(out, &err)) {
    if (error) *error = err;
    return false;
  }
  return true;
}

std::string dump(const Value& value, int indent) {
  std::ostringstream os;
  dumpValue(value, os, indent, 0);
  return os.str();
}

}  // namespace mfc::json
