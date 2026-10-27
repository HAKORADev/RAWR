// Entry point for the Linux build.
#include <string>
#include <vector>

#include "../core/cli.h"
#include "../core/platform.h"

int main(int argc, char** argv) {
    std::vector<std::string> args;
    args.reserve(static_cast<size_t>(argc > 0 ? argc - 1 : 0));
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);

    rawr::platform::prepare_output();

    rawr::CliOptions options;
    if (!rawr::cli_parse(args, options)) {
        std::fprintf(stderr, "rawr: %s\n\n", options.error.c_str());
        std::fprintf(stderr, "Run 'rawr help' for the usage.\n");
        return 2;
    }
    return rawr::cli_run(options);
}
