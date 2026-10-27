// Reflexive Arcade wrapper detection and removal.
#pragma once

#include <string>
#include <vector>

#include "common.h"
#include "pe.h"

namespace rawr {

// How the folder looks.
enum class WrapperLayout {
    NotReflexive,     // nothing Reflexive here
    RwgWithConfig,    // wrapper stub + encrypted .RWG payload + config blob
    DirectExecutable, // the game exe is not wrapped, only the support folder is
    Integrated,       // the wrapper layer is compiled into the game exe itself
    Incomplete,       // Reflexive markers present but the payload is missing
};

struct ReflexiveLayout {
    WrapperLayout layout = WrapperLayout::NotReflexive;
    std::string reason;

    std::string gameDir;
    std::string wrapperExe;      // the wrapper stub (may be *.exe or *.exe.BAK)
    std::string targetExe;       // where the recovered game must be written
    std::string rwgPath;         // the encrypted payload
    std::string configPath;      // RAW_002.wdt / RAW_002.dat
    std::string raw3Path;        // RAW_003.wdt / RAW_003.dat
    std::string dllPath;         // ReflexiveArcade.dll
    std::vector<std::string> imagePaths;  // background.jpg + button*.jpg
    std::vector<std::string> junkPaths;   // wrapper leftovers to drop
};

struct UnwrapOptions {
    bool dryRun = false;
    bool restoreResources = true;
    bool keepTemp = false;
    bool verbose = false;
    bool quiet = false;
};

struct UnwrapResult {
    bool changed = false;
    bool alreadyClean = false;
    std::string gameExePath;
    std::string method;              // which seed derivation succeeded
    uint32_t seed1 = 0;
    uint32_t seed2 = 0;
    uint32_t entrySkip = 0;
    uint32_t regionStart = 0;
    uint32_t regionLength = 0;
    bool dotnet = false;
    bool resourcesRestored = false;
    size_t resourcesRestoredCount = 0;
    std::vector<std::string> removedPaths;
    std::vector<std::string> notes;
};

// Inspects a folder and works out what, if anything, can be removed.
bool detect_reflexive(const std::string& dir, ReflexiveLayout& out, std::string& err);

// Removes the wrapper from a detected folder.
bool unwrap_folder(const std::string& dir, const UnwrapOptions& options, UnwrapResult& result,
                   std::string& err);

// True when a binary carries Reflexive wrapper markers.
bool looks_like_reflexive_wrapper(const Bytes& data);

} // namespace rawr
