// Platform hooks used by the CLI. The core never talks to the OS directly.
#pragma once

#include <string>
#include <vector>

namespace rawr {
namespace platform {

// True when the running binary carries its dependencies with it.
bool is_static_build();

// The full path of the running executable, so the tool can drop its report
// next to itself.
std::string executable_path();

// Called before anything is printed. On the Windows shell build there is
// no console to print to, so the output is redirected to a log file next
// to the executable. On every other platform this does nothing.
void prepare_output();

} // namespace platform
} // namespace rawr
