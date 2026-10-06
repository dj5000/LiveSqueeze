#pragma once

#include <string>
#include <utility>

namespace lsq {

// Result of an operation that can fail for reasons worth showing to the user.
struct Status {
    bool ok = true;
    std::string message;

    static Status success() { return {}; }
    static Status error(std::string msg) { return {false, std::move(msg)}; }
    explicit operator bool() const { return ok; }
};

} // namespace lsq
