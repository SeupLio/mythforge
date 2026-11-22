#include "mfvm/value.h"

namespace mfvm {

// Value is header-only today; this translation unit exists so the type has a
// stable home if helpers grow (hashing, boxed references) without touching
// every includer.
static_assert(sizeof(Value) == 16, "Value must stay 16 bytes");

}  // namespace mfvm
