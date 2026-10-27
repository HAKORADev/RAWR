#include "platform_posix.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace rawr {
namespace platform {
namespace {

bool haveCommand(const std::string& name) {
    std::string which = name;
    const char* path = std::getenv("PATH");
    if (!path) return false;
    std::stringstream ss(path);
    std::string dir;
    while (std::getline(ss, dir, ':')) {
        if (dir.empty()) continue;
        std::string full = dir + "/" + name;
        std::ifstream probe(full, std::ios::binary);
        if (probe.good()) return true;
        std::ifstream exec(full + ".exe", std::ios::binary);
        if (exec.good()) return true;
    }
    return false;
}

} // namespace

bool is_static_build() {
#if defined(RAWR_STATIC_BUILD)
    return true;
#else
    return false;
#endif
}

std::vector<Dependency> check_build_dependencies() {
    std::vector<Dependency> deps;
    deps.push_back({"cmake", "configures the build", "apt-get install cmake", haveCommand("cmake")});
    deps.push_back({"g++", "C++17 compiler", "apt-get install g++", haveCommand("g++")});
    deps.push_back({"clang++", "alternative C++ compiler", "apt-get install clang", haveCommand("clang++")});
    deps.push_back({"git", "version control", "apt-get install git", haveCommand("git")});
    deps.push_back({"make", "build driver used by the default generator", "apt-get install make",
                    haveCommand("make")});
    deps.push_back({"ninja", "faster build driver", "apt-get install ninja-build", haveCommand("ninja")});
    return deps;
}

bool install_build_dependencies(std::vector<Dependency>& missing, std::string& err) {
    if (missing.empty()) return true;
    if (!haveCommand("apt-get")) {
        err = "this system has no apt-get; install the listed tools with your package manager";
        return false;
    }
    std::string packages;
    for (const Dependency& d : missing) {
        if (d.name == "g++") packages += " g++";
        else if (d.name == "cmake") packages += " cmake";
        else if (d.name == "clang++") packages += " clang";
        else if (d.name == "git") packages += " git";
        else if (d.name == "make") packages += " make";
        else if (d.name == "ninja") packages += " ninja-build";
    }
    if (packages.empty()) return true;

    std::string command = "apt-get update -qq && apt-get install -y -qq" + packages;
    int rc = std::system(command.c_str());
    if (rc != 0) {
        err = "apt-get failed while installing:" + packages +
              " (you may need to run it with elevated privileges)";
        return false;
    }
    return true;
}

} // namespace platform
} // namespace rawr
