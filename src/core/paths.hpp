#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace aurora {

std::filesystem::path home_dir();
std::filesystem::path app_data_dir();
std::filesystem::path default_game_dir();
std::filesystem::path default_config_path();
std::filesystem::path default_log_path();

std::string read_text_file(const std::filesystem::path& path);
void write_text_file(const std::filesystem::path& path, std::string_view content);
void ensure_directory(const std::filesystem::path& path);
std::filesystem::path absolute_path(const std::filesystem::path& path);
bool is_safe_relative_path(std::string_view value);
std::filesystem::path checked_relative_path(std::string_view value, std::string_view context);
std::string environment_value(std::string_view name);

} // namespace aurora
