#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace aurora {

struct JavaRuntime {
    std::filesystem::path home;
    std::filesystem::path executable;
    int major = 0;
    std::string version;
    std::string vendor;
    std::string source;
};

std::vector<JavaRuntime> detect_java_runtimes();
std::vector<JavaRuntime> detect_java_runtimes(const std::filesystem::path& game_dir);
std::optional<JavaRuntime> find_java_runtime(int required_major);
std::optional<JavaRuntime> find_java_runtime(int required_major,
                                            const std::filesystem::path& game_dir);
std::optional<JavaRuntime> probe_java_home(const std::filesystem::path& home,
                                           std::string source = {});

} // namespace aurora
