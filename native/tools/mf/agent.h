#pragma once

#include <string>

#include "mfc/common.h"
#include "mfc/json.h"

namespace mfagent {

struct FixReport {
  bool changed = false;
  int fixedCount = 0;
  int remainingErrors = 0;
  std::string text;                    // repaired graph JSON (if changed)
  mfc::json::Value report = mfc::json::Value::object();
};

// Applies deterministic, schema-driven repairs to a content graph. The agent
// never invents design data it cannot infer: missing enums/timeline assets are
// reported as remaining errors instead of being guessed.
FixReport fixGraphText(const std::string& text, const std::string& path,
                       const mfc::json::Value& schema, mfc::DiagnosticSink& sink);

}  // namespace mfagent
