// libFuzzer harness: module deserialization is the untrusted-input boundary
// (downloaded .mfb). Clang only:
//   cmake -S . -B build-fuzz -DMYTHFORGE_BUILD_FUZZERS=ON \
//         -DCMAKE_CXX_COMPILER=clang++
//   ./build-fuzz/native/fuzz_module -max_total_time=30
#include <cstdint>
#include <string>

#include "mfc/common.h"
#include "mfc/module.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  const std::string bytes(reinterpret_cast<const char*>(data), size);
  mfc::Module module;
  mfc::DiagnosticSink sink;
  if (mfc::deserializeModule(bytes, module, sink)) {
    mfc::verifyModule(module, sink);
  }
  return 0;
}
