#include "platform_win32.h"

#include <cstdio>
#include <cstdlib>
#include <windows.h>

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
    char buffer[MAX_PATH * 4];
    DWORD n = GetModuleFileNameA(nullptr, buffer, static_cast<DWORD>(sizeof(buffer)));
    if (n == 0 || n >= sizeof(buffer)) return std::string();
    return std::string(buffer, n);
}

void prepare_output() {
    // The shell build has no console of its own. If it was started from a
    // terminal, borrow it. If it was started from Explorer, write the
    // output to a log file next to the executable so the work is not
    // lost.
    if (GetConsoleWindow() != nullptr) return;
    if (AttachConsole(ATTACH_PARENT_PROCESS)) return;

    char exePath[MAX_PATH * 4];
    DWORD n = GetModuleFileNameA(nullptr, exePath, static_cast<DWORD>(sizeof(exePath)));
    std::string path;
    if (n > 0 && n < sizeof(exePath)) path.assign(exePath, n);

    std::string dir = ".";
    size_t slash = path.find_last_of("\\/");
    if (slash != std::string::npos) dir = path.substr(0, slash);

    SYSTEMTIME st;
    GetLocalTime(&st);
    char name[128];
    std::snprintf(name, sizeof(name), "rawr-output-%04d%02d%02d-%02d%02d%02d.txt", st.wYear,
                  st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    std::string logPath = dir + "\\" + name;

    FILE* f = freopen(logPath.c_str(), "w", stdout);
    if (f) setvbuf(stdout, nullptr, _IOLBF, 0);
    FILE* e = freopen(logPath.c_str(), "a", stderr);
    if (e) setvbuf(stderr, nullptr, _IOLBF, 0);
}

} // namespace platform
} // namespace rawr
