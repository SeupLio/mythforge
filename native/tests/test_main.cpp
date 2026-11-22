#include "mf_test.h"

#include <iostream>

int main(int argc, char** argv) {
  const std::string filter = argc > 1 ? argv[1] : "";
  int failedTests = 0;
  int executed = 0;
  for (const auto& test : mftest::registry()) {
    if (!filter.empty() && test.name.find(filter) == std::string::npos) continue;
    ++executed;
    const int before = mftest::failures();
    std::cout << "[ RUN  ] " << test.name << "\n";
    test.fn();
    if (mftest::failures() != before) {
      std::cout << "[ FAIL ] " << test.name << "\n";
      ++failedTests;
    } else {
      std::cout << "[  OK  ] " << test.name << "\n";
    }
  }
  std::cout << "\n" << executed << " test(s), " << mftest::checks() << " check(s), "
            << failedTests << " failed test(s)\n";
  return failedTests == 0 ? 0 : 1;
}
