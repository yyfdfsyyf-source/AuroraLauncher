#include "cli.hpp"

#include "core/error.hpp"
#include "core/strings.hpp"

#include <exception>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace {

int execute(const std::vector<std::string>& arguments) {
    try {
        return aurora::run_cli(arguments);
    } catch (const aurora::Error& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << '\n';
        return 1;
    }
}

#ifdef _WIN32
std::vector<std::string> wide_arguments(int& count) {
    std::vector<std::string> arguments;
    int wide_count = 0;
    LPWSTR* wide_argv = CommandLineToArgvW(GetCommandLineW(), &wide_count);
    if (wide_argv == nullptr) {
        return arguments;
    }
    for (int i = 1; i < wide_count; ++i) {
        arguments.push_back(aurora::to_utf8(wide_argv[i]));
    }
    LocalFree(wide_argv);
    count = wide_count;
    return arguments;
}
#endif

} // namespace

#ifdef _WIN32
int main(int argc, char** argv) {
    // Use the wide Windows command line so non-ASCII paths and usernames survive.
    int wide_count = 0;
    std::vector<std::string> arguments;
    try {
        arguments = wide_arguments(wide_count);
    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << '\n';
        return 1;
    }
    if (wide_count == 0) {
        std::vector<std::string> fallback;
        for (int i = 1; i < argc; ++i) {
            fallback.emplace_back(argv[i]);
        }
        return execute(fallback);
    }
    return execute(arguments);
}
#else
int main(int argc, char** argv) {
    std::vector<std::string> arguments;
    for (int i = 1; i < argc; ++i) {
        arguments.emplace_back(argv[i]);
    }
    return execute(arguments);
}
#endif
