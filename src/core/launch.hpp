#pragma once

#include "core/repository.hpp"
#include "core/rules.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace aurora {

struct LaunchOptions {
    std::filesystem::path java_path;
    int memory_mb = 2048;
    int width = 854;
    int height = 480;
    std::string username;
    // Populated only for an authenticated Microsoft account. The caller is
    // responsible for keeping these values out of logs and JSON diagnostics.
    bool online = false;
    std::string access_token;
    std::string uuid;
    std::string user_type = "legacy";
    std::string xuid;
    std::string client_id;
    bool demo = false;
    std::vector<std::string> extra_jvm_args;
    std::vector<std::string> extra_game_args;
};

struct LaunchPlan {
    std::filesystem::path java;
    std::filesystem::path working_directory;
    std::vector<std::string> arguments;

    std::string command_line() const;
};

LaunchPlan build_launch_plan(const ResolvedVersion& version,
                             const Environment& environment,
                             const LaunchOptions& options);

int launch_game(const LaunchPlan& plan,
                bool wait = true,
                const std::filesystem::path& output_log = {});

} // namespace aurora
