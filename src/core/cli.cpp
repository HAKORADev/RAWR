#include "cli.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>

#include "fsutil.h"
#include "integrated.h"
#include "platform.h"
#include "rwg.h"
#include "selftest.h"

namespace rawr {
namespace {

const char* kVersion = "0.0.0-1";

void printUsage() {
    std::cout <<
        "RAWR - unified game wrapper removal\n"
        "\n"
        "USAGE\n"
        "  rawr <path> [<path> ...]        remove the wrapper from every game it finds\n"
        "  rawr detect <path> [...]        report what a folder is, change nothing\n"
        "  rawr wrappers-list              show the wrappers RAWR knows about\n"
        "  rawr selftest [tests folder]    run the built in checks\n"
        "  rawr version                    print the version\n"
        "  rawr help                       print this help\n"
        "\n"
        "OPTIONS\n"
        "  -n, --dry-run        show what would happen, change nothing\n"
        "  -v, --verbose        report every step\n"
        "  -q, --quiet          only report failures\n"
        "      --no-resources   do not carry the icon and manifest over\n"
        "      --keep-temp      keep the stage folder instead of deleting it\n"
        "      --no-report      do not write the report file\n"
        "      --json           print the result as JSON\n"
        "  -h, --help           print this help\n"
        "\n"
        "RAWR takes a folder and works out where the games are on its own: it\n"
        "walks the tree, finds every executable, and keeps the ones that are\n"
        "actually a wrapped game. Point it at a whole library and it will\n"
        "crawl it.\n"
        "\n"
        "Progress goes to stderr, the result goes to stdout, so the output can\n"
        "be piped into other tools. A markdown report is written next to the\n"
        "executable.\n";
}

// Left pads a string to a column width.
std::string pad(const std::string& in, size_t width) {
    std::string out = in;
    while (out.size() < width) out.push_back(' ');
    return out;
}

std::string jsonEscape(const std::string& in) {
    std::string out;
    for (char c : in) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

struct FolderReport {
    std::string path;
    std::string name;
    std::string status;      // cleaned | already-clean | registered | skipped | failed
    std::string detail;
    std::string method;
    std::string wrapper;
    bool resourcesRestored = false;
    size_t resourceCount = 0;
    bool registered = false;
    std::vector<std::string> removed;
    std::vector<std::string> notes;
};

// ---------------------------------------------------------------------------
// Finding the games
// ---------------------------------------------------------------------------

void collectDirsWithExecutables(const std::string& dir, std::vector<std::string>& out,
                                int depth, int maxDepth) {
    if (depth > maxDepth) return;
    std::vector<std::string> entries = list_dir(dir);
    bool hasExe = false;
    for (const std::string& e : entries) {
        if (!path_is_file(e)) continue;
        std::string ext = path_extension(e);
        if (ext == "exe" || ext == "bak") { hasExe = true; break; }
    }
    if (hasExe) out.push_back(dir);
    for (const std::string& e : entries) {
        if (!path_is_dir(e)) continue;
        std::string base = path_basename(e);
        if (base == "ReflexiveArcade" || base == ".rawr-stage") continue;
        collectDirsWithExecutables(e, out, depth + 1, maxDepth);
    }
}

// Every folder that could hold a wrapped game: a folder with an executable
// in it, and any folder that holds a payload next to its support files.
void collectCandidateRoots(const std::string& root, std::vector<std::string>& out) {
    std::vector<std::string> dirs;
    collectDirsWithExecutables(root, dirs, 0, 12);
    std::sort(dirs.begin(), dirs.end());

    // A folder that only holds the support directory still counts when a
    // child folder holds the payload.
    for (const std::string& d : dirs) {
        for (const std::string& e : list_dir(d)) {
            if (!path_is_file(e)) continue;
            if (path_extension(e) == "rwg") {
                // The payload is in this folder. It is already in the list
                // when it holds an executable; add it either way.
                if (std::find(out.begin(), out.end(), d) == out.end()) out.push_back(d);
            }
        }
    }
    for (const std::string& d : dirs)
        if (std::find(out.begin(), out.end(), d) == out.end()) out.push_back(d);
}

std::vector<std::string> gatherCandidates(const std::vector<std::string>& paths) {
    std::vector<std::string> out;
    for (const std::string& p : paths) {
        if (path_is_file(p)) {
            std::string dir = path_dirname(p);
            if (std::find(out.begin(), out.end(), dir) == out.end()) out.push_back(dir);
            continue;
        }
        if (!path_is_dir(p)) continue;
        std::vector<std::string> found;
        collectCandidateRoots(p, found);
        for (const std::string& f : found)
            if (std::find(out.begin(), out.end(), f) == out.end()) out.push_back(f);
    }
    return out;
}

// ---------------------------------------------------------------------------
// The wrappers RAWR knows about
// ---------------------------------------------------------------------------

const std::vector<WrapperInfo> kWrappers = {
    {"Reflexive Arcade", "download service shell", "supported",
     "encrypted .RWG payload, launcher stub, ReflexiveArcade.dll trial timer"},
    {"Reflexive Arcade", "integrated binding layer", "supported",
     "the binding layer is compiled into the game; the purchase check is patched"},
    {"PopCap", "registration nag", "planned", "the register info is inside the executable"},
    {"GameHouse", "download service shell", "planned", "several generations, needs research"},
    {"Alawar", "Nova disc installer", "planned", "the Inno payload inside the disc image"},
    {"GameTop", "download service shell", "planned", "the easiest of the remaining ones"},
    {"MyPlayCity / MyRealGames", "download service shell", "planned", "same family as GameTop"},
    {"BigFish", "Torque Game Builder layout", "planned", "the game is TGB.exe next to the engine data"},
    {"iWin", "registration nag", "planned", "check the launcher shape first"},
    {"Zylom", "registration nag", "planned", "check the launcher shape first"},
    {"FreeGamePick", "registration nag", "planned", "check the launcher shape first"},
    {"PlayFirst", "download service shell", "planned", "Wedding Dash and friends"},
    {"Shockwave", "download service shell", "planned", "the old shockwave.com service"},
    {"WildTangent", "registration nag", "planned", "check the launcher shape first"},
    {"MumboJumbo", "download service shell", "planned", "not looked at yet"},
    {"Rovio", "registration nag", "planned", "the old Angry Birds catalogue"},
    {"Interaction Studios", "registration nag", "planned", "AxySnake and the Real Arcade games"},
};

void printWrappersList() {
    std::cout << "RAWR " << kVersion << " - known wrappers\n\n";
    std::cout << "  " << pad("PUBLISHER", 28) << pad("WRAPPER", 28) << pad("STATE", 18) << "NOTE\n";
    std::cout << "  " << std::string(92, '-') << "\n";
    for (const WrapperInfo& w : kWrappers) {
        std::string state = w.state;
        std::string note = w.note;
        if (state == "supported") state = "SUPPORTED";
        else if (state == "planned") state = "planned";
        std::cout << "  " << pad(w.publisher, 28) << pad(w.wrapper, 28) << pad(state, 18) << note << "\n";
    }
    std::cout << "\n  " << kWrappers.size() << " known, "
              << std::count_if(kWrappers.begin(), kWrappers.end(),
                               [](const WrapperInfo& w) { return std::string(w.state) == "supported"; })
              << " supported right now.\n";
    std::cout << "  The planned ones are researched but not implemented yet.\n";
}

// ---------------------------------------------------------------------------
// The report
// ---------------------------------------------------------------------------

std::string timestamp() {
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
#if defined(_WIN32)
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d-%H-%M-%S", &tmv);
    return std::string(buf);
}

bool writeReport(const std::vector<FolderReport>& reports, const std::string& exePath,
                 std::string& written) {
    std::string dir = exePath.empty() ? std::string(".") : path_dirname(exePath);
    std::string file = path_join(dir, "rawr-report-" + timestamp() + ".md");

    std::ostringstream out;
    out << "# RAWR report\n\n";
    out << "Generated " << timestamp() << " by RAWR " << kVersion << ".\n\n";
    size_t cleaned = 0, registered = 0, failed = 0, clean = 0;
    for (const FolderReport& r : reports) {
        if (r.status == "cleaned") ++cleaned;
        else if (r.status == "registered") ++registered;
        else if (r.status == "failed") ++failed;
        else if (r.status == "already-clean") ++clean;
    }
    out << "| cleaned | marked registered | already clean | failed |\n";
    out << "|---|---|---|---|\n";
    out << "| " << cleaned << " | " << registered << " | " << clean << " | " << failed << " |\n\n";
    out << "## Every folder\n\n";
    out << "| game | folder | result | detail |\n|---|---|---|---|\n";
    for (const FolderReport& r : reports) {
        out << "| " << r.name << " | `" << r.path << "` | " << r.status << " | " << r.detail;
        if (!r.notes.empty()) {
            out << "<br>";
            for (const std::string& n : r.notes) out << n << " ";
        }
        out << " |\n";
    }
    out << "\n";
    if (failed) {
        out << "## Needs a look\n\n";
        for (const FolderReport& r : reports)
            if (r.status == "failed") out << "- `" << r.path << "` - " << r.detail << "\n";
        out << "\n";
    }

    std::string text = out.str();
    Bytes data(text.begin(), text.end());
    std::string err;
    if (!write_file_atomic(file, data, err)) return false;
    written = file;
    return true;
}

void printJson(const std::vector<FolderReport>& reports) {
    std::cout << "{\n  \"tool\": \"rawr\",\n  \"version\": \"" << kVersion << "\",\n  \"results\": [\n";
    for (size_t i = 0; i < reports.size(); ++i) {
        const FolderReport& r = reports[i];
        std::cout << "    {\n";
        std::cout << "      \"path\": \"" << jsonEscape(r.path) << "\",\n";
        std::cout << "      \"name\": \"" << jsonEscape(r.name) << "\",\n";
        std::cout << "      \"status\": \"" << jsonEscape(r.status) << "\",\n";
        std::cout << "      \"detail\": \"" << jsonEscape(r.detail) << "\",\n";
        std::cout << "      \"method\": \"" << jsonEscape(r.method) << "\",\n";
        std::cout << "      \"wrapper\": \"" << jsonEscape(r.wrapper) << "\",\n";
        std::cout << "      \"registered\": " << (r.registered ? "true" : "false") << ",\n";
        std::cout << "      \"resources_restored\": " << (r.resourcesRestored ? "true" : "false") << ",\n";
        std::cout << "      \"resource_count\": " << r.resourceCount << ",\n";
        std::cout << "      \"removed\": [";
        for (size_t k = 0; k < r.removed.size(); ++k) {
            if (k) std::cout << ", ";
            std::cout << "\"" << jsonEscape(r.removed[k]) << "\"";
        }
        std::cout << "],\n";
        std::cout << "      \"notes\": [";
        for (size_t k = 0; k < r.notes.size(); ++k) {
            if (k) std::cout << ", ";
            std::cout << "\"" << jsonEscape(r.notes[k]) << "\"";
        }
        std::cout << "]\n    }" << (i + 1 < reports.size() ? "," : "") << "\n";
    }
    std::cout << "  ]\n}\n";
}

// ---------------------------------------------------------------------------
// Doing the work
// ---------------------------------------------------------------------------

void processOne(const std::string& root, const CliOptions& options, FolderReport& report) {
    report.path = root;
    report.name = path_basename(root);
    if (report.name.empty()) report.name = root;

    UnwrapOptions unwrapOptions;
    unwrapOptions.dryRun = options.dryRun;
    unwrapOptions.restoreResources = options.restoreResources;
    unwrapOptions.keepTemp = options.keepTemp;
    unwrapOptions.verbose = options.verbose;
    unwrapOptions.quiet = options.quiet;

    UnwrapResult result;
    std::string err;
    if (!unwrap_folder(root, unwrapOptions, result, err)) {
        report.status = "failed";
        report.detail = err;
        return;
    }
    report.method = result.method;
    report.resourcesRestored = result.resourcesRestored;
    report.resourceCount = result.resourcesRestoredCount;
    report.registered = result.registered;
    report.removed = result.removedPaths;
    report.notes = result.notes;
    report.wrapper = "Reflexive Arcade";
    if (result.registered) {
        report.status = "registered";
        report.detail = result.gameExePath;
    } else if (result.alreadyClean) {
        report.status = "already-clean";
        report.detail = result.notes.empty() ? "nothing to remove" : result.notes.front();
    } else {
        report.status = "cleaned";
        report.detail = result.gameExePath;
    }
}

void detectOne(const std::string& root, FolderReport& report) {
    report.path = root;
    report.name = path_basename(root);
    if (report.name.empty()) report.name = root;
    ReflexiveLayout layout;
    std::string err;
    if (!detect_reflexive(root, layout, err)) {
        report.status = "failed";
        report.detail = err;
        return;
    }
    switch (layout.layout) {
        case WrapperLayout::NotReflexive:
            report.status = "skipped";
            report.detail = "not a wrapped game";
            break;
        case WrapperLayout::RwgWithConfig:
            report.status = "wrapped";
            report.wrapper = "Reflexive Arcade";
            report.detail = "encrypted payload: " + path_basename(layout.rwgPath);
            break;
        case WrapperLayout::Integrated:
            report.status = "integrated";
            report.wrapper = "Reflexive Arcade";
            report.detail = "the binding layer is inside the game executable";
            break;
        case WrapperLayout::DirectExecutable:
            report.status = "skipped";
            report.detail = "the game is not wrapped, only the support folder is left";
            break;
        case WrapperLayout::PopCap:
            report.status = "wrapped";
            report.wrapper = "PopCap";
            report.detail = layout.reason;
            break;
        case WrapperLayout::Incomplete:
            report.status = "broken";
            report.detail = layout.reason;
            break;
    }
}

int runUnwrap(const CliOptions& options) {
    std::vector<std::string> candidates = gatherCandidates(options.paths);

    if (candidates.empty()) {
        if (!options.quiet)
            std::cerr << "no executables found under the given paths\n";
        return 1;
    }

    if (!options.quiet && !options.json) {
        std::cerr << "RAWR " << kVersion << " - unified game wrapper removal\n";
        if (options.dryRun) std::cerr << "(dry run - nothing will be written)\n";
        std::cerr << "found " << candidates.size() << " candidate folder(s)\n\n";
    }

    size_t total = candidates.size();
    std::vector<FolderReport> reports(total);
    std::atomic<size_t> nextIndex{0};
    std::atomic<size_t> done{0};
    std::mutex printMutex;

    unsigned int threads = std::thread::hardware_concurrency();
    if (threads == 0) threads = 1;
    // Leave a core for the system so a big library does not lock the machine.
    if (threads > 1) threads -= 1;
    if (threads > 8) threads = 8;
    if (total < threads) threads = static_cast<unsigned int>(total);

    auto worker = [&]() {
        while (true) {
            size_t i = nextIndex.fetch_add(1);
            if (i >= total) break;
            // One bad folder must never take the whole run down.
            try {
                processOne(candidates[i], options, reports[i]);
            } catch (const std::exception& e) {
                reports[i].status = "failed";
                reports[i].detail = std::string("unexpected error: ") + e.what();
            } catch (...) {
                reports[i].status = "failed";
                reports[i].detail = "unexpected error";
            }
            size_t d = done.fetch_add(1) + 1;
            if (!options.quiet && !options.json) {
                std::lock_guard<std::mutex> lock(printMutex);
                std::cerr << "  [" << d << "/" << total << "] " << reports[i].name << " - "
                          << reports[i].status << "\n";
                std::cerr.flush();
            }
        }
    };

    std::vector<std::thread> pool;
    for (unsigned int i = 0; i < threads; ++i) pool.emplace_back(worker);
    for (std::thread& t : pool) t.join();

    size_t cleaned = 0, registered = 0, failed = 0, already = 0, skipped = 0;
    for (const FolderReport& r : reports) {
        if (r.status == "cleaned") ++cleaned;
        else if (r.status == "registered") ++registered;
        else if (r.status == "failed") ++failed;
        else if (r.status == "already-clean") ++already;
        else ++skipped;
    }

    if (options.json) {
        printJson(reports);
    } else {
        std::cout << "RAWR " << kVersion << "\n";
        for (const FolderReport& r : reports) {
            if (r.status == "skipped") continue;
            if (r.status == "failed")
                std::cout << "  [x] " << r.name << " (" << r.path << "): " << r.detail << "\n";
            else if (r.status == "already-clean")
                std::cout << "  [=] " << r.name << ": " << r.detail << "\n";
            else if (r.status == "registered")
                std::cout << "  [*] " << r.name << ": marked as registered -> " << r.detail << "\n";
            else
                std::cout << "  [+] " << r.name << ": wrapper removed -> " << r.detail << "\n";
        }
        std::cout << "\n" << cleaned << " cleaned, " << registered << " marked registered, " << already
                  << " already clean, " << skipped << " skipped, " << failed << " failed\n";
    }

    if (!options.quiet && !options.json && !options.dryRun && !options.noReport) {
        std::string written;
        if (writeReport(reports, platform::executable_path(), written))
            std::cout << "report: " << written << "\n";
    }
    return failed == 0 ? 0 : 1;
}

int runDetect(const CliOptions& options) {
    std::vector<std::string> candidates = gatherCandidates(options.paths);
    std::vector<FolderReport> reports;
    for (const std::string& c : candidates) {
        FolderReport r;
        detectOne(c, r);
        reports.push_back(r);
    }
    if (options.json) {
        printJson(reports);
        return 0;
    }
    for (const FolderReport& r : reports) {
        if (r.status == "skipped") continue;
        std::cout << "  " << r.name << " - " << r.status;
        if (!r.wrapper.empty()) std::cout << " (" << r.wrapper << ")";
        std::cout << ": " << r.detail << "\n";
    }
    std::cout << "\n" << reports.size() << " folder(s) looked at\n";
    return 0;
}

int runSelftest(const CliOptions& options) {
    SelfTestResult r = selftest_run();
    if (!options.json) {
        std::cout << "RAWR " << kVersion << " self test\n";
        for (const std::string& line : r.lines) std::cout << line << "\n";
        std::cout << "\n" << (r.checks - r.failures) << "/" << r.checks << " checks passed\n";
    } else {
        std::cout << "{\"passed\": " << (r.passed ? "true" : "false") << ", \"checks\": " << r.checks
                  << ", \"failures\": " << r.failures << "}\n";
    }

    // When a tests folder is given, run the script tests that live there.
    if (!options.paths.empty()) {
        std::string dir = options.paths.front();
        std::string runner = path_join(dir, "run_tests.sh");
        if (path_is_file(runner)) {
            std::cout << "\nrunning " << runner << "\n";
            int rc = std::system(("sh \"" + runner + "\" \"" + dir + "\"").c_str());
            if (rc != 0) r.passed = false;
        } else {
            std::cout << "\nno run_tests.sh in " << dir << ", only the built in checks ran\n";
        }
    }
    return r.passed ? 0 : 1;
}

} // namespace

const char* cli_version() { return kVersion; }

const std::vector<WrapperInfo>& known_wrappers() { return kWrappers; }

bool cli_parse(const std::vector<std::string>& args, CliOptions& options) {
    options = CliOptions();
    options.command = "unwrap";

    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "-h" || a == "--help") { options.showHelp = true; options.command = "help"; return true; }
        if (a == "-V" || a == "--version") { options.showVersion = true; options.command = "version"; return true; }
        if (a == "-n" || a == "--dry-run") { options.dryRun = true; continue; }
        if (a == "-v" || a == "--verbose") { options.verbose = true; continue; }
        if (a == "-q" || a == "--quiet") { options.quiet = true; continue; }
        if (a == "--no-resources") { options.restoreResources = false; continue; }
        if (a == "--keep-temp") { options.keepTemp = true; continue; }
        if (a == "--no-report") { options.noReport = true; continue; }
        if (a == "--json") { options.json = true; continue; }
        if (a == "--") { continue; }
        if (!a.empty() && a[0] == '-') {
            options.error = "unknown option: " + a;
            return false;
        }
        if (options.paths.empty() &&
            (a == "detect" || a == "wrappers-list" || a == "selftest" || a == "version" || a == "help")) {
            options.command = a;
            continue;
        }
        options.paths.push_back(a);
    }

    if (options.command == "unwrap" && options.paths.empty()) {
        options.error = "no path given";
        return false;
    }
    if ((options.command == "detect") && options.paths.empty()) {
        options.error = "no path given";
        return false;
    }
    return true;
}

int cli_run(const CliOptions& options) {
    if (options.showHelp || options.command == "help") {
        printUsage();
        return 0;
    }
    if (options.showVersion || options.command == "version") {
        std::cout << "rawr " << kVersion << "\n";
        return 0;
    }
    if (options.command == "wrappers-list") {
        printWrappersList();
        return 0;
    }
    if (options.command == "selftest") return runSelftest(options);
    if (options.command == "detect") return runDetect(options);
    return runUnwrap(options);
}

} // namespace rawr
