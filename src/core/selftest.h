// Built in self test. RAWR ships as a single binary with no test harness
// around it, so the binary has to be able to check itself.
#pragma once

#include <string>
#include <vector>

namespace rawr {

struct SelfTestResult {
    bool passed = false;
    int checks = 0;
    int failures = 0;
    std::vector<std::string> lines;
};

// Runs the built in checks. Returns the result; `passed` is true when every
// check succeeded.
SelfTestResult selftest_run();

} // namespace rawr
