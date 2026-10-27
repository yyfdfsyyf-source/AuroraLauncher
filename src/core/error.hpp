#pragma once

#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace aurora {

class Error : public std::runtime_error {
public:
    explicit Error(const std::string& message) : std::runtime_error(message) {}
};

[[noreturn]] inline void fail(const std::string& message) {
    throw Error(message);
}

template <typename... Args>
std::string concat(Args&&... args) {
    std::ostringstream out;
    (out << ... << std::forward<Args>(args));
    return out.str();
}

} // namespace aurora
