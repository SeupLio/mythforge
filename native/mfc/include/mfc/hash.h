#pragma once

#include <cstdint>
#include <string>

namespace mfc {

// SHA-256 (FIPS 180-4), used for content hashing, build cache keys and the
// module checksum. Implemented locally so the pipeline has no third-party
// dependency and every byte of the format is explainable in an interview.
std::string sha256Hex(const std::string& data);
std::string sha256Hex(const void* data, size_t size);

// FNV-1a 64-bit: cheap non-cryptographic hash for hash maps and value numbers.
uint64_t fnv1a64(const std::string& data);
uint64_t fnv1a64(const void* data, size_t size);

}  // namespace mfc
