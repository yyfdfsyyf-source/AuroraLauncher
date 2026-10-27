#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace aurora {

std::string to_utf8(const std::wstring& value);
std::wstring to_wide(std::string_view value);

std::vector<std::string> split(std::string_view value, char delimiter, bool keep_empty = false);
std::string join(const std::vector<std::string>& values, std::string_view delimiter);
std::string replace_all(std::string value, std::string_view from, std::string_view to);
std::string trim(std::string_view value);
std::string to_lower_ascii(std::string value);
std::string to_upper_ascii(std::string value);
bool iequals(std::string_view left, std::string_view right);

std::string quote_windows_arg(std::string_view arg);
std::string shell_join(const std::vector<std::string>& args);

std::string format_bytes(unsigned long long bytes);
std::string now_iso8601();

std::filesystem::path path_from_utf8(std::string_view value);
std::string path_to_utf8(const std::filesystem::path& value);

} // namespace aurora
