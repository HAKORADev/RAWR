// Command line front end for RAWR.
#pragma once

#include <string>
#include <vector>

namespace rawr {

struct CliOptions {
    std::vector<std::string> paths;
    std::string command;              // "unwrap", "deps-check", "deps-install", "version", "help"
    bool dryRun = false;
    bool verbose = false;
    bool quiet = false;
    bool restoreResources = true;
    bool keepTemp = false;
    bool json = false;
    bool showHelp = false;
    bool showVersion = false;
    std::string error;
};

// Parses argv. Returns false and fills options.error on a bad command line.
bool cli_parse(const std::vector<std::string>& args, CliOptions& options);

// Runs the requested command. Returns the process exit code.
int cli_run(const CliOptions& options);

const char* cli_version();

} // namespace rawr
