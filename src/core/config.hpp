#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace aurora {

struct Config {
    std::filesystem::path game_dir;
    std::filesystem::path java_path;
    int memory_mb = 2048;
    int width = 854;
    int height = 480;
    std::string username;
    std::vector<std::string> jvm_args;
    std::vector<std::string> game_args;
    bool demo = false;

    static Config defaults();
    static Config load(const std::filesystem::path& path);
    void save(const std::filesystem::path& path) const;
};

} // namespace aurora
