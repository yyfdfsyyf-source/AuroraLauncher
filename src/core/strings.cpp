#include "core/strings.hpp"

#include "core/error.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace aurora {

#ifdef _WIN32
std::string to_utf8(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        fail("Unable to convert a wide string to UTF-8");
    }
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring to_wide(std::string_view value) {
    if (value.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        nullptr, 0);
    if (size <= 0) {
        fail("Unable to convert a UTF-8 string to UTF-16");
    }
    std::wstring result(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        result.data(), size);
    return result;
}
#else
std::string to_utf8(const std::wstring& value) {
    return std::string(value.begin(), value.end());
}

std::wstring to_wide(std::string_view value) {
    return std::wstring(value.begin(), value.end());
}
#endif

std::vector<std::string> split(std::string_view value, char delimiter, bool keep_empty) {
    std::vector<std::string> result;
    size_t start = 0;
    while (start <= value.size()) {
        const size_t end = value.find(delimiter, start);
        const size_t length = (end == std::string_view::npos ? value.size() : end) - start;
        if (length > 0 || keep_empty) {
            result.emplace_back(value.substr(start, length));
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return result;
}

std::string join(const std::vector<std::string>& values, std::string_view delimiter) {
    std::string result;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            result.append(delimiter);
        }
        result.append(values[i]);
    }
    return result;
}

std::string replace_all(std::string value, std::string_view from, std::string_view to) {
    if (from.empty()) {
        return value;
    }
    size_t position = 0;
    while ((position = value.find(from, position)) != std::string::npos) {
        value.replace(position, from.size(), to);
        position += to.size();
    }
    return value;
}

std::string trim(std::string_view value) {
    size_t begin = 0;
    size_t end = value.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }
    return std::string(value.substr(begin, end - begin));
}

std::string to_lower_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string to_upper_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return value;
}

bool iequals(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (size_t i = 0; i < left.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(left[i])) !=
            std::tolower(static_cast<unsigned char>(right[i]))) {
            return false;
        }
    }
    return true;
}

std::string quote_windows_arg(std::string_view arg) {
    if (arg.empty()) {
        return "\"\"";
    }

    bool needs_quotes = false;
    for (const char c : arg) {
        if (c == ' ' || c == '\t' || c == '"') {
            needs_quotes = true;
            break;
        }
    }
    if (!needs_quotes) {
        return std::string(arg);
    }

    std::string result;
    result.push_back('"');
    size_t backslashes = 0;
    for (const char c : arg) {
        if (c == '\\') {
            ++backslashes;
            continue;
        }
        if (c == '"') {
            result.append(backslashes * 2 + 1, '\\');
            result.push_back('"');
        } else {
            result.append(backslashes, '\\');
            result.push_back(c);
        }
        backslashes = 0;
    }
    result.append(backslashes * 2, '\\');
    result.push_back('"');
    return result;
}

std::string shell_join(const std::vector<std::string>& args) {
    std::string result;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i != 0) {
            result.push_back(' ');
        }
        result += quote_windows_arg(args[i]);
    }
    return result;
}

std::string format_bytes(unsigned long long bytes) {
    static constexpr std::array<const char*, 5> units = {"B", "KiB", "MiB", "GiB", "TiB"};
    double value = static_cast<double>(bytes);
    size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < units.size()) {
        value /= 1024.0;
        ++unit;
    }
    std::ostringstream out;
    out << std::fixed << std::setprecision(unit == 0 ? 0 : 2) << value << ' ' << units[unit];
    return out.str();
}

std::string now_iso8601() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &time);
#else
    localtime_r(&time, &local);
#endif
    std::ostringstream out;
    out << std::put_time(&local, "%Y-%m-%dT%H:%M:%S");
    return out.str();
}

std::filesystem::path path_from_utf8(std::string_view value) {
#ifdef _WIN32
    return std::filesystem::path(to_wide(value));
#else
    return std::filesystem::path(std::string(value));
#endif
}

std::string path_to_utf8(const std::filesystem::path& value) {
    const auto utf8 = value.u8string();
    return std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
}

} // namespace aurora
