#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace aurora {

std::string build_command_line(const std::filesystem::path& executable,
                               const std::vector<std::string>& arguments);

// Runs a process without a shell. When output_log is non-empty, stdout and
// stderr are appended to that file and stdin is redirected from NUL so a
// detached child cannot block waiting for console input.
int run_process(const std::filesystem::path& executable,
                const std::vector<std::string>& arguments,
                const std::filesystem::path& working_directory,
                bool wait = true,
                const std::filesystem::path& output_log = {});

} // namespace aurora
