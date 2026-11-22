#pragma once

#include <string>

#include "mfc/common.h"
#include "mfc/ir.h"
#include "mfc/module.h"

namespace mfc {

// Lowers an optimized SSA function into a register-bytecode chunk:
//   * critical edges are split when a successor has phis
//   * phi nodes become parallel copies
//   * values are mapped 1:1 to frame slots (no spilling; frames live on the heap)
//   * jump offsets are patched and range-checked
bool generateChunk(const ir::Function& function, Chunk& out, DiagnosticSink& sink,
                   const std::string& file);

}  // namespace mfc
