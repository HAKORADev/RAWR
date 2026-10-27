#include "rwg.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <set>

#include "crc32.h"
#include "fsutil.h"
#include "stream_cipher.h"

namespace rawr {
namespace {

namespace fs = std::filesystem;

constexpr uint32_t kMask32 = 0xFFFFFFFFu;
constexpr uint32_t kNativeEntrySkip = 5;
constexpr const char* kNativeEntrySkipMarker =
    "The Code Section is not long enough to encode with some initial skip bytes";

const char* kWrapperMarkers[] = {
    "ReflexiveArcade.dll",
    "ReflexiveGameWrapper",
    "radll_",
    "Reflexive Arcade",
    "ReflexiveArcade",
};

const char* kConfigMarkers[] = {"Application Name=", "Demo Time Seconds="};

const char* kSidecarNames[] = {
    "background.jpg", "button_hover.jpg", "button_normal.jpg", "button_pressed.jpg",
    "channel.dat", "raw_002.dat", "raw_002.wdt", "raw_003.dat", "raw_003.wdt",
    "raw_004.dat", "raw_004.wdt",
};

const char* kJunkNames[] = {"ra_games.png", "ra_trial_dialog.png", "wraperr.log"};

// Sections that a later wrapper generation bolted onto the stub. They are
// not part of the image the seed was computed over, so they must be left
// out of the CRC as well.
//
// The ".FFF" section holds the Reflexive glue code (radll_GetUnlockCode,
// VirtualProtect, ...). The older wrapper generation kept that code inside
// .text, so the seed was computed before the section existed. Including it
// changes the CRC and the configuration never decrypts.
const char* kWrapperOnlySections[] = {".FFF"};

const char* kPrimarySeedDeps[] = {
    "background.jpg", "button_hover.jpg", "button_normal.jpg", "button_pressed.jpg",
    "raw_003.dat", "raw_003.wdt",
};

const char* kSecondarySeedDeps[] = {
    "background.jpg", "button_hover.jpg", "button_normal.jpg", "button_pressed.jpg",
    "raw_003.dat", "raw_003.wdt", "channel.dat", "raw_004.dat", "raw_004.wdt",
};

bool contains(const Bytes& haystack, const char* needle) {
    size_t n = std::strlen(needle);
    if (n == 0 || haystack.size() < n) return false;
    for (size_t i = 0; i + n <= haystack.size(); ++i) {
        if (std::memcmp(haystack.data() + i, needle, n) == 0) return true;
    }
    return false;
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(::tolower(c)); });
    return s;
}

bool file_contains(const std::string& path, const char* needle) {
    Bytes data;
    std::string err;
    if (!read_file(path, data, err)) return false;
    return contains(data, needle);
}

std::map<std::string, std::string> parse_config(const Bytes& data) {
    std::map<std::string, std::string> config;
    std::string text(reinterpret_cast<const char*>(data.data()), data.size());
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t eq = line.find('=');
        if (eq != std::string::npos) {
            std::string key = line.substr(0, eq);
            std::string value = line.substr(eq + 1);
            auto trim = [](std::string& s) {
                size_t b = s.find_first_not_of(" \t");
                size_t e = s.find_last_not_of(" \t");
                s = (b == std::string::npos) ? std::string() : s.substr(b, e - b + 1);
            };
            trim(key);
            trim(value);
            config[key] = value;
        }
        if (end == text.size()) break;
        start = end + 1;
    }
    return config;
}

bool config_flag(const std::map<std::string, std::string>& config, const char* key) {
    auto it = config.find(key);
    if (it == config.end()) return false;
    std::string v = lower(it->second);
    return v == "1" || v == "true" || v == "yes";
}

bool config_has(const std::map<std::string, std::string>& config, const char* key) {
    return config.find(key) != config.end();
}

bool looks_like_config(const Bytes& data) {
    for (const char* marker : kConfigMarkers)
        if (!contains(data, marker)) return false;
    return true;
}

// CRC32 over the raw bytes of every section of a PE image, skipping the
// sections a later wrapper generation appended.
bool crc32_pe_sections(const Bytes& data, uint32_t& crc, std::string& err) {
    PeImage pe;
    if (!pe_parse(data, pe, err)) return false;
    for (const PeSection& s : pe.sections) {
        if (s.sizeOfRawData == 0) continue;
        bool wrapperOnly = false;
        for (const char* skip : kWrapperOnlySections)
            if (s.name == skip) wrapperOnly = true;
        if (wrapperOnly) continue;
        if (static_cast<size_t>(s.pointerToRawData) + s.sizeOfRawData > data.size()) return false;
        crc = crc32_update(crc, data.data() + s.pointerToRawData, s.sizeOfRawData);
    }
    return true;
}

// The wrapper image as the seed was computed over it: everything before the
// first wrapper-only section.
Bytes wrapper_image_prefix(const Bytes& data) {
    PeImage pe;
    std::string err;
    if (!pe_parse(data, pe, err)) return data;
    uint32_t limit = static_cast<uint32_t>(data.size());
    for (const PeSection& s : pe.sections) {
        for (const char* skip : kWrapperOnlySections) {
            if (s.name != skip) continue;
            if (s.pointerToRawData < limit) limit = s.pointerToRawData;
        }
    }
    if (limit >= data.size()) return data;
    return Bytes(data.begin(), data.begin() + limit);
}

uint32_t derive_seed2(const Bytes& encryptedConfig, const std::map<std::string, std::string>& config) {
    size_t dwordCount;
    if (config_has(config, "App Version String"))
        dwordCount = encryptedConfig.size() / 4;
    else
        dwordCount = encryptedConfig.size() & 3;
    uint32_t total = 0;
    for (size_t i = 0; i < dwordCount; ++i) {
        size_t o = i * 4;
        if (o + 4 > encryptedConfig.size()) break;
        uint32_t v = static_cast<uint32_t>(encryptedConfig[o]) |
                     (static_cast<uint32_t>(encryptedConfig[o + 1]) << 8) |
                     (static_cast<uint32_t>(encryptedConfig[o + 2]) << 16) |
                     (static_cast<uint32_t>(encryptedConfig[o + 3]) << 24);
        total = (total + v) & kMask32;
    }
    return total & kMask32;
}

struct SeedMaterial {
    bool ok = false;
    uint32_t seed = 0;
    std::string method;
    Bytes decryptedConfig;
};

bool try_seed(uint32_t seed, const Bytes& encryptedConfig, SeedMaterial& out, const char* method) {
    Bytes decrypted = stream_decrypt_copy(encryptedConfig.data(), encryptedConfig.size(), seed);
    if (!looks_like_config(decrypted)) return false;
    out.ok = true;
    out.seed = seed;
    out.method = method;
    out.decryptedConfig = std::move(decrypted);
    return true;
}

// Repacked installs seed from the payload size plus a subset of file sizes.
namespace {

// Visits every combination of `k` indices out of `n` in lexicographic order.
template <typename F>
bool for_each_subset(size_t n, size_t k, F f) {
    if (k > n) return false;
    std::vector<size_t> idx(k);
    for (size_t i = 0; i < k; ++i) idx[i] = i;
    while (true) {
        if (f(idx)) return true;
        if (k == 0) return false;
        // Find the rightmost slot that has not reached its maximum value.
        size_t i = k;
        while (i > 0 && idx[i - 1] == n - k + i - 1) --i;
        if (i == 0) return false;
        ++idx[i - 1];
        for (size_t j = i; j < k; ++j) idx[j] = idx[j - 1] + 1;
    }
}

} // namespace

bool seed_repack_additive(const std::vector<std::pair<std::string, uint64_t>>& pool,
                          uint64_t childSize, const Bytes& encryptedConfig, SeedMaterial& out) {
    size_t maxSubset = std::min<size_t>(6, pool.size());
    for (size_t subsetSize = 0; subsetSize <= maxSubset; ++subsetSize) {
        bool found = for_each_subset(pool.size(), subsetSize, [&](const std::vector<size_t>& idx) {
            uint64_t total = childSize;
            for (size_t i : idx) {
                if (i >= pool.size()) return false;
                total += pool[i].second;
            }
            uint32_t seed = static_cast<uint32_t>(total & kMask32);
            return try_seed(seed, encryptedConfig, out, "repack_additive_sizes");
        });
        if (found) return true;
    }
    return false;
}

// Entry point plausibility: the first bytes of a compiled program are almost
// never random. The list was collected from real Reflexive titles.
bool looks_like_entry_bytes(const uint8_t* p, size_t len) {
    if (len == 0) return false;
    if (p[0] == 0x55 && len >= 3 && p[1] == 0x8B && p[2] == 0xEC) return true;  // push ebp; mov ebp,esp
    static const uint8_t kFirstByteOk[] = {
        0x51, 0x52, 0x53, 0x55, 0x56, 0x57, 0x6A, 0x68, 0x83, 0x8B, 0x89, 0x90, 0xCC,
        0xE8, 0xE9, 0x33, 0x31, 0x29, 0x48, 0x50, 0x6B, 0xFF, 0x74, 0x75, 0x0F, 0x85,
    };
    for (uint8_t b : kFirstByteOk)
        if (p[0] == b) return true;
    return false;
}

struct Region {
    uint32_t start = 0;
    uint32_t length = 0;
    bool ok = false;
    std::string error;
};

Region native_region(const PeImage& pe, bool shortFixed, uint32_t entrySkip) {
    Region r;
    for (const PeSection& s : pe.sections) {
        uint32_t va = s.virtualAddress;
        uint32_t vsize = s.virtualSize;
        uint32_t rsize = s.sizeOfRawData;
        uint32_t span = std::max(vsize, rsize);
        if (pe.addressOfEntryPoint < va || pe.addressOfEntryPoint >= va + span) continue;
        uint32_t usable = std::min(vsize, rsize);
        uint32_t offsetInSection = pe.addressOfEntryPoint - va;
        if (offsetInSection > usable) {
            r.error = "entry point falls outside the usable raw range of its section";
            return r;
        }
        uint32_t rawStart = s.pointerToRawData + offsetInSection;
        uint32_t decryptLength = usable - offsetInSection;
        if (shortFixed) {
            if (decryptLength < 0x80) {
                r.error = "short fixed encryption region is too small";
                return r;
            }
            decryptLength = 0x80;
        }
        if (decryptLength <= entrySkip) {
            r.error = "encrypted region is too small for the entry skip";
            return r;
        }
        r.start = rawStart + entrySkip;
        r.length = decryptLength - entrySkip;
        r.ok = true;
        return r;
    }
    r.error = "entry point does not belong to any section";
    return r;
}

Region dotnet_region(const PeImage& pe) {
    Region r;
    uint32_t rva = pe.dataDirRva[14];
    uint32_t size = pe.dataDirSize[14];
    if (rva == 0 || size == 0) {
        r.error = "missing CLR COM descriptor";
        return r;
    }
    uint32_t cliOffset = pe_rva_to_offset(pe, rva);
    if (cliOffset == 0 || cliOffset + 16 > pe.data.size()) {
        r.error = "CLR header is not mapped";
        return r;
    }
    const Bytes& d = pe.data;
    uint16_t headerSize = static_cast<uint16_t>(d[cliOffset + 16] | (d[cliOffset + 17] << 8));
    uint32_t metadataRva = static_cast<uint32_t>(d[cliOffset + 8]) |
                           (static_cast<uint32_t>(d[cliOffset + 9]) << 8) |
                           (static_cast<uint32_t>(d[cliOffset + 10]) << 16) |
                           (static_cast<uint32_t>(d[cliOffset + 11]) << 24);
    uint32_t metadataOffset = pe_rva_to_offset(pe, metadataRva);
    if (metadataOffset == 0) {
        r.error = "CLR metadata is not mapped";
        return r;
    }
    uint32_t regionStart = cliOffset + headerSize;
    if (metadataOffset <= regionStart) {
        r.error = "CLR metadata precedes the CLR header";
        return r;
    }
    r.start = regionStart;
    r.length = metadataOffset - regionStart;
    r.ok = true;
    return r;
}

// Picks the wrapper's own icon group and the manifest / version info.
bool harvest_resources(const Bytes& wrapper, std::vector<ResourceLeaf>& out, size_t& count) {
    out.clear();
    count = 0;
    std::string err;
    PeImage pe;
    if (!pe_parse(wrapper, pe, err)) return false;
    std::vector<ResourceLeaf> leaves;
    if (!pe_read_resources(pe, leaves, err)) return false;
    if (leaves.empty()) return true;

    // Preferred group icon.
    const ResourceLeaf* preferred = nullptr;
    size_t bestCount = 0;
    for (const ResourceLeaf& leaf : leaves) {
        if (leaf.typeIsNamed || leaf.type != kRtGroupIcon) continue;
        size_t count = leaf.data.size() >= 6 ? (leaf.data[4] | (leaf.data[5] << 8)) : 0;
        bool namedMain = leaf.nameIsNamed && lower(leaf.nameString) == "mainicon";
        if (namedMain) {
            preferred = &leaf;
            break;
        }
        if (count > bestCount) {
            bestCount = count;
            preferred = &leaf;
        }
    }

    if (preferred) {
        // Collect the icon ids the group points at.
        std::set<uint16_t> iconIds;
        const Bytes& grp = preferred->data;
        if (grp.size() >= 6) {
            uint16_t n = static_cast<uint16_t>(grp[4] | (grp[5] << 8));
            for (uint16_t i = 0; i < n; ++i) {
                size_t off = 6 + static_cast<size_t>(i) * 14;
                if (off + 14 > grp.size()) break;
                uint16_t id = static_cast<uint16_t>(grp[off + 12] | (grp[off + 13] << 8));
                iconIds.insert(id);
            }
        }
        for (const ResourceLeaf& leaf : leaves) {
            if (leaf.typeIsNamed || leaf.type != kRtIcon) continue;
            if (!iconIds.empty() && iconIds.find(leaf.nameId) == iconIds.end()) continue;
            out.push_back(leaf);
            ++count;
        }
        out.push_back(*preferred);
        ++count;
    }

    // Manifest and version info travel with the game.
    for (const ResourceLeaf& leaf : leaves) {
        if (leaf.typeIsNamed) continue;
        if (leaf.type == kRtManifest || leaf.type == kRtVersion) {
            bool duplicate = false;
            for (const ResourceLeaf& existing : out) {
                if (existing.type == leaf.type && existing.nameId == leaf.nameId &&
                    existing.language == leaf.language) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) {
                out.push_back(leaf);
                ++count;
            }
        }
    }
    return true;
}

std::string stage_signature(const std::string& dir) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%08x", crc32_of(dir.data(), dir.size()));
    return std::string(buf);
}

// Case insensitive file lookup. Windows installs disagree about the case of
// their own support files ("Background.jpg" vs "background.jpg"), and the
// wrapper's seed depends on exactly which files were packaged.
std::string find_ci(const std::string& dir, const char* name) {
    if (dir.empty() || !path_is_dir(dir)) return std::string();
    std::string want = lower(name);
    for (const std::string& entry : list_dir(dir)) {
        if (!path_is_file(entry)) continue;
        if (lower(path_basename(entry)) == want) return entry;
    }
    return std::string();
}

// The same lookup, but the support file may sit either next to the game or
// inside the ReflexiveArcade folder, depending on the packaging.
std::string find_support(const std::string& gameDir, const std::string& reflexDir,
                          const char* name) {
    std::string p = find_ci(reflexDir, name);
    if (!p.empty()) return p;
    return find_ci(gameDir, name);
}

} // namespace

bool looks_like_reflexive_wrapper(const Bytes& data) {
    for (const char* marker : kWrapperMarkers)
        if (contains(data, marker)) return true;
    return false;
}

bool detect_reflexive(const std::string& dir, ReflexiveLayout& out, std::string& err) {
    (void)err;
    out = ReflexiveLayout();
    out.gameDir = dir;

    if (!path_is_dir(dir)) {
        out.reason = "not a directory";
        return true;
    }

    std::string reflexDir = path_join(dir, "ReflexiveArcade");
    out.dllPath = path_join(reflexDir, "ReflexiveArcade.dll");
    bool hasDll = path_is_file(out.dllPath);
    bool hasReflexDir = path_is_dir(reflexDir);

    // Config blob. It lives in the support folder in some packages and next
    // to the game in others.
    for (const char* name : {"RAW_002.wdt", "RAW_002.dat"}) {
        std::string p = find_support(dir, reflexDir, name);
        if (!p.empty()) { out.configPath = p; break; }
    }
    for (const char* name : {"RAW_003.wdt", "RAW_003.dat"}) {
        std::string p = find_support(dir, reflexDir, name);
        if (!p.empty()) { out.raw3Path = p; break; }
    }

    // Wrapper images.
    for (const char* name : {"background.jpg", "button_normal.jpg", "button_hover.jpg",
                             "button_pressed.jpg"}) {
        std::string p = find_support(dir, reflexDir, name);
        if (!p.empty()) out.imagePaths.push_back(p);
    }

    // Encrypted payload: a .RWG container, or the older RAW_001 blob.
    for (const std::string& entry : list_dir(dir)) {
        if (!path_is_file(entry)) continue;
        if (lower(path_extension(entry)) == "rwg") {
            if (out.rwgPath.empty()) out.rwgPath = entry;
        }
    }
    if (out.rwgPath.empty()) {
        for (const char* name : {"RAW_001.exe", "RAW_001.dat"}) {
            std::string p = find_ci(dir, name);
            if (!p.empty()) { out.rwgPath = p; break; }
        }
    }

    if (!hasDll && !hasReflexDir && out.rwgPath.empty()) {
        out.layout = WrapperLayout::NotReflexive;
        out.reason = "no Reflexive Arcade markers found";
        return true;
    }

    // Locate the wrapper stub among the top level executables. The recovered
    // game always replaces the stub, so the stub path is also the target.
    std::vector<std::string> exes;
    for (const std::string& entry : list_dir(dir)) {
        if (!path_is_file(entry)) continue;
        std::string ext = lower(path_extension(entry));
        if (ext == "exe" || ext == "bak") exes.push_back(entry);
    }
    std::string rwgStem = out.rwgPath.empty() ? std::string() : path_stem(out.rwgPath);

    std::string markerMatch, stemMatch, largest;
    uint64_t largestSize = 0;
    for (const std::string& exe : exes) {
        std::string ext = lower(path_extension(exe));
        if (ext != "exe" && ext != "bak") continue;
        if (markerMatch.empty() &&
            (file_contains(exe, "ReflexiveArcade.dll") || file_contains(exe, "ReflexiveGameWrapper")))
            markerMatch = exe;
        if (stemMatch.empty() && !rwgStem.empty() && path_stem(exe) == rwgStem) stemMatch = exe;
        uint64_t size = path_size(exe);
        if (size > largestSize) { largestSize = size; largest = exe; }
    }
    out.wrapperExe = !markerMatch.empty() ? markerMatch
                                          : (!stemMatch.empty() ? stemMatch : largest);
    out.targetExe = out.wrapperExe;
    if (out.targetExe.empty() && !out.rwgPath.empty())
        out.targetExe = path_join(dir, rwgStem + ".exe");

    Bytes wrapperExeData;
    if (!out.wrapperExe.empty()) {
        std::string rerr;
        read_file(out.wrapperExe, wrapperExeData, rerr);
    }

    // Wrapper leftovers. A top level copy of a support file is only a
    // leftover when it matches the one in the support folder; a game that
    // ships its own "background.jpg" keeps it.
    std::string supportDir = path_join(dir, "ReflexiveArcade");
    for (const char* name : kSidecarNames) {
        std::string p = find_ci(dir, name);
        if (p.empty()) continue;
        std::string inSupport = find_ci(supportDir, name);
        if (!inSupport.empty()) {
            Bytes a, b;
            std::string rerr;
            if (read_file(p, a, rerr) && read_file(inSupport, b, rerr) && a != b) continue;
        }
        out.junkPaths.push_back(p);
    }
    for (const char* name : kJunkNames) {
        std::string p = find_ci(dir, name);
        if (!p.empty()) out.junkPaths.push_back(p);
    }

    if (!out.rwgPath.empty() && !out.configPath.empty()) {
        out.layout = WrapperLayout::RwgWithConfig;
        out.reason = "wrapper stub with an encrypted payload";
        return true;
    }
    if (!out.rwgPath.empty()) {
        out.layout = WrapperLayout::Incomplete;
        out.reason = "encrypted payload found but the configuration blob is missing";
        return true;
    }
    if (hasDll || hasReflexDir) {
        // No payload and no configuration blob: the binding layer is
        // compiled into the game executable itself.
        if (!out.wrapperExe.empty() && looks_like_reflexive_wrapper(wrapperExeData)) {
            out.layout = WrapperLayout::Integrated;
            out.reason = "the Reflexive binding layer is compiled into the game executable";
            return true;
        }
        out.layout = WrapperLayout::DirectExecutable;
        out.reason = "Reflexive support folder without an encrypted payload";
        return true;
    }
    out.layout = WrapperLayout::NotReflexive;
    out.reason = "no Reflexive Arcade markers found";
    return true;
}

bool unwrap_folder(const std::string& dir, const UnwrapOptions& options, UnwrapResult& result,
                   std::string& err) {
    result = UnwrapResult();

    ReflexiveLayout layout;
    if (!detect_reflexive(dir, layout, err)) return false;

    if (layout.layout == WrapperLayout::NotReflexive) {
        result.alreadyClean = true;
        result.notes.push_back(layout.reason);
        return true;
    }
    if (layout.layout == WrapperLayout::Incomplete) {
        err = layout.reason;
        return false;
    }
    if (layout.layout == WrapperLayout::Integrated) {
        err = layout.reason +
              " - this layout is not unwrappable yet (see ROADMAP.md, integrated wrappers)";
        return false;
    }

    // ---------------------------------------------------------------
    // Crash recovery: a previous run may have died mid flight.
    // ---------------------------------------------------------------
    std::string stageDir = path_join(dir, ".rawr-stage-" + stage_signature(dir));
    std::string stageMarker = path_join(stageDir, "RAWR-STAGE");
    if (path_is_dir(stageDir) && path_is_file(stageMarker)) {
        Bytes marker;
        std::string merr;
        if (read_file(stageMarker, marker, merr)) {
            std::string list(reinterpret_cast<const char*>(marker.data()), marker.size());
            size_t start = 0;
            while (start < list.size()) {
                size_t end = list.find('\n', start);
                if (end == std::string::npos) end = list.size();
                std::string rel = list.substr(start, end - start);
                if (!rel.empty()) {
                    std::string staged = path_join(stageDir, rel);
                    std::string target = path_join(dir, rel);
                    if (path_is_file(staged)) {
                        Bytes data;
                        if (read_file(staged, data, merr)) write_file_atomic(target, data, merr);
                    }
                }
                if (end == list.size()) break;
                start = end + 1;
            }
            result.notes.push_back("restored an interrupted previous run from the stage folder");
        }
        remove_path(stageDir, merr);
    }

    if (layout.layout == WrapperLayout::DirectExecutable) {
        // Nothing is encrypted: the only wrapper artefact left is the
        // support folder. Drop it, but only when the game does not still
        // import the wrapper library.
        if (!layout.wrapperExe.empty() && file_contains(layout.wrapperExe, "ReflexiveArcade.dll")) {
            err = "the game still links ReflexiveArcade.dll; refusing to remove the support folder";
            return false;
        }
        std::vector<std::string> removals;
        std::string reflexDir = path_join(dir, "ReflexiveArcade");
        if (path_is_dir(reflexDir)) removals.push_back(reflexDir);
        for (const std::string& p : layout.junkPaths) removals.push_back(p);
        if (removals.empty()) {
            result.alreadyClean = true;
            result.notes.push_back("support folder already removed");
            return true;
        }
        result.notes.push_back("game executable was not wrapped; only the support folder is removed");
        if (options.dryRun) {
            for (const std::string& p : removals) result.removedPaths.push_back(p);
            result.changed = true;
            return true;
        }
        for (const std::string& p : removals) {
            if (!remove_path(p, err)) return false;
            result.removedPaths.push_back(p);
        }
        result.changed = true;
        return true;
    }

    // ---------------------------------------------------------------
    // RwgWithConfig
    // ---------------------------------------------------------------
    Bytes wrapperData, rwgData, configData, raw3Data;
    if (!read_file(layout.wrapperExe, wrapperData, err)) return false;
    if (!read_file(layout.rwgPath, rwgData, err)) return false;
    if (!read_file(layout.configPath, configData, err)) return false;
    if (!layout.raw3Path.empty()) {
        std::string rerr;
        read_file(layout.raw3Path, raw3Data, rerr);
    }

    // Image sizes take part in the seed derivation.
    std::vector<std::pair<std::string, uint64_t>> imageSizes;
    for (const std::string& p : layout.imagePaths) {
        std::string name = lower(path_basename(p));
        imageSizes.emplace_back(name, path_size(p));
    }

    // Seed derivation, in the order the wrapper itself would have used.
    SeedMaterial seed;
    {
        uint32_t crc = 0;
        std::string cerr;
        bool wrapperOk = crc32_pe_sections(wrapperData, crc, cerr);
        uint32_t afterWrapper = crc;
        bool raw3Ok = raw3Data.empty() || crc32_pe_sections(raw3Data, crc, cerr);
        uint32_t afterRaw3 = crc;
        for (const auto& kv : imageSizes) {
            uint32_t size = static_cast<uint32_t>(kv.second);
            uint8_t le[4] = {static_cast<uint8_t>(size & 0xFF), static_cast<uint8_t>((size >> 8) & 0xFF),
                             static_cast<uint8_t>((size >> 16) & 0xFF),
                             static_cast<uint8_t>((size >> 24) & 0xFF)};
            crc = crc32_update(crc, le, 4);
        }
        if (options.verbose) {
            char buf[256];
            std::snprintf(buf, sizeof(buf),
                          "seed probe: wrapper=%08x raw3=%08x images=%zu final=%08x (wrapperOk=%d raw3Ok=%d)",
                          afterWrapper, afterRaw3, imageSizes.size(), crc, wrapperOk ? 1 : 0,
                          raw3Ok ? 1 : 0);
            result.notes.push_back(buf);
        }
        if (wrapperOk && raw3Ok &&
            try_seed(crc, configData, seed, "original_crc32_sections_and_sizes")) {
            // done
        } else {
            // Fallback: CRC32 over the full files.
            Bytes wrapperPrefix = wrapper_image_prefix(wrapperData);
            uint32_t fullCrc = 0;
            fullCrc = crc32_update(fullCrc, wrapperPrefix.data(), wrapperPrefix.size());
            for (const std::string& p : layout.imagePaths) {
                Bytes data;
                std::string rerr;
                if (read_file(p, data, rerr)) fullCrc = crc32_update(fullCrc, data.data(), data.size());
            }
            if (!raw3Data.empty()) fullCrc = crc32_update(fullCrc, raw3Data.data(), raw3Data.size());
            if (options.verbose) {
                char buf[128];
                std::snprintf(buf, sizeof(buf), "seed probe: full-file candidate=%08x", fullCrc);
                result.notes.push_back(buf);
            }
            if (!try_seed(fullCrc, configData, seed, "original_crc32_full_files")) {
                // Last resort: brute force the size based seeds over the
                // files that could have taken part in the packaging.
                std::vector<std::pair<std::string, uint64_t>> pool;
                std::vector<std::string> searchDirs = {path_dirname(layout.configPath),
                                                       path_join(dir, "ReflexiveArcade"), dir};
                std::vector<std::string> seen;
                for (const std::string& sd : searchDirs) {
                    if (sd.empty() || !path_is_dir(sd)) continue;
                    for (const std::string& entry : list_dir(sd)) {
                        if (!path_is_file(entry)) continue;
                        std::string base = path_basename(entry);
                        std::string ext = lower(path_extension(entry));
                        std::string lowBase = lower(base);
                        if (ext == "exe" || ext == "bak" || ext == "dll") continue;
                        if (lowBase == "raw_001.exe" || lowBase == "raw_001.dat") continue;
                        if (entry == layout.configPath) continue;
                        if (entry == layout.rwgPath) continue;
                        bool duplicate = false;
                        for (const std::string& s : seen)
                            if (s == entry) duplicate = true;
                        if (duplicate) continue;
                        seen.push_back(entry);
                        pool.emplace_back(lowBase, path_size(entry));
                    }
                }
                // Prefer the well known dependency names first.
                std::stable_sort(pool.begin(), pool.end(), [](const auto& a, const auto& b) {
                    auto rank = [](const std::string& n) {
                        for (const char* p : kPrimarySeedDeps)
                            if (n == p) return 0;
                        for (const char* p : kSecondarySeedDeps)
                            if (n == p) return 1;
                        return 2;
                    };
                    return rank(a.first) < rank(b.first);
                });
                if (!seed_repack_additive(pool, rwgData.size(), configData, seed)) {
                    err = "could not derive the wrapper stream seed";
                    return false;
                }
            }
        }
    }
    result.method = seed.method;
    result.seed1 = seed.seed;

    auto config = parse_config(seed.decryptedConfig);
    bool isDotNet = config_flag(config, "Is .NET Executable");
    bool shortFixed = config_flag(config, "Game Needs Short Fixed Encryption");
    result.dotnet = isDotNet;

    uint32_t entrySkip = 0;
    if (!isDotNet && contains(wrapperData, kNativeEntrySkipMarker)) entrySkip = kNativeEntrySkip;
    result.entrySkip = entrySkip;

    PeImage rwgPe;
    if (!pe_parse(rwgData, rwgPe, err)) {
        err = "the payload is not a PE image: " + err;
        return false;
    }

    Region region = isDotNet ? dotnet_region(rwgPe) : native_region(rwgPe, shortFixed, entrySkip);
    if (!region.ok) {
        err = region.error;
        return false;
    }
    if (static_cast<size_t>(region.start) + region.length > rwgData.size()) {
        err = "the encrypted region lies outside the payload";
        return false;
    }
    result.regionStart = region.start;
    result.regionLength = region.length;

    uint32_t seed2 = derive_seed2(configData, config);
    result.seed2 = seed2;

    Bytes recovered = rwgData;
    stream_decrypt_inplace(recovered.data() + region.start, region.length, seed2);

    // Sanity: the code must have actually changed and must look like code.
    bool changed = false;
    for (uint32_t i = 0; i < region.length; ++i) {
        if (recovered[region.start + i] != rwgData[region.start + i]) { changed = true; break; }
    }
    if (!changed) {
        err = "decryption produced no change; the derived seed is wrong";
        return false;
    }
    if (!pe_looks_like_program(recovered, err)) {
        err = "the decrypted payload is not a valid program: " + err;
        return false;
    }

    // The entry point bytes, including the plaintext skip bytes.
    std::vector<uint8_t> entryBytes;
    if (entrySkip > 0 && region.start >= entrySkip)
        for (uint32_t i = 0; i < entrySkip; ++i) entryBytes.push_back(rwgData[region.start - entrySkip + i]);
    for (uint32_t i = 0; i < region.length && i < 16; ++i) entryBytes.push_back(recovered[region.start + i]);
    if (!looks_like_entry_bytes(entryBytes.data(), entryBytes.size())) {
        result.notes.push_back("the decrypted entry point does not match the usual code patterns");
    }

    // Resource restoration: give the recovered game the icon the wrapper kept.
    std::vector<ResourceLeaf> resources;
    size_t resourceCount = 0;
    if (options.restoreResources) {
        std::string rerr;
        if (harvest_resources(wrapperData, resources, resourceCount) && resourceCount > 0) {
            if (!pe_write_resources(recovered, resources, rerr)) {
                result.notes.push_back("resource restore failed: " + rerr);
            } else {
                result.resourcesRestored = true;
                result.resourcesRestoredCount = resourceCount;
            }
        }
    }

    // ---------------------------------------------------------------
    // Write it out
    // ---------------------------------------------------------------
    std::vector<std::string> touched;
    touched.push_back(path_basename(layout.targetExe));
    if (layout.wrapperExe != layout.targetExe) touched.push_back(path_basename(layout.wrapperExe));
    touched.push_back(path_basename(layout.rwgPath));

    if (!options.dryRun) {
        // Stage the files we are about to replace so a crash cannot leave a
        // half written game behind.
        if (!options.keepTemp) {
            std::string serr;
            remove_path(stageDir, serr);
            fs::create_directories(stageDir);
            std::string list;
            for (const std::string& rel : touched) {
                std::string src = path_join(dir, rel);
                if (!path_is_file(src)) continue;
                if (!copy_file(src, path_join(stageDir, rel), serr)) continue;
                list += rel + "\n";
            }
            Bytes marker(list.begin(), list.end());
            write_file_atomic(stageMarker, marker, serr);
        }

        std::string werr;
        if (!write_file_atomic(layout.targetExe, recovered, werr)) {
            err = werr;
            return false;
        }
    }
    result.gameExePath = layout.targetExe;

    if (!options.dryRun) {
        // Remove the wrapper pieces.
        if (layout.rwgPath != layout.targetExe) {
            std::string rerr;
            if (remove_path(layout.rwgPath, rerr)) result.removedPaths.push_back(layout.rwgPath);
        }
        if (layout.wrapperExe != layout.targetExe) {
            std::string rerr;
            if (remove_path(layout.wrapperExe, rerr)) result.removedPaths.push_back(layout.wrapperExe);
        }
        std::string reflexDir = path_join(dir, "ReflexiveArcade");
        if (path_is_dir(reflexDir)) {
            std::string rerr;
            if (remove_path(reflexDir, rerr)) result.removedPaths.push_back(reflexDir);
        }
        for (const std::string& p : layout.junkPaths) {
            std::string rerr;
            if (remove_path(p, rerr)) result.removedPaths.push_back(p);
        }
        if (!options.keepTemp) {
            std::string rerr;
            remove_path(stageDir, rerr);
        }
    } else {
        result.removedPaths.push_back(layout.rwgPath);
        if (layout.wrapperExe != layout.targetExe) result.removedPaths.push_back(layout.wrapperExe);
        std::string reflexDir = path_join(dir, "ReflexiveArcade");
        if (path_is_dir(reflexDir)) result.removedPaths.push_back(reflexDir);
        for (const std::string& p : layout.junkPaths) result.removedPaths.push_back(p);
    }

    result.changed = true;
    return true;
}

} // namespace rawr
