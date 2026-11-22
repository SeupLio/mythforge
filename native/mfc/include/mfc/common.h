#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mfc {

// Source position inside a content file or a MythScript file.
struct Loc {
  int line = 0;
  int col = 0;
  bool valid() const { return line > 0; }
};

enum class Severity { Error, Warning, Note };

// Every diagnostic carries a stable code so tooling (editor, CI, AI repair loop)
// can react without parsing English prose. Codes:
//   MFC0xxx  lexer
//   MFC1xxx  parser
//   MFC2xxx  semantic analysis
//   MFC3xxx  IR / SSA / optimization
//   MFC4xxx  codegen / module format
//   MFC5xxx  content graph / schema
struct Diagnostic {
  std::string code;
  Severity severity = Severity::Error;
  std::string message;
  Loc loc;
  std::string nodeId;  // graph node that produced the code, when known
  std::string hint;    // actionable fix suggestion
  std::string file;
};

class DiagnosticSink {
 public:
  void add(Diagnostic d);
  void error(std::string code, const Loc& loc, std::string message,
             std::string nodeId = {}, std::string hint = {}, std::string file = {});
  void warning(std::string code, const Loc& loc, std::string message,
               std::string nodeId = {}, std::string hint = {}, std::string file = {});

  const std::vector<Diagnostic>& all() const { return diagnostics_; }
  bool hasErrors() const { return errorCount_ > 0; }
  int errorCount() const { return errorCount_; }
  int warningCount() const { return warningCount_; }
  void clear();

  std::string formatText() const;
  std::string formatJson() const;

 private:
  std::vector<Diagnostic> diagnostics_;
  int errorCount_ = 0;
  int warningCount_ = 0;
};

const char* severityName(Severity s);

// Levenshtein distance used for "did you mean" hints.
int editDistance(const std::string& a, const std::string& b);

// A small value type system shared by sema and codegen.
enum class Type { Void, Int, Float, Bool, String, Error };

const char* typeName(Type t);
bool isNumeric(Type t);
bool isAssignable(Type from, Type to);

// Thin filesystem helpers. std::filesystem is used only in the CLI and build
// code, not in the runtime hot path.
bool readFile(const std::string& path, std::string* out, std::string* error);
bool writeFile(const std::string& path, const std::string& data, std::string* error);

}  // namespace mfc
