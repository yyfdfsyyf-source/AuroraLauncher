#include "core/catalog.hpp"

#include "core/error.hpp"
#include "core/hash.hpp"
#include "core/http.hpp"
#include "core/json.hpp"
#include "core/paths.hpp"
#include "core/strings.hpp"

#include <algorithm>
#include <cctype>
#include <system_error>
#include <utility>

namespace aurora {

namespace {

constexpr std::string_view kModpackType = "modpack";
constexpr std::string_view kResourcePackType = "resourcepack";
constexpr std::string_view kShaderType = "shader";

std::string_view project_type_for(CatalogKind kind) {
    switch (kind) {
    case CatalogKind::Modpack:
        return kModpackType;
    case CatalogKind::ResourcePack:
        return kResourcePackType;
    case CatalogKind::Shader:
        return kShaderType;
    }
    return kResourcePackType;
}

std::string url_encode(std::string_view value) {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(value.size());
    for (const unsigned char character : value) {
        const bool unreserved =
            (character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') || character == '-' ||
            character == '_' || character == '.' || character == '~';
        if (unreserved) {
            result.push_back(static_cast<char>(character));
        } else {
            result.push_back('%');
            result.push_back(hex[character >> 4]);
            result.push_back(hex[character & 0x0F]);
        }
    }
    return result;
}

bool is_https_url(std::string_view url) {
    return url.size() >= 8 &&
           to_lower_ascii(std::string(url.substr(0, 8))) == "https://";
}

bool is_sha1_hex(std::string_view value) {
    if (value.size() != 40) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](unsigned char character) {
        return std::isxdigit(character) != 0;
    });
}

bool is_safe_project_ref(std::string_view value) {
    if (value.empty() || value.size() > 128) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](unsigned char character) {
        return std::isalnum(character) != 0 || character == '-' || character == '_';
    });
}

void require_project_ref(std::string_view value, std::string_view label) {
    if (!is_safe_project_ref(value)) {
        fail("Unsafe " + std::string(label) + " '" + std::string(value) + "'");
    }
}

std::string normalized_api_root(const ModrinthEndpoints& endpoints) {
    std::string root = trim(endpoints.api_root);
    while (!root.empty() && root.back() == '/') {
        root.pop_back();
    }
    if (root.empty()) {
        fail("Modrinth API root is empty");
    }
    if (!is_https_url(root)) {
        fail("Modrinth API root must use HTTPS: " + root);
    }
    return root;
}

std::string normalized_game_version(std::string_view game_version) {
    const std::string value = trim(game_version);
    if (value.empty()) {
        return {};
    }
    if (!is_safe_mod_game_version(value)) {
        fail("Unsafe Minecraft version '" + value + "'");
    }
    return value;
}

std::string normalized_loader(std::string_view loader) {
    const std::string value = to_lower_ascii(trim(loader));
    if (value.empty()) {
        return {};
    }
    if (!is_supported_mod_loader(value)) {
        fail("Unsupported mod loader '" + std::string(loader) +
             "' (expected fabric, quilt, forge or neoforge)");
    }
    return value;
}

bool has_extension(std::string_view filename, std::string_view extension) {
    if (filename.size() < extension.size()) {
        return false;
    }
    return to_lower_ascii(std::string(filename.substr(filename.size() - extension.size()))) ==
           std::string(extension);
}

bool is_safe_catalog_filename(std::string_view filename, CatalogKind kind) {
    const std::string_view extension = catalog_file_extension(kind);
    if (filename.empty() || filename.size() > 200 ||
        !has_extension(filename, extension)) {
        return false;
    }
    if (filename.front() == '.' || filename.front() == ' ' ||
        filename.back() == '.' || filename.back() == ' ') {
        return false;
    }
    for (const unsigned char character : filename) {
        if (character < 0x20 || character == 0x7F) {
            return false;
        }
        switch (character) {
        case '/':
        case '\\':
        case ':':
        case '*':
        case '?':
        case '"':
        case '<':
        case '>':
        case '|':
            return false;
        default:
            break;
        }
    }
    const std::string lower = to_lower_ascii(std::string(filename));
    const std::string stem = lower.substr(0, lower.find('.'));
    if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul") {
        return false;
    }
    if (stem.size() == 4 &&
        (stem.compare(0, 3, "com") == 0 || stem.compare(0, 3, "lpt") == 0) &&
        stem[3] >= '1' && stem[3] <= '9') {
        return false;
    }
    return true;
}

void require_project_type(const ModrinthProject& project, CatalogKind kind) {
    if (project.project_type.empty()) {
        return;
    }
    if (project.project_type != project_type_for(kind)) {
        fail("Modrinth project '" + project.slug + "' is a " +
             project.project_type + ", not a " + std::string(project_type_for(kind)));
    }
}

const ModrinthFile* select_file(const ModrinthVersion& version, CatalogKind kind) {
    const ModrinthFile* fallback = nullptr;
    for (const ModrinthFile& file : version.files) {
        if (!is_safe_catalog_filename(file.filename, kind) ||
            !is_sha1_hex(file.sha1) || !is_https_url(file.url)) {
            continue;
        }
        if (file.primary) {
            return &file;
        }
        if (fallback == nullptr) {
            fallback = &file;
        }
    }
    return fallback;
}

bool version_matches_game(const ModrinthVersion& version,
                          std::string_view game_version) {
    if (game_version.empty() || version.game_versions.empty()) {
        return true;
    }
    return std::find(version.game_versions.begin(), version.game_versions.end(),
                     game_version) != version.game_versions.end();
}

ModrinthFetcher default_fetcher(const ModrinthFetcher& fetch) {
    if (fetch) {
        return fetch;
    }
    return [](std::string_view url) { return http_get(url); };
}

ModrinthProject fetch_project(const std::string& project, const ModrinthEndpoints& endpoints,
                              const ModrinthFetcher& fetch) {
    const std::string url = modrinth_project_url(project, endpoints);
    return parse_modrinth_project(fetch(url), url);
}

} // namespace

std::string catalog_kind_name(CatalogKind kind) {
    return std::string(project_type_for(kind));
}

bool catalog_kind_from(std::string_view name, CatalogKind& kind) {
    const std::string value = to_lower_ascii(trim(name));
    if (value == kModpackType) {
        kind = CatalogKind::Modpack;
        return true;
    }
    if (value == kResourcePackType) {
        kind = CatalogKind::ResourcePack;
        return true;
    }
    if (value == kShaderType) {
        kind = CatalogKind::Shader;
        return true;
    }
    return false;
}

std::string_view catalog_file_extension(CatalogKind kind) {
    return kind == CatalogKind::Modpack ? std::string_view(".mrpack")
                                        : std::string_view(".zip");
}

std::string modrinth_catalog_search_url(CatalogKind kind,
                                        std::string_view query,
                                        std::string_view game_version,
                                        std::string_view loader,
                                        int limit,
                                        const ModrinthEndpoints& endpoints) {
    const std::string root = normalized_api_root(endpoints);
    if (limit < 1 || limit > 100) {
        fail("Modrinth search limit must be between 1 and 100");
    }

    Json facets = Json::array();
    facets.push_back(Json::array({"project_type:" + std::string(project_type_for(kind))}));
    const std::string normalized_game = normalized_game_version(game_version);
    if (!normalized_game.empty()) {
        facets.push_back(Json::array({"versions:" + normalized_game}));
    }
    if (kind == CatalogKind::Modpack) {
        const std::string normalized_loader_value = normalized_loader(loader);
        if (!normalized_loader_value.empty()) {
            facets.push_back(
                Json::array({"categories:" + normalized_loader_value}));
        }
    }

    const std::string trimmed_query = trim(query);
    const std::string index = trimmed_query.empty() ? "downloads" : "relevance";
    return root + "/v2/search?query=" + url_encode(trimmed_query) +
           "&limit=" + std::to_string(limit) + "&index=" + index +
           "&facets=" + url_encode(facets.dump());
}

std::string modrinth_catalog_versions_url(std::string_view project,
                                          std::string_view game_version,
                                          std::string_view loader,
                                          const ModrinthEndpoints& endpoints) {
    const std::string root = normalized_api_root(endpoints);
    const std::string project_ref = trim(project);
    require_project_ref(project_ref, "Modrinth project");

    std::string query;
    const std::string normalized_loader_value = normalized_loader(loader);
    if (!normalized_loader_value.empty()) {
        query += "loaders=" +
                 url_encode(Json::array({normalized_loader_value}).dump());
    }
    const std::string normalized_game = normalized_game_version(game_version);
    if (!normalized_game.empty()) {
        if (!query.empty()) {
            query.push_back('&');
        }
        query += "game_versions=" +
                 url_encode(Json::array({normalized_game}).dump());
    }

    std::string url = root + "/v2/project/" + project_ref + "/version";
    if (!query.empty()) {
        url.push_back('?');
        url += query;
    }
    return url;
}

ModrinthSearchResult search_modrinth_catalog(const CatalogSearchOptions& options) {
    const std::string url = modrinth_catalog_search_url(
        options.kind, options.query, options.game_version, options.loader,
        options.limit, options.endpoints);
    const ModrinthFetcher fetch = default_fetcher(options.fetch);
    ModrinthSearchResult result = parse_modrinth_search(fetch(url), url);
    const std::string expected = catalog_kind_name(options.kind);
    std::vector<ModrinthSearchHit> filtered;
    filtered.reserve(result.hits.size());
    for (ModrinthSearchHit& hit : result.hits) {
        if (!hit.project_type.empty() && hit.project_type != expected) {
            continue;
        }
        filtered.push_back(std::move(hit));
    }
    result.hits = std::move(filtered);
    return result;
}

std::vector<ModrinthVersion> fetch_modrinth_catalog_versions(
    const CatalogVersionQuery& query) {
    const std::string url = modrinth_catalog_versions_url(
        query.project, query.game_version,
        query.kind == CatalogKind::Modpack ? query.loader : std::string(),
        query.endpoints);
    const ModrinthFetcher fetch = default_fetcher(query.fetch);
    std::vector<ModrinthVersion> versions =
        parse_modrinth_versions(fetch(url), url);
    std::vector<ModrinthVersion> usable;
    usable.reserve(versions.size());
    for (ModrinthVersion& version : versions) {
        if (select_file(version, query.kind) == nullptr) {
            continue;
        }
        if (!version_matches_game(version, normalized_game_version(query.game_version))) {
            continue;
        }
        usable.push_back(std::move(version));
    }
    return usable;
}

const ModrinthFile* select_catalog_file(const ModrinthVersion& version,
                                        CatalogKind kind) {
    return select_file(version, kind);
}

CatalogInstallResult install_modrinth_catalog_file(
    const CatalogInstallOptions& options) {
    if (options.target_dir.empty()) {
        fail("Catalog install needs a target directory");
    }
    const std::string project_ref = trim(options.project);
    require_project_ref(project_ref, "Modrinth project");
    if (!options.version_id.empty()) {
        require_project_ref(trim(options.version_id), "Modrinth version id");
    }

    const ModrinthFetcher fetch = default_fetcher(options.fetch);
    const ModrinthProject project =
        fetch_project(project_ref, options.endpoints, fetch);
    require_project_type(project, options.kind);

    ModrinthVersion version;
    std::error_code error;
    if (!options.version_id.empty()) {
        const std::string url =
            modrinth_version_url(options.version_id, options.endpoints);
        version = parse_modrinth_version(fetch(url), url);
        if (!version.project_id.empty() && version.project_id != project.id) {
            fail("Modrinth version '" + options.version_id +
                 "' belongs to another project");
        }
        const std::string game_version = normalized_game_version(options.game_version);
        if (!version_matches_game(version, game_version)) {
            fail("Version '" + version.version_number + "' of " + project.slug +
                 " does not target Minecraft " + game_version);
        }
    } else {
        CatalogVersionQuery query;
        query.kind = options.kind;
        query.project = project_ref;
        query.game_version = options.game_version;
        query.loader = options.loader;
        query.endpoints = options.endpoints;
        query.fetch = fetch;
        const std::vector<ModrinthVersion> versions =
            fetch_modrinth_catalog_versions(query);
        const auto selected = select_modrinth_version(versions, "", false);
        if (!selected) {
            fail("No compatible release of '" + project.title + "'" +
                 (options.game_version.empty()
                      ? std::string()
                      : " for Minecraft " + options.game_version));
        }
        version = *selected;
    }

    const ModrinthFile* file = select_catalog_file(version, options.kind);
    if (file == nullptr) {
        fail("Modrinth version " + version.version_number + " of " +
             project.slug + " has no installable " +
             std::string(catalog_file_extension(options.kind)) + " file");
    }

    const std::filesystem::path target_dir = absolute_path(options.target_dir);
    ensure_directory(target_dir);
    const std::filesystem::path destination =
        target_dir / path_from_utf8(file->filename);
    if (destination.parent_path() != target_dir) {
        fail("Downloaded file escapes its target directory: " +
             path_to_utf8(destination));
    }

    HttpDownloadOptions http_options;
    http_options.max_retries = std::max(0, options.download_retries);
    http_options.mirrors = options.download_mirrors;
    ModrinthDownloader downloader = options.download;
    if (!downloader) {
        downloader = [http_options](std::string_view url,
                                    const std::filesystem::path& path,
                                    std::string_view sha1) {
            http_download(url, path, http_options, sha1);
        };
    }

    if (options.force && std::filesystem::is_regular_file(destination, error)) {
        std::filesystem::remove(destination, error);
        error.clear();
    }
    const bool present = std::filesystem::is_regular_file(destination, error);
    bool downloaded = false;
    if (present && checksum_matches(file->sha1, sha1_file(destination))) {
        downloaded = false;
    } else {
        downloader(file->url, destination, file->sha1);
        if (!std::filesystem::is_regular_file(destination, error)) {
            fail("Download did not produce a file: " + path_to_utf8(destination));
        }
        if (file->size > 0) {
            const auto actual_size = std::filesystem::file_size(destination, error);
            if (error || actual_size != file->size) {
                std::filesystem::remove(destination, error);
                fail("Size mismatch after downloading " + file->filename);
            }
        }
        if (!checksum_matches(file->sha1, sha1_file(destination))) {
            std::filesystem::remove(destination, error);
            fail("SHA-1 mismatch after downloading " + file->filename);
        }
        downloaded = true;
    }

    CatalogInstallResult result;
    result.project = project;
    result.version = std::move(version);
    result.file = *file;
    result.path = destination;
    result.downloaded = downloaded;
    return result;
}

} // namespace aurora
