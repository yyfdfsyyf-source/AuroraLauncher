#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace aurora {

struct ModrinthEndpoints {
    std::string api_root = "https://api.modrinth.com";
};

struct ModrinthSearchHit {
    std::string project_id;
    std::string slug;
    std::string title;
    std::string description;
    std::string author;
    std::uint64_t downloads = 0;
    std::vector<std::string> categories;
};

struct ModrinthSearchResult {
    std::uint64_t total_hits = 0;
    std::vector<ModrinthSearchHit> hits;
};

struct ModrinthProject {
    std::string id;
    std::string slug;
    std::string title;
    std::string description;
    std::string project_type;
};

struct ModrinthDependency {
    std::string project_id;
    std::string version_id;
    std::string dependency_type;
};

struct ModrinthFile {
    std::string url;
    std::string filename;
    std::uint64_t size = 0;
    std::string sha1;
    std::string sha512;
    bool primary = false;
};

struct ModrinthVersion {
    std::string id;
    std::string project_id;
    std::string name;
    std::string version_number;
    std::string version_type;
    std::string date_published;
    std::vector<std::string> game_versions;
    std::vector<std::string> loaders;
    std::vector<ModrinthFile> files;
    std::vector<ModrinthDependency> dependencies;
};

// Loader names accepted by Modrinth's "categories" facet and version filter.
bool is_supported_mod_loader(std::string_view loader);
bool is_safe_mod_game_version(std::string_view game_version);

std::string modrinth_search_url(std::string_view query,
                                std::string_view loader,
                                std::string_view game_version,
                                int limit,
                                const ModrinthEndpoints& endpoints = {});
std::string modrinth_project_url(std::string_view project,
                                 const ModrinthEndpoints& endpoints = {});
std::string modrinth_project_versions_url(std::string_view project,
                                          std::string_view loader,
                                          std::string_view game_version,
                                          const ModrinthEndpoints& endpoints = {});
std::string modrinth_version_url(std::string_view version_id,
                                 const ModrinthEndpoints& endpoints = {});
std::string modrinth_version_file_url(std::string_view sha1,
                                      const ModrinthEndpoints& endpoints = {});

ModrinthSearchResult parse_modrinth_search(
    std::string_view json, std::string_view source = "Modrinth search");
ModrinthProject parse_modrinth_project(
    std::string_view json, std::string_view source = "Modrinth project");
ModrinthVersion parse_modrinth_version(
    std::string_view json, std::string_view source = "Modrinth version");
std::vector<ModrinthVersion> parse_modrinth_versions(
    std::string_view json, std::string_view source = "Modrinth versions");

// Without an explicit version id, release versions are preferred. When
// allow_prerelease is false and only alpha/beta versions exist, no version is
// selected. The newest matching version is chosen by date_published.
std::optional<ModrinthVersion> select_modrinth_version(
    const std::vector<ModrinthVersion>& versions,
    std::string_view requested_version_id,
    bool allow_prerelease);
const ModrinthFile* select_modrinth_file(const ModrinthVersion& version);

using ModrinthFetcher = std::function<std::string(std::string_view url)>;
using ModrinthDownloader = std::function<void(std::string_view url,
                                              const std::filesystem::path& destination,
                                              std::string_view sha1)>;

struct ModrinthSearchOptions {
    std::string query;
    std::string loader;
    std::string game_version;
    int limit = 10;
    ModrinthEndpoints endpoints;
    ModrinthFetcher fetch;
};

ModrinthSearchResult search_modrinth(const ModrinthSearchOptions& options);

struct ModrinthInstallOptions {
    std::filesystem::path game_dir;
    std::string project;
    std::string loader;
    std::string game_version;
    std::string version_id;
    bool allow_prerelease = false;
    bool install_dependencies = true;
    bool force = false;
    int download_retries = 3;
    std::vector<std::string> download_mirrors;
    ModrinthEndpoints endpoints;
    ModrinthFetcher fetch;
    ModrinthDownloader download;
};

struct ModrinthInstalledMod {
    ModrinthProject project;
    ModrinthVersion version;
    ModrinthFile file;
    std::filesystem::path file_path;
    bool downloaded = false;
    bool dependency = false;
};

struct ModrinthInstallResult {
    ModrinthInstalledMod root;
    std::vector<ModrinthInstalledMod> dependencies;
};

// Installs the selected mod and, by default, its required Modrinth
// dependencies into <game_dir>/mods. Every file is verified with SHA-1 and
// must stay inside that directory.
ModrinthInstallResult install_modrinth_mod(const ModrinthInstallOptions& options);

enum class ModrinthUpdateStatus {
    UpToDate,
    UpdateAvailable,
    NotOnModrinth,
    // The local JAR could not be hashed.
    Unreadable,
    LookupFailed,
};

struct ModrinthUpdateOptions {
    std::filesystem::path game_dir;
    std::string loader;
    std::string game_version;
    // Include prerelease versions when choosing the newest candidate.
    bool allow_prerelease = false;
    bool include_disabled = true;
    // Download and verify available updates. The superseded local file is
    // removed only after the replacement was verified.
    bool apply = false;
    int download_retries = 3;
    std::vector<std::string> download_mirrors;
    ModrinthEndpoints endpoints;
    ModrinthFetcher fetch;
    ModrinthDownloader download;
};

struct ModrinthUpdateEntry {
    ModrinthUpdateStatus status = ModrinthUpdateStatus::LookupFailed;
    std::string local_file;
    bool local_enabled = true;
    std::string sha1;
    std::optional<ModrinthVersion> current;
    std::optional<ModrinthVersion> latest;
    std::optional<ModrinthFile> latest_file;
    bool applied = false;
    std::filesystem::path installed_file;
    std::string error;
};

struct ModrinthUpdateCheckResult {
    std::vector<ModrinthUpdateEntry> entries;
    std::size_t updates = 0;
    std::size_t up_to_date = 0;
    std::size_t not_on_modrinth = 0;
    std::size_t applied = 0;
    std::size_t failures = 0;
};

// Hashes every local mod and looks each one up through Modrinth's
// /v2/version_file endpoint. Unknown hashes are reported as NotOnModrinth
// instead of aborting the whole check. With options.apply the newest file is
// downloaded, SHA-1 verified and the superseded local file is removed; the
// disabled state is preserved. Without apply the check is read-only.
ModrinthUpdateCheckResult check_modrinth_updates(
    const ModrinthUpdateOptions& options);

} // namespace aurora
