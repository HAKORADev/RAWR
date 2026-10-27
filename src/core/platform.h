// Platform hooks used by the CLI. The core never talks to the OS directly.
#pragma once

#include <string>
#include <vector>

namespace rawr {
namespace platform {

// True when the running binary carries its dependencies with it.
bool is_static_build();

// Tools the project needs in order to build RAWR again from source.
struct Dependency {
    std::string name;
    std::string purpose;
    std::string installHint;
    bool present = false;
};

std::vector<Dependency> check_build_dependencies();

// Installs the missing build dependencies. Returns false with a reason when
// the platform cannot do it automatically.
bool install_build_dependencies(std::vector<Dependency>& missing, std::string& err);

} // namespace platform
} // namespace rawr
