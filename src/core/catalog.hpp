#pragma once

#include "core/modrinth.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace aurora {

// Content kinds exposed by the download page. Mods keep their dedicated
// install path because they need dependency resolution and loader filtering.
enum class CatalogKind {
    Modpack,
    ResourcePack,
    Shader,
};

std::string catalog_kind_name(CatalogKind kind);
bool catalog_kind_from(std::string_view name, CatalogKind& kind);

// Extension a downloaded file must use for each kind.
std::string_view catalog_file_extension(CatalogKind kind);

std::string modrinth_catalog_search_url(
    CatalogKind kind,
    std::string_view query,
    std::string_view game_version,
    std::string_view loader,
    int limit,
    const ModrinthEndpoints& endpoints = {});
std::string modrinth_catalog_versions_url(
    std::string_view project,
    std::string_view game_version,
    std::string_view loader,
    const ModrinthEndpoints& endpoints = {});

struct CatalogSearchOptions {
    CatalogKind kind = CatalogKind::Modpack;
    std::string query;
    std::string game_version;
    std::string loader;
    int limit = 20;
    ModrinthEndpoints endpoints;
    ModrinthFetcher fetch;
};

ModrinthSearchResult search_modrinth_catalog(const CatalogSearchOptions& options);

struct CatalogVersionQuery {
    CatalogKind kind = CatalogKind::Modpack;
    std::string project;
    std::string game_version;
    std::string loader;
    ModrinthEndpoints endpoints;
    ModrinthFetcher fetch;
};

// Returns only versions that expose a usable file for the requested kind.
std::vector<ModrinthVersion> fetch_modrinth_catalog_versions(
    const CatalogVersionQuery& query);

// The primary file matching the kind's extension, or the first matching file.
const ModrinthFile* select_catalog_file(const ModrinthVersion& version,
                                        CatalogKind kind);

struct CatalogInstallOptions {
    CatalogKind kind = CatalogKind::ResourcePack;
    // Directory that receives the file. The caller chooses resourcepacks/,
    // shaderpacks/ or a temporary directory for modpacks.
    std::filesystem::path target_dir;
    std::string project;
    // Empty selects the newest compatible release.
    std::string version_id;
    // Optional filter for the automatic selection and a safety check when a
    // version id is provided.
    std::string game_version;
    std::string loader;
    // Re-download even when the destination already matches.
    bool force = false;
    int download_retries = 3;
    std::vector<std::string> download_mirrors;
    ModrinthEndpoints endpoints;
    ModrinthFetcher fetch;
    ModrinthDownloader download;
};

struct CatalogInstallResult {
    ModrinthProject project;
    ModrinthVersion version;
    ModrinthFile file;
    std::filesystem::path path;
    bool downloaded = false;
};

// Fetches the project and one version, then downloads its primary file into
// target_dir. The file name, extension, URL and SHA-1 are validated, and a
// file escaping target_dir is rejected.
CatalogInstallResult install_modrinth_catalog_file(
    const CatalogInstallOptions& options);

} // namespace aurora
