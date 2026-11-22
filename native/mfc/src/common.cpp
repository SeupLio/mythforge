#include "mfc/common.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace mfc {

namespace {

std::string escapeJson(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
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

}  // namespace

const char* severityName(Severity s) {
  switch (s) {
    case Severity::Error: return "error";
    case Severity::Warning: return "warning";
    case Severity::Note: return "note";
  }
  return "error";
}

void DiagnosticSink::add(Diagnostic d) {
  if (d.severity == Severity::Error) {
    ++errorCount_;
  } else if (d.severity == Severity::Warning) {
    ++warningCount_;
  }
  diagnostics_.push_back(std::move(d));
}

void DiagnosticSink::error(std::string code, const Loc& loc, std::string message,
                           std::string nodeId, std::string hint, std::string file) {
  Diagnostic d;
  d.code = std::move(code);
  d.severity = Severity::Error;
  d.message = std::move(message);
  d.loc = loc;
  d.nodeId = std::move(nodeId);
  d.hint = std::move(hint);
  d.file = std::move(file);
  add(std::move(d));
}

void DiagnosticSink::warning(std::string code, const Loc& loc, std::string message,
                             std::string nodeId, std::string hint, std::string file) {
  Diagnostic d;
  d.code = std::move(code);
  d.severity = Severity::Warning;
  d.message = std::move(message);
  d.loc = loc;
  d.nodeId = std::move(nodeId);
  d.hint = std::move(hint);
  d.file = std::move(file);
  add(std::move(d));
}

void DiagnosticSink::clear() {
  diagnostics_.clear();
  errorCount_ = 0;
  warningCount_ = 0;
}

std::string DiagnosticSink::formatText() const {
  std::ostringstream os;
  for (const auto& d : diagnostics_) {
    os << (d.file.empty() ? "<content>" : d.file);
    if (d.loc.valid()) {
      os << ":" << d.loc.line << ":" << d.loc.col;
    }
    os << ": " << severityName(d.severity) << " " << d.code << ": " << d.message;
    if (!d.nodeId.empty()) {
      os << " [node " << d.nodeId << "]";
    }
    os << "\n";
    if (!d.hint.empty()) {
      os << "  hint: " << d.hint << "\n";
    }
  }
  return os.str();
}

std::string DiagnosticSink::formatJson() const {
  std::ostringstream os;
  os << "[";
  bool first = true;
  for (const auto& d : diagnostics_) {
    if (!first) os << ",";
    first = false;
    os << "{";
    os << "\"code\":\"" << escapeJson(d.code) << "\",";
    os << "\"severity\":\"" << severityName(d.severity) << "\",";
    os << "\"message\":\"" << escapeJson(d.message) << "\",";
    os << "\"file\":\"" << escapeJson(d.file) << "\",";
    os << "\"line\":" << d.loc.line << ",";
    os << "\"col\":" << d.loc.col << ",";
    os << "\"nodeId\":\"" << escapeJson(d.nodeId) << "\",";
    os << "\"hint\":\"" << escapeJson(d.hint) << "\"";
    os << "}";
  }
  os << "]";
  return os.str();
}

int editDistance(const std::string& a, const std::string& b) {
  const size_t n = a.size();
  const size_t m = b.size();
  if (n == 0) return static_cast<int>(m);
  if (m == 0) return static_cast<int>(n);
  std::vector<int> prev(m + 1), cur(m + 1);
  for (size_t j = 0; j <= m; ++j) prev[j] = static_cast<int>(j);
  for (size_t i = 1; i <= n; ++i) {
    cur[0] = static_cast<int>(i);
    for (size_t j = 1; j <= m; ++j) {
      const int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
      cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost});
    }
    std::swap(prev, cur);
  }
  return prev[m];
}

const char* typeName(Type t) {
  switch (t) {
    case Type::Void: return "void";
    case Type::Int: return "int";
    case Type::Float: return "float";
    case Type::Bool: return "bool";
    case Type::String: return "string";
    case Type::Error: return "<error>";
  }
  return "<error>";
}

bool isNumeric(Type t) { return t == Type::Int || t == Type::Float; }

bool isAssignable(Type from, Type to) {
  if (from == Type::Error || to == Type::Error) return true;
  if (from == to) return true;
  // int widens to float; nothing else is implicitly convertible.
  return from == Type::Int && to == Type::Float;
}

bool readFile(const std::string& path, std::string* out, std::string* error) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    if (error) *error = "cannot open '" + path + "'";
    return false;
  }
  std::ostringstream os;
  os << in.rdbuf();
  *out = os.str();
  return true;
}

bool writeFile(const std::string& path, const std::string& data, std::string* error) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    if (error) *error = "cannot write '" + path + "'";
    return false;
  }
  out.write(data.data(), static_cast<std::streamsize>(data.size()));
  if (!out) {
    if (error) *error = "write failed for '" + path + "'";
    return false;
  }
  return true;
}

}  // namespace mfc
