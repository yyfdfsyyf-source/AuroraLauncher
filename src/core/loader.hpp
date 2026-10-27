#pragma once

#include "core/json.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace aurora {

// Supported mod loaders. Forge and NeoForge are intentionally out of scope for
// this module: their installers run processors and cannot be represented by a
// single downloaded profile JSON.
enum class LoaderKind { Fabric, Quilt };

enum class LoaderChannel { Stable, Latest };

std::string_view loader_name(LoaderKind kind);
std::string_view loader_display_name(LoaderKind kind);

struct GameVersion {
    std::string id;
    bool stable = true;
};

struct LoaderVersion {
    std::string game_version;
    std::string version;
    std::string maven;
    std::string separator;
    bool stable = true;
};

// Returned text is expected to be the raw response body. An empty fetcher uses
// aurora::http_get, which keeps production callers unchanged.
using LoaderFetcher = std::function<std::string(std::string_view url)>;

// Base URLs without a trailing slash. They are injectable so tests can use
// fixture responses without touching the network.
struct LoaderEndpoints {
    std::string fabric_meta = "https://meta.fabricmc.net";
    std::string quilt_meta = "https://meta.quiltmc.org";
};

struct LoaderInstallOptions {
    LoaderKind kind = LoaderKind::Fabric;
    std::string game_version;
    // Empty means "select the newest version for channel".
    std::string loader_version;
    LoaderChannel channel = LoaderChannel::Stable;
    bool force = false;
    LoaderEndpoints endpoints;
    LoaderFetcher fetch;
};

struct LoaderInstallResult {
    LoaderKind kind = LoaderKind::Fabric;
    std::string id;
    std::string game_version;
    std::string loader_version;
    std::filesystem::path json_path;
    // False when an existing profile was kept because force was not set.
    bool written = false;
};

// Endpoint shapes:
//
// Fabric:
//   GET <fabric_meta>/v2/versions/game
//   GET <fabric_meta>/v2/versions/loader/<game>
//   GET <fabric_meta>/v2/versions/loader/<game>/<loader>/profile/json
//
// Quilt (v3, verified 2026-10-27):
//   GET <quilt_meta>/v3/versions/loader/<game>
//   GET <quilt_meta>/v3/versions/loader/<game>/<loader>/profile/json
//
// Both loader-list endpoints return objects of the form
// [ { "loader": { "version": "0.16.10", "stable": true, ... } }, ... ].
// Quilt's list currently omits the "stable" field. When it is absent, a
// version containing a '-' suffix (for example 0.28.0-beta.1) is treated as
// unstable; a version without a suffix is treated as stable.
std::string fabric_game_versions_url(const LoaderEndpoints& endpoints);
std::string loader_versions_url(LoaderKind kind,
                                std::string_view game_version,
                                const LoaderEndpoints& endpoints);
std::string loader_profile_url(LoaderKind kind,
                               std::string_view game_version,
                               std::string_view loader_version,
                               const LoaderEndpoints& endpoints);

std::vector<GameVersion> parse_game_versions(
    std::string_view json,
    std::string_view source = "Fabric game versions");

std::vector<LoaderVersion> parse_loader_versions(
    LoaderKind kind,
    std::string_view json,
    std::string_view game_version,
    std::string_view source = {});

// Parses and validates a Fabric/Quilt profile, then rewrites its "id" field to
// generated_id. "inheritsFrom" and all other fields are preserved.
Json parse_loader_profile(LoaderKind kind,
                          std::string_view json,
                          std::string_view generated_id,
                          std::string_view source = {});

std::optional<LoaderVersion> select_loader_version(
    const std::vector<LoaderVersion>& versions,
    LoaderChannel channel);

// Compares dotted, semver-like loader versions. Stable releases sort after
// their pre-releases. Exposed for deterministic selection tests.
int compare_loader_versions(std::string_view left, std::string_view right);

// Produces e.g. "fabric-loader-0.16.10-1.21.1" or
// "quilt-loader-0.24.0-1.21.1". Both components are validated as safe path
// segments, so invalid input fails instead of being silently rewritten.
std::string make_loader_version_id(LoaderKind kind,
                                   std::string_view loader_version,
                                   std::string_view game_version);

std::vector<GameVersion> fetch_fabric_game_versions(
    const LoaderEndpoints& endpoints,
    const LoaderFetcher& fetch = {});

std::vector<LoaderVersion> fetch_loader_versions(
    LoaderKind kind,
    std::string_view game_version,
    const LoaderEndpoints& endpoints,
    const LoaderFetcher& fetch = {});

// Downloaded, validated, and with its id normalized.
Json fetch_loader_profile(LoaderKind kind,
                          std::string_view game_version,
                          std::string_view loader_version,
                          const LoaderEndpoints& endpoints,
                          const LoaderFetcher& fetch = {});

// Writes <game_dir>/versions/<id>/<id>.json atomically. An existing profile is
// left untouched unless options.force is true. The parent game version is not
// downloaded or validated here.
LoaderInstallResult install_loader(const std::filesystem::path& game_dir,
                                   const LoaderInstallOptions& options);

} // namespace aurora
