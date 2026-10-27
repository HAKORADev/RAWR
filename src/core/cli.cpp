#include "cli.h"

#include <cstdio>
#include <cstring>
#include <iostream>
#include <sstream>

#include "fsutil.h"
#include "platform.h"
#include "rwg.h"
#include "selftest.h"

namespace rawr {
namespace {

const char* kVersion = "0.0.0";

bool isCommand(const std::string& arg, const char* name) {
    return arg == name;
}

void printUsage() {
    std::cout <<
        "RAWR - unified game wrapper removal\n"
        "\n"
        "USAGE\n"
        "  rawr <folder> [<folder> ...]        remove the wrapper from every given game folder\n"
        "  rawr deps-check                    report the build dependencies and their status\n"
        "  rawr deps-install                  install the missing build dependencies\n"
        "  rawr version                       print the version\n"
        "  rawr help                          print this help\n"
        "\n"
        "OPTIONS\n"
        "  -n, --dry-run        show what would happen, change nothing\n"
        "  -v, --verbose        report every step\n"
        "  -q, --quiet          only report failures\n"
        "      --no-resources   do not carry the icon and manifest over\n"
        "      --keep-temp      keep the stage folder instead of deleting it\n"
        "      --json           print the result as JSON\n"
        "  -h, --help           print this help\n"
        "\n"
        "RAWR works on a whole installed game folder. It never touches a file it\n"
        "does not own, it is safe to run twice, and it never modifies a game it\n"
        "does not recognise.\n";
}

struct FolderReport {
    std::string path;
    std::string status;      // "cleaned", "already-clean", "skipped", "failed"
    std::string detail;
    std::string method;
    bool resourcesRestored = false;
    size_t resourceCount = 0;
    std::vector<std::string> removed;
    std::vector<std::string> notes;
};

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

void printJson(const std::vector<FolderReport>& reports) {
    std::cout << "{\n  \"tool\": \"rawr\",\n  \"version\": \"" << kVersion << "\",\n  \"results\": [\n";
    for (size_t i = 0; i < reports.size(); ++i) {
        const FolderReport& r = reports[i];
        std::cout << "    {\n";
        std::cout << "      \"path\": \"" << jsonEscape(r.path) << "\",\n";
        std::cout << "      \"status\": \"" << jsonEscape(r.status) << "\",\n";
        std::cout << "      \"detail\": \"" << jsonEscape(r.detail) << "\",\n";
        std::cout << "      \"method\": \"" << jsonEscape(r.method) << "\",\n";
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

int runDepsCheck(bool json) {
    std::vector<platform::Dependency> deps = platform::check_build_dependencies();
    size_t missing = 0;
    for (const auto& d : deps)
        if (!d.present) ++missing;

    if (json) {
        std::cout << "{\n  \"runtime_dependencies\": ";
        std::cout << (platform::is_static_build() ? "\"none (statically linked)\"" : "\"system libc\"") << ",\n";
        std::cout << "  \"build_dependencies\": [\n";
        for (size_t i = 0; i < deps.size(); ++i) {
            const auto& d = deps[i];
            std::cout << "    {\"name\": \"" << jsonEscape(d.name) << "\", \"purpose\": \""
                      << jsonEscape(d.purpose) << "\", \"present\": " << (d.present ? "true" : "false")
                      << ", \"install\": \"" << jsonEscape(d.installHint) << "\"}"
                      << (i + 1 < deps.size() ? "," : "") << "\n";
        }
        std::cout << "  ],\n  \"missing\": " << missing << "\n}\n";
        return missing == 0 ? 0 : 1;
    }

    std::cout << "RAWR runtime dependencies\n";
    std::cout << "  " << (platform::is_static_build() ? "none - the binary is statically linked"
                                                     : "system C library")
              << "\n\n";
    std::cout << "Build dependencies (needed only to rebuild RAWR from source)\n";
    for (const auto& d : deps) {
        std::cout << "  [" << (d.present ? "ok  " : "MISS") << "] " << d.name << " - " << d.purpose << "\n";
        if (!d.present) std::cout << "          install: " << d.installHint << "\n";
    }
    if (missing) {
        std::cout << "\n" << missing << " dependency missing. Run: rawr deps-install\n";
        return 1;
    }
    std::cout << "\nEverything needed to build RAWR is present.\n";
    return 0;
}

int runDepsInstall() {
    std::vector<platform::Dependency> deps = platform::check_build_dependencies();
    std::vector<platform::Dependency> missing;
    for (const auto& d : deps)
        if (!d.present) missing.push_back(d);

    if (missing.empty()) {
        std::cout << "Nothing to install - every build dependency is present.\n";
        return 0;
    }
    std::cout << "Missing build dependencies:\n";
    for (const auto& d : missing) std::cout << "  - " << d.name << " (" << d.purpose << ")\n";
    std::cout << "\nInstalling...\n";
    std::string err;
    if (!platform::install_build_dependencies(missing, err)) {
        std::cerr << "could not install automatically: " << err << "\n";
        std::cerr << "install them by hand using the hints above.\n";
        return 2;
    }
    std::cout << "Done.\n";
    return 0;
}

} // namespace

const char* cli_version() { return kVersion; }

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
        if (a == "--json") { options.json = true; continue; }
        if (a == "--") { continue; }
        if (!a.empty() && a[0] == '-') {
            options.error = "unknown option: " + a;
            return false;
        }
        if (options.paths.empty() &&
            (isCommand(a, "deps-check") || isCommand(a, "deps-install") ||
             isCommand(a, "version") || isCommand(a, "help") || isCommand(a, "selftest"))) {
            options.command = a;
            options.paths.clear();
            continue;
        }
        options.paths.push_back(a);
    }

    if (options.command == "unwrap" && options.paths.empty()) {
        options.error = "no game folder given";
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
    if (options.command == "deps-check") return runDepsCheck(options.json);
    if (options.command == "deps-install") return runDepsInstall();
    if (options.command == "selftest") {
        SelfTestResult r = selftest_run();
        if (!options.json) {
            std::cout << "RAWR " << kVersion << " self test\n";
            for (const std::string& line : r.lines) std::cout << line << "\n";
            std::cout << "\n" << (r.checks - r.failures) << "/" << r.checks << " checks passed\n";
        } else {
            std::cout << "{\"passed\": " << (r.passed ? "true" : "false") << ", \"checks\": " << r.checks
                      << ", \"failures\": " << r.failures << "}\n";
        }
        return r.passed ? 0 : 1;
    }

    if (!options.quiet && !options.json) {
        std::cout << "RAWR " << kVersion << " - unified game wrapper removal\n";
        if (options.dryRun) std::cout << "(dry run - nothing will be written)\n";
        std::cout << "\n";
    }

    std::vector<FolderReport> reports;
    int failures = 0;
    int cleaned = 0;

    for (const std::string& raw : options.paths) {
        FolderReport report;
        report.path = raw;

        if (!path_exists(raw)) {
            report.status = "failed";
            report.detail = "path does not exist";
            failures++;
            reports.push_back(report);
            if (!options.quiet) std::cout << "  [x] " << raw << ": path does not exist\n";
            continue;
        }

        UnwrapOptions unwrapOptions;
        unwrapOptions.dryRun = options.dryRun;
        unwrapOptions.restoreResources = options.restoreResources;
        unwrapOptions.keepTemp = options.keepTemp;
        unwrapOptions.verbose = options.verbose;
        unwrapOptions.quiet = options.quiet;

        UnwrapResult result;
        std::string err;
        if (!unwrap_folder(raw, unwrapOptions, result, err)) {
            report.status = "failed";
            report.detail = err;
            report.notes = result.notes;
            failures++;
            reports.push_back(report);
            if (!options.quiet && !options.json) {
                std::cout << "  [x] " << raw << ": " << err << "\n";
                if (options.verbose)
                    for (const std::string& note : result.notes) std::cout << "      " << note << "\n";
            }
            continue;
        }

        if (result.alreadyClean) {
            report.status = "already-clean";
            report.detail = result.notes.empty() ? "nothing to remove" : result.notes.front();
        } else {
            report.status = "cleaned";
            report.detail = result.gameExePath;
            cleaned++;
        }
        report.method = result.method;
        report.resourcesRestored = result.resourcesRestored;
        report.resourceCount = result.resourcesRestoredCount;
        report.removed = result.removedPaths;
        report.notes = result.notes;
        reports.push_back(report);

        if (options.quiet || options.json) continue;
        if (report.status == "already-clean") {            std::cout << "  [=] " << raw << ": " << report.detail << "\n";
        } else {
            std::cout << "  [+] " << raw << ": wrapper removed -> " << result.gameExePath << "\n";
            if (!result.method.empty()) std::cout << "      seed method: " << result.method << "\n";
            if (result.resourcesRestored)
                std::cout << "      restored " << result.resourcesRestoredCount
                          << " resources (icon, manifest, version info)\n";
            for (const std::string& note : result.notes) std::cout << "      note: " << note << "\n";
            if (options.verbose) {
                for (const std::string& p : result.removedPaths) std::cout << "      removed: " << p << "\n";
            }
        }
    }

    if (options.json) {
        printJson(reports);
    } else if (!options.quiet) {
        std::cout << "\n"
                  << cleaned << " folder(s) cleaned, " << failures << " failed, "
                  << (reports.size() - cleaned - failures) << " already clean\n";
    }
    return failures == 0 ? 0 : 1;
}

} // namespace rawr
