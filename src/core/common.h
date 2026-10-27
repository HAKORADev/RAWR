// RAWR - Reflexive Arcade Wrapper Removal (unified game wrapper removal tool)
// common.h - shared primitives
#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace rawr {

using Bytes = std::vector<uint8_t>;

// Every fallible core routine reports through this instead of throwing.
// A plain bool + message keeps the binary dependency-free and the call
// sites readable.
struct Status {
    bool ok = true;
    std::string message;

    Status() = default;
    Status(bool o, std::string m) : ok(o), message(std::move(m)) {}

    static Status success() { return Status(); }
    static Status failure(std::string m) { return Status(false, std::move(m)); }

    explicit operator bool() const { return ok; }
};

} // namespace rawr
