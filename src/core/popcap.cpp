#include "popcap.h"

#include <cstring>

#include "fsutil.h"

namespace rawr {
namespace {

const char* kPayloadName = "popcapgame1.exe";
const char* kPartnerName = "partner.xml";

bool lowerEquals(const std::string& a, const char* b) {
    size_t n = std::strlen(b);
    if (a.size() != n) return false;
    for (size_t i = 0; i < n; ++i) {
        char c = a[i];
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (c != b[i]) return false;
    }
    return true;
}

std::string find_ci(const std::string& dir, const char* name) {
    if (!path_is_dir(dir)) return std::string();
    for (const std::string& entry : list_dir(dir)) {
        if (!path_is_file(entry)) continue;
        if (lowerEquals(path_basename(entry), name)) return entry;
    }
    return std::string();
}

// The files that are never the game: the uninstaller, the setup helper
// and PopCap's own protection tool.
bool isUtility(const std::string& name) {
    std::string low = path_basename(name);
    for (char& c : low)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return low == "unins000.exe" || low == "setup.exe" || low == "cdprotect.exe" ||
           low == "popcapgame1.exe" || low == "config.exe" || low == "launcher.exe";
}

} // namespace

bool detect_popcap(const std::string& dir, PopcapResult& out, std::string& err) {
    out = PopcapResult();
    if (!path_is_dir(dir)) {
        err = "not a directory";
        return false;
    }

    out.payloadPath = find_ci(dir, kPayloadName);
    out.partnerPath = find_ci(dir, kPartnerName);
    if (out.payloadPath.empty() && out.partnerPath.empty()) return true;

    // The stub is the other executable in the folder.
    for (const std::string& entry : list_dir(dir)) {
        if (!path_is_file(entry)) continue;
        if (path_extension(entry) != "exe" && path_extension(entry) != "EXE") continue;
        if (isUtility(entry)) continue;
        out.stubPath = entry;
        break;
    }

    out.detected = !out.payloadPath.empty();
    if (out.detected) {
        out.detail = out.stubPath.empty()
                         ? "the real game popcapgame1.exe is here, but no trial stub was found"
                         : "a trial stub fronts the real game popcapgame1.exe";
    }
    return true;
}

bool unwrap_popcap(const std::string& dir, bool dryRun, PopcapResult& out, std::string& err) {
    if (!detect_popcap(dir, out, err)) return false;
    if (!out.detected) return true;

    if (out.stubPath.empty()) {
        err = "the real game was found but there is no trial stub to replace";
        return false;
    }

    Bytes payload;
    if (!read_file(out.payloadPath, payload, err)) return false;

    if (!dryRun) {
        // Put the real game back under the game's own name, so every
        // shortcut and every save path keeps working.
        if (!write_file_atomic(out.stubPath, payload, err)) return false;
    }

    // Tell partner.xml the game is registered.
    if (!out.partnerPath.empty()) {
        Bytes partner;
        if (read_file(out.partnerPath, partner, err)) {
            std::string text(reinterpret_cast<const char*>(partner.data()), partner.size());
            const std::string from = "NoReg=\"true\"";
            const std::string to = "NoReg=\"false\"";
            size_t at = text.find(from);
            if (at != std::string::npos) {
                text.replace(at, from.size(), to);
                if (!dryRun) {
                    Bytes updated(text.begin(), text.end());
                    if (!write_file_atomic(out.partnerPath, updated, err)) return false;
                }
                out.partnerFlipped = true;
            } else {
                // Some packages use the single quoted form.
                size_t at2 = text.find("NoReg='true'");
                if (at2 != std::string::npos) {
                    text.replace(at2, std::strlen("NoReg='true'"), "NoReg='false'");
                    if (!dryRun) {
                        Bytes updated(text.begin(), text.end());
                        if (!write_file_atomic(out.partnerPath, updated, err)) return false;
                    }
                    out.partnerFlipped = true;
                }
            }
        }
    }

    if (!dryRun) {
        // The stub used to delete the payload when it exited. The payload
        // is now the game itself, so the copy is not needed any more.
        std::string rerr;
        remove_path(out.payloadPath, rerr);
    }

    out.unwrapped = true;
    out.detail = "the real game replaced the trial stub";
    return true;
}

} // namespace rawr
