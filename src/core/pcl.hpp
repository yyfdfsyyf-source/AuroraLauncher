#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace aurora {

// PCL stores per-instance settings in versions/<id>/PCL/Setup.ini (older
// releases) or PCL/config.v1.yml (PCL CE). Both use flat key:value lines.
struct PclInstanceSettings {
    // True when a PCL configuration file was found.
    bool detected = false;
    // True when the game should run inside versions/<id>. This follows
    // VersionArgumentIndie/VersionArgumentIndieV2 and PCL's fallback rule:
    // a non-empty mods folder or a saves folder containing directories.
    bool isolated = false;
    bool auto_isolated = false;
    std::filesystem::path config_path;
    // PCL writes VersionOriginal for flattened instances, where the version
    // JSON has no inheritsFrom to read the Minecraft version from.
    std::string minecraft_version;
    std::string java_path;
    int memory_mb = 0;
    std::vector<std::string> jvm_args;
    std::vector<std::string> game_args;
};

PclInstanceSettings read_pcl_instance_settings(
    const std::filesystem::path& version_dir);

bool pcl_instance_is_isolated(const std::filesystem::path& version_dir);

// Splits PCL's space-separated argument strings, honouring single and double
// quotes. Empty input yields an empty list.
std::vector<std::string> split_pcl_arguments(std::string_view value);

} // namespace aurora
