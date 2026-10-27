#pragma once

#include "core/json.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace aurora {

inline constexpr std::string_view kMojangPistonMetaBaseUrl =
    "https://piston-meta.mojang.com";
inline constexpr std::string_view kOfficialVersionManifestUrl =
    "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json";

struct VersionManifestEntry {
    std::string id;
    std::string type;
    std::string url;
    std::string sha1;
    std::optional<std::string> release_time;
    std::optional<std::string> time;
};

struct VersionManifestLatest {
    std::string release;
    std::string snapshot;
};

struct VersionManifest {
    std::optional<VersionManifestLatest> latest;
    std::vector<VersionManifestEntry> versions;

    static VersionManifest parse(const Json& value, std::string source = {});

    const VersionManifestEntry* find(std::string_view id) const;
    const VersionManifestEntry& require(std::string_view id) const;
    const VersionManifestEntry& latest_release() const;
    const VersionManifestEntry& latest_snapshot() const;
};

VersionManifest parse_version_manifest(std::string_view text,
                                       std::string_view source = "version manifest");

std::string version_manifest_url(std::string_view base_url = kMojangPistonMetaBaseUrl);

VersionManifest fetch_version_manifest(
    std::string_view url = kOfficialVersionManifestUrl);

using VersionJsonDownloader = std::function<void(std::string_view url,
                                                 const std::filesystem::path& destination,
                                                 std::string_view sha1)>;

struct VersionJsonInstallOptions {
    bool force = false;
    VersionJsonDownloader download;
};

struct VersionJsonInstallResult {
    std::filesystem::path path;
    bool downloaded = false;
};

std::filesystem::path version_json_path(const std::filesystem::path& game_dir,
                                        std::string_view id);

VersionJsonInstallResult install_version_json(
    const VersionManifestEntry& entry,
    const std::filesystem::path& game_dir,
    const VersionJsonInstallOptions& options = {});

VersionJsonInstallResult install_version_json(
    const VersionManifest& manifest,
    std::string_view id,
    const std::filesystem::path& game_dir,
    const VersionJsonInstallOptions& options = {});

} // namespace aurora
