#include "platform_win32.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace rawr {
namespace platform {
namespace {

bool haveCommand(const std::string& name) {
    const char* path = std::getenv("PATH");
    if (!path) return false;
    std::stringstream ss(path);
    std::string dir;
    while (std::getline(ss, dir, ';')) {
        if (dir.empty()) continue;
        for (const char* ext : {"", ".exe", ".cmd", ".bat"}) {
            std::string full = dir + "\\" + name + ext;
            std::ifstream probe(full, std::ios::binary);
            if (probe.good()) return true;
        }
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
    deps.push_back({"cmake", "configures the build", "winget install Kitware.CMake", haveCommand("cmake")});
    deps.push_back({"git", "version control", "winget install Git.Git", haveCommand("git")});
    deps.push_back({"ninja", "faster build driver", "winget install Ninja-build.Ninja", haveCommand("ninja")});
    deps.push_back({"cl", "MSVC compiler (Visual Studio Build Tools)",
                    "winget install Microsoft.VisualStudio.2022.BuildTools", haveCommand("cl")});
    return deps;
}

bool install_build_dependencies(std::vector<Dependency>& missing, std::string& err) {
    if (missing.empty()) return true;

    bool winget = haveCommand("winget");
    bool choco = haveCommand("choco");
    if (!winget && !choco) {
        err = "neither winget nor chocolatey was found; install the listed tools manually";
        return false;
    }

    for (const Dependency& d : missing) {
        std::string package;
        if (d.name == "cmake") package = "Kitware.CMake";
        else if (d.name == "git") package = "Git.Git";
        else if (d.name == "ninja") package = "Ninja-build.Ninja";
        else if (d.name == "cl") package = "Microsoft.VisualStudio.2022.BuildTools";
        else continue;

        std::string command;
        if (winget) {
            command = "winget install --id " + package +
                      " --accept-package-agreements --accept-source-agreements --silent";
        } else {
            command = "choco install " + package + " -y";
        }
        std::printf("  installing %s ...\n", d.name.c_str());
        int rc = std::system(command.c_str());
        if (rc != 0) {
            err = "the package manager failed while installing " + d.name;
            return false;
        }
    }
    return true;
}

} // namespace platform
} // namespace rawr
