#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace aurora {

enum class ResourcePackCompatibility {
    Unknown,
    Compatible,
    TooOld,
    TooNew,
    Invalid,
    MissingMetadata,
    MissingGameMetadata,
};

// Minecraft switched from bare file names to "file/<name>" in the 1.13
// options format. pack_format is the client's current resource format; when
// it is 0 the format is derived from minecraft_version or left unknown.
struct ResourcePackContext {
    std::string minecraft_version;
    int pack_format = 0;
    int pack_format_minor = 0;
};

// Reads pack_version from <game_dir>/versions/<version>/<version>.jar and
// falls back to the release table when the JAR is absent.
ResourcePackContext resource_pack_context(const std::filesystem::path& game_dir,
                                          std::string_view minecraft_version);

int minecraft_resource_pack_format(std::string_view minecraft_version);
bool minecraft_uses_new_resource_pack_options(std::string_view minecraft_version);

// Resource packs are ZIP files or directories in <game>/resourcepacks.
struct InstalledResourcePack {
    std::filesystem::path path;
    std::string name;
    bool directory = false;
    bool enabled = false;
    bool has_metadata = false;
    bool has_icon = false;
    int pack_format = 0;
    std::optional<std::pair<int, int>> supported_formats;
    ResourcePackCompatibility compatibility = ResourcePackCompatibility::Unknown;
    std::string description;
};

std::filesystem::path resourcepacks_directory(const std::filesystem::path& game_dir);

// Reads <game>/options.txt and lists the packs present on disk. A missing
// options.txt is treated as "nothing enabled". A malformed resourcePacks
// value disables detection instead of failing the listing.
std::vector<InstalledResourcePack> scan_resource_packs(
    const std::filesystem::path& game_dir,
    const ResourcePackContext& context = {});

// Enables or disables a pack by editing the resourcePacks array in
// options.txt. Other lines (and the file's CRLF/LF style) are preserved.
// The id uses "file/<name>" for 1.13+ and a bare name for older versions.
// Enabling syncs incompatibleResourcePacks with pack.mcmeta compatibility.
struct ResourcePackToggleResult {
    std::string name;
    bool enabled = false;
    bool changed = false;
    std::filesystem::path options_path;
};

ResourcePackToggleResult set_resource_pack_enabled(
    const std::filesystem::path& game_dir,
    std::string_view name,
    bool enabled,
    const ResourcePackContext& context = {});

} // namespace aurora
