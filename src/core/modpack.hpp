#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace aurora {

struct ModpackFile {
    // Empty until a CurseForge manifest is resolved through the API.
    std::string path;
    std::string url;
    std::string sha1;
    std::uint64_t size = 0;
    std::uint64_t project_id = 0;
    std::uint64_t file_id = 0;
};

struct ModpackInfo {
    // "modrinth" or "curseforge".
    std::string format;
    std::string name;
    std::string version;
    std::string author;
    std::string summary;
    std::string minecraft_version;
    // fabric, quilt, forge or neoforge. Empty for a vanilla pack.
    std::string loader;
    std::string loader_version;
    std::vector<ModpackFile> files;
    // ZIP prefixes (with trailing slash) that become instance files.
    std::vector<std::string> override_prefixes;
};

ModpackInfo parse_modrinth_index(
    std::string_view json, std::string_view source = "modrinth.index.json");
ModpackInfo parse_curseforge_manifest(std::string_view json,
                                      std::string_view source = "manifest.json");

// Detects modrinth.index.json or manifest.json inside the archive and parses
// it. No network access and no files are written.
ModpackInfo read_modpack_info(const std::filesystem::path& archive);

// Suggested command for installing the required loader. Empty when the pack
// does not name a loader this project supports.
std::string modpack_loader_command(const ModpackInfo& info,
                                   const std::filesystem::path& game_dir);

using ModpackFetcher = std::function<std::string(std::string_view url)>;
using ModpackDownloader = std::function<void(std::string_view url,
                                             const std::filesystem::path& destination,
                                             std::string_view sha1)>;

// Installs the pack loader and returns the version id the instance should
// inherit from. Tests can inject this to avoid network/installer access.
using ModpackLoaderInstaller = std::function<std::string(
    const ModpackInfo&,
    const std::filesystem::path& game_dir)>;

struct ModpackInstallOptions {
    std::filesystem::path archive;
    std::filesystem::path game_dir;
    bool force = false;
    bool install_overrides = true;
    // Installs the loader and downloads the client, libraries and assets, so
    // the resulting version can be launched immediately (HMCL behaviour).
    bool install_loader = true;
    bool install_game = true;
    // Copies the existing instance to <game_dir>/backup before upgrading.
    bool backup = true;
    // Empty means the pack name sanitised as a version id.
    std::string version_id;
    std::filesystem::path java_home;
    int download_retries = 3;
    std::vector<std::string> download_mirrors;
    // Required for CurseForge manifests and never logged.
    std::string curseforge_api_key;
    ModpackFetcher fetch;
    ModpackDownloader download;
    ModpackLoaderInstaller loader_installer;
};

struct ModpackInstallResult {
    ModpackInfo info;
    std::string version_id;
    std::string loader_version_id;
    std::filesystem::path version_json;
    std::filesystem::path instance_dir;
    std::size_t files_installed = 0;
    std::size_t files_skipped = 0;
    std::size_t files_removed = 0;
    std::size_t overrides_extracted = 0;
    std::size_t overrides_skipped = 0;
    std::size_t overrides_removed = 0;
    bool upgraded = false;
    std::filesystem::path backup_path;
    std::filesystem::path manifest_path;
};

// The version id proposed for a pack before sanitisation, matching HMCL's
// habit of naming instances after the modpack.
std::string modpack_version_id(const ModpackInfo& info);

// Installs a pack as a launchable instance under <game_dir>/versions/<id>:
// the loader is installed, the parent version files are downloaded, the pack
// version JSON inherits from the loader, and mods/overrides are extracted into
// the instance directory. Upgrades preserve user-edited overrides, keep files
// the user deleted, and remove entries dropped from the new manifest.
ModpackInstallResult install_modpack(const ModpackInstallOptions& options);

} // namespace aurora
