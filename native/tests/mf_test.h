#pragma once

#include <cmath>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace mftest {

struct TestCase {
  std::string name;
  std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
  static std::vector<TestCase> tests;
  return tests;
}

inline int& failures() {
  static int count = 0;
  return count;
}

inline int& checks() {
  static int count = 0;
  return count;
}

struct Registrar {
  Registrar(std::string name, std::function<void()> fn) {
    registry().push_back({std::move(name), std::move(fn)});
  }
};

inline void fail(const char* file, int line, const std::string& message) {
  ++failures();
  std::cout << "    " << file << ":" << line << ": " << message << "\n";
}

}  // namespace mftest

#define MF_TEST(name)                                        \
  static void name();                                        \
  static ::mftest::Registrar name##_registrar(#name, name);   \
  static void name()

#define MF_CHECK(cond)                                                          \
  do {                                                                          \
    ++::mftest::checks();                                                       \
    if (!(cond)) {                                                              \
      ::mftest::fail(__FILE__, __LINE__, "check failed: " #cond);               \
    }                                                                           \
  } while (0)

#define MF_CHECK_EQ(a, b)                                                       \
  do {                                                                          \
    ++::mftest::checks();                                                       \
    const auto& mf_va = (a);                                                     \
    const auto& mf_vb = (b);                                                     \
    if (!(mf_va == mf_vb)) {                                                    \
      ::mftest::fail(__FILE__, __LINE__, "expected " #a " == " #b);             \
    }                                                                           \
  } while (0)

#define MF_CHECK_NE(a, b)                                                       \
  do {                                                                          \
    ++::mftest::checks();                                                       \
    const auto& mf_va = (a);                                                     \
    const auto& mf_vb = (b);                                                     \
    if (mf_va == mf_vb) {                                                       \
      ::mftest::fail(__FILE__, __LINE__, "expected " #a " != " #b);             \
    }                                                                           \
  } while (0)

#define MF_CHECK_STR_EQ(a, b)                                                   \
  do {                                                                          \
    ++::mftest::checks();                                                       \
    const std::string mf_va = (a);                                               \
    const std::string mf_vb = (b);                                               \
    if (mf_va != mf_vb) {                                                       \
      ::mftest::fail(__FILE__, __LINE__,                                        \
                     "expected string equality: " #a " vs " #b "\n      got: " + \
                         mf_va + "\n      want: " + mf_vb);                     \
    }                                                                           \
  } while (0)

#define MF_CHECK_NEAR(a, b, eps)                                                \
  do {                                                                          \
    ++::mftest::checks();                                                       \
    const double mf_va = (a);                                                    \
    const double mf_vb = (b);                                                    \
    if (std::fabs(mf_va - mf_vb) > (eps)) {                                     \
      ::mftest::fail(__FILE__, __LINE__, "expected " #a " ~= " #b);             \
    }                                                                           \
  } while (0)
