#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace aurora {

std::string build_command_line(const std::filesystem::path& executable,
                               const std::vector<std::string>& arguments);

int run_process(const std::filesystem::path& executable,
                const std::vector<std::string>& arguments,
                const std::filesystem::path& working_directory,
                bool wait = true);

} // namespace aurora
