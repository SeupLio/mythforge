#pragma once

#include <string>
#include <vector>

#include "mfc/common.h"

namespace mfc {

// Event contracts used by quest scripts. The schema is intentionally small and
// table-driven: `wait event(Kill, target == "Wolf")` can only reference fields
// declared here, which lets sema reject typos at edit time instead of at runtime.
struct EventField {
  std::string name;
  Type type = Type::String;
};

struct EventSchema {
  std::string name;
  std::vector<EventField> fields;

  const EventField* findField(const std::string& fieldName) const {
    for (const auto& f : fields) {
      if (f.name == fieldName) return &f;
    }
    return nullptr;
  }
};

const std::vector<EventSchema>& builtinEvents();
const EventSchema* findEventSchema(const std::string& name);

}  // namespace mfc
