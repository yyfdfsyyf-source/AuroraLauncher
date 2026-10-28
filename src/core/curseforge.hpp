#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace aurora {

struct CurseForgeEndpoints {
    std::string api_root = "https://api.curseforge.com";
};

struct CurseForgeDependency {
    std::uint64_t mod_id = 0;
    // 1 embedded library, 2 optional, 3 required, 4 tool, 5 incompatible,
    // 6 include. Only relation 3 is followed during installation.
    int relation_type = 0;
};

struct CurseForgeFile {
    std::uint64_t id = 0;
    std::uint64_t mod_id = 0;
    std::string display_name;
    std::string filename;
    std::string download_url;
    std::uint64_t length = 0;
    // 1 release, 2 beta, 3 alpha.
    int release_type = 0;
    std::string file_date;
    std::string sha1;
    bool available = true;
    std::vector<CurseForgeDependency> dependencies;
};

struct CurseForgeMod {
    std::uint64_t id = 0;
    std::string name;
    std::string slug;
    std::string summary;
    std::string author;
    std::uint64_t downloads = 0;
    // 6 mods, 12 resource packs, 17 worlds, 6552 shaders, ...
    int class_id = 0;
    bool allow_mod_distribution = false;
};

struct CurseForgeSearchResult {
    std::uint64_t total_count = 0;
    std::vector<CurseForgeMod> mods;
};

// CurseForge mods use gameId 432 and classId 6. Unknown loader names are
// rejected.
int curseforge_loader_type(std::string_view loader);
bool is_safe_curseforge_game_version(std::string_view game_version);

std::string curseforge_search_url(std::string_view query,
                                  std::string_view loader,
                                  std::string_view game_version,
                                  int limit,
                                  const CurseForgeEndpoints& endpoints = {});
std::string curseforge_mod_url(std::uint64_t mod_id,
                               const CurseForgeEndpoints& endpoints = {});
std::string curseforge_files_url(std::uint64_t mod_id,
                                 std::string_view loader,
                                 std::string_view game_version,
                                 const CurseForgeEndpoints& endpoints = {});
std::string curseforge_file_url(std::uint64_t mod_id,
                                std::uint64_t file_id,
                                const CurseForgeEndpoints& endpoints = {});

// CurseForge fingerprint: SHA-agnostic MurmurHash2 (seed 1) over the JAR with
// 0x09, 0x0A, 0x0D and 0x20 removed, as used by HMCL and CurseForge.
std::uint32_t curseforge_fingerprint(const std::filesystem::path& jar);
std::string curseforge_fingerprints_url(
    const CurseForgeEndpoints& endpoints = {});
std::string curseforge_fingerprints_body(
    const std::vector<std::uint32_t>& fingerprints);

struct CurseForgeFingerprintMatch {
    std::uint32_t fingerprint = 0;
    CurseForgeFile file;
};

CurseForgeSearchResult parse_curseforge_search(
    std::string_view json, std::string_view source = "CurseForge search");
CurseForgeMod parse_curseforge_mod(std::string_view json,
                                   std::string_view source = "CurseForge mod");
std::vector<CurseForgeFile> parse_curseforge_files(
    std::string_view json, std::string_view source = "CurseForge files");
CurseForgeFile parse_curseforge_file(std::string_view json,
                                     std::string_view source = "CurseForge file");

// A selectable file must be available, have a safe file name, an HTTPS
// download URL and a SHA-1. Release files are preferred; explicit ids bypass
// the release filter but are still validated.
std::optional<CurseForgeFile> select_curseforge_file(
    const std::vector<CurseForgeFile>& files,
    std::uint64_t explicit_file_id,
    bool allow_prerelease);

// URL-based transport. The default implementation adds the x-api-key header
// through http_get_with_headers and throws HttpStatusError for non-2xx
// responses.
using CurseForgeFetcher = std::function<std::string(std::string_view url)>;
using CurseForgePostFetcher =
    std::function<std::string(std::string_view url, std::string_view body)>;
using CurseForgeDownloader = std::function<void(std::string_view url,
                                                const std::filesystem::path& destination,
                                                std::string_view sha1)>;

std::string curseforge_http_get(std::string_view url, std::string_view api_key);
std::string curseforge_http_post_json(std::string_view url,
                                      std::string_view body,
                                      std::string_view api_key);

std::vector<CurseForgeFingerprintMatch> parse_curseforge_fingerprint_matches(
    std::string_view json,
    const std::vector<std::uint32_t>& requested = {},
    std::string_view source = "CurseForge fingerprints");

// POST /v1/fingerprints/432 in batches. Empty post uses the built-in WinHTTP
// transport with the x-api-key header.
std::vector<CurseForgeFingerprintMatch> lookup_curseforge_fingerprints(
    const std::vector<std::uint32_t>& fingerprints,
    const CurseForgeEndpoints& endpoints,
    std::string_view api_key,
    const CurseForgePostFetcher& post = {});

struct CurseForgeSearchOptions {
    std::string query;
    std::string loader;
    std::string game_version;
    int limit = 10;
    CurseForgeEndpoints endpoints;
    std::string api_key;
    CurseForgeFetcher fetch;
};

CurseForgeSearchResult search_curseforge(const CurseForgeSearchOptions& options);

struct CurseForgeInstallOptions {
    std::filesystem::path game_dir;
    std::uint64_t mod_id = 0;
    // Zero selects the newest matching release.
    std::uint64_t file_id = 0;
    std::string loader;
    std::string game_version;
    bool allow_prerelease = false;
    bool install_dependencies = true;
    bool force = false;
    int download_retries = 3;
    std::vector<std::string> download_mirrors;
    CurseForgeEndpoints endpoints;
    std::string api_key;
    CurseForgeFetcher fetch;
    CurseForgeDownloader download;
};

struct CurseForgeInstalledMod {
    CurseForgeMod mod;
    CurseForgeFile file;
    std::filesystem::path file_path;
    bool downloaded = false;
    bool dependency = false;
};

struct CurseForgeInstallResult {
    CurseForgeInstalledMod root;
    std::vector<CurseForgeInstalledMod> dependencies;
};

// The API key is mandatory unless an injectable fetcher is supplied. Files
// without a download URL (author disabled third-party distribution) are
// rejected instead of guessing a CDN URL.
CurseForgeInstallResult install_curseforge_mod(const CurseForgeInstallOptions& options);

enum class CurseForgeUpdateStatus {
    UpToDate,
    UpdateAvailable,
    NotOnCurseForge,
    Unreadable,
    LookupFailed,
};

struct CurseForgeUpdateOptions {
    std::filesystem::path game_dir;
    std::string loader;
    std::string game_version;
    bool include_disabled = true;
    bool allow_prerelease = false;
    bool apply = false;
    int download_retries = 3;
    std::vector<std::string> download_mirrors;
    CurseForgeEndpoints endpoints;
    std::string api_key;
    CurseForgeFetcher fetch;
    CurseForgePostFetcher post;
    CurseForgeDownloader download;
};

struct CurseForgeUpdateEntry {
    std::string local_file;
    bool enabled = true;
    std::uint32_t fingerprint = 0;
    CurseForgeUpdateStatus status = CurseForgeUpdateStatus::NotOnCurseForge;
    std::optional<CurseForgeFile> current;
    std::optional<CurseForgeFile> latest;
    std::filesystem::path installed_file;
    bool applied = false;
    std::string error;
};

struct CurseForgeUpdateResult {
    std::size_t updates = 0;
    std::size_t up_to_date = 0;
    std::size_t not_on_curseforge = 0;
    std::size_t failures = 0;
    std::size_t applied = 0;
    std::vector<CurseForgeUpdateEntry> entries;
};

// Identifies local JARs by fingerprint, then compares them with the newest
// file for the requested loader/game version. --apply replaces untouched
// files while keeping the enabled/disabled state.
CurseForgeUpdateResult check_curseforge_updates(
    const CurseForgeUpdateOptions& options);

} // namespace aurora
