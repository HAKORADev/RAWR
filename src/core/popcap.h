// The PopCap wrapper.
//
// PopCap shipped its games as a trial stub in front of the real game. The
// real game was dropped in the same folder as a hidden file called
// popcapgame1.exe, the stub launched that, and the stub deleted it again
// on exit. The trial state also lived in partner.xml as NoReg="true".
//
// The full game is therefore sitting right there: the wrapper is removed
// by putting the payload back under the game's own name and telling
// partner.xml that the game is registered.
//
// PopCap is long gone (EA bought them in 2011), so this is a preservation
// job rather than a trial lift.
#pragma once

#include <string>

#include "common.h"

namespace rawr {

struct PopcapResult {
    bool detected = false;
    bool unwrapped = false;
    std::string stubPath;       // the trial stub that was replaced
    std::string payloadPath;    // popcapgame1.exe
    std::string partnerPath;    // partner.xml
    bool partnerFlipped = false;
    std::string detail;
};

// Looks for the PopCap layout in a folder.
bool detect_popcap(const std::string& dir, PopcapResult& out, std::string& err);

// Replaces the stub with the real game and registers it in partner.xml.
bool unwrap_popcap(const std::string& dir, bool dryRun, PopcapResult& out, std::string& err);

} // namespace rawr
