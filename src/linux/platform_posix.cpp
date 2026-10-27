#include "platform_posix.h"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <unistd.h>

namespace rawr {
namespace platform {

bool is_static_build() {
#if defined(RAWR_STATIC_BUILD)
    return true;
#else
    return false;
#endif
}

std::string executable_path() {
    char buffer[4096];
    ssize_t n = ::readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (n <= 0) return std::string();
    buffer[n] = 0;
    return std::string(buffer);
}

void prepare_output() {
    // A terminal is always there on Linux.
}

} // namespace platform
} // namespace rawr
