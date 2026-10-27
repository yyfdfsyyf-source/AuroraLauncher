#include "core/paths.hpp"

#include "core/error.hpp"
#include "core/strings.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace aurora {

namespace {
std::filesystem::path env_path(std::string_view name) {
    const std::string value = environment_value(name);
    return value.empty() ? std::filesystem::path{} : path_from_utf8(value);
}
} // namespace

std::string environment_value(std::string_view name) {
#ifdef _WIN32
    const std::wstring wide_name = to_wide(name);
    DWORD required = GetEnvironmentVariableW(wide_name.c_str(), nullptr, 0);
    if (required == 0) {
        return {};
    }
    std::wstring buffer(required, L'\0');
    const DWORD written =
        GetEnvironmentVariableW(wide_name.c_str(), buffer.data(), required);
    if (written == 0 || written >= required) {
        return {};
    }
    buffer.resize(written);
    return to_utf8(buffer);
#else
    const std::string key(name);
    const char* value = std::getenv(key.c_str());
    return value == nullptr ? std::string{} : std::string(value);
#endif
}

std::filesystem::path home_dir() {
#ifdef _WIN32
    auto home = env_path("USERPROFILE");
    if (home.empty()) {
        const auto drive = environment_value("HOMEDRIVE");
        const auto path = environment_value("HOMEPATH");
        home = path_from_utf8(drive + path);
    }
#else
    auto home = env_path("HOME");
#endif
    if (home.empty()) {
        home = std::filesystem::current_path();
    }
    return home;
}

std::filesystem::path app_data_dir() {
#ifdef _WIN32
    auto path = env_path("APPDATA");
    if (path.empty()) {
        path = home_dir() / "AppData" / "Roaming";
    }
#else
    auto path = env_path("XDG_CONFIG_HOME");
    if (path.empty()) {
        path = home_dir() / ".config";
    }
#endif
    return path / "AuroraLauncher";
}

std::filesystem::path default_game_dir() {
    return home_dir() / ".minecraft";
}

std::filesystem::path default_config_path() {
    return app_data_dir() / "config.json";
}

std::filesystem::path default_log_path() {
    return app_data_dir() / "logs" / "aurora.log";
}

std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        fail("Unable to open file: " + path_to_utf8(path));
    }
    std::ostringstream content;
    content << input.rdbuf();
    if (input.bad()) {
        fail("Unable to read file: " + path_to_utf8(path));
    }
    return content.str();
}

void ensure_directory(const std::filesystem::path& path) {
    std::error_code ec;
    if (path.empty() || std::filesystem::exists(path, ec)) {
        return;
    }
    std::filesystem::create_directories(path, ec);
    if (ec) {
        fail("Unable to create directory: " + path_to_utf8(path) + " (" + ec.message() + ")");
    }
}

void write_text_file(const std::filesystem::path& path, std::string_view content) {
    ensure_directory(path.parent_path());
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            fail("Unable to open file for writing: " + path_to_utf8(path));
        }
        output.write(content.data(), static_cast<std::streamsize>(content.size()));
        output.flush();
        if (!output) {
            fail("Unable to write file: " + path_to_utf8(path));
        }
    }
    std::error_code ec;
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        std::filesystem::remove(path, ec);
        ec.clear();
        std::filesystem::rename(temporary, path, ec);
        if (ec) {
            std::filesystem::remove(temporary);
            fail("Unable to replace file: " + path_to_utf8(path) + " (" + ec.message() + ")");
        }
    }
}

std::filesystem::path absolute_path(const std::filesystem::path& path) {
    std::error_code ec;
    auto result = std::filesystem::absolute(path, ec);
    if (ec) {
        result = path;
    }
    return result.lexically_normal();
}

bool is_safe_relative_path(std::string_view value) {
    if (value.empty()) {
        return false;
    }
    std::string normalized(value);
    for (char& character : normalized) {
        if (character == '\\') {
            character = '/';
        }
    }
    if (normalized.front() == '/' || normalized.find(':') != std::string::npos) {
        return false;
    }
    for (const std::string& part : split(normalized, '/')) {
        if (part == ".." || (!part.empty() && (part.back() == '.' || part.back() == ' '))) {
            return false;
        }
    }
    return true;
}

std::filesystem::path checked_relative_path(std::string_view value, std::string_view context) {
    if (!is_safe_relative_path(value)) {
        fail("Unsafe relative path for " + std::string(context) + ": " + std::string(value));
    }
    return path_from_utf8(value);
}

} // namespace aurora
