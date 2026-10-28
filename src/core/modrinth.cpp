#include "core/modrinth.hpp"

#include "core/error.hpp"
#include "core/hash.hpp"
#include "core/http.hpp"
#include "core/json.hpp"
#include "core/log.hpp"
#include "core/mods.hpp"
#include "core/paths.hpp"
#include "core/strings.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <system_error>
#include <utility>

namespace aurora {

namespace {

bool is_https_url(std::string_view url) {
    return url.size() >= 8 &&
           to_lower_ascii(std::string(url.substr(0, 8))) == "https://";
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

bool is_sha1_hex(std::string_view value) {
    if (value.size() != 40) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](unsigned char character) {
        return std::isxdigit(character) != 0;
    });
}

bool is_alphanumeric_ref_char(unsigned char character) {
    return std::isalnum(character) != 0 || character == '-' || character == '_';
}

bool is_safe_project_ref(std::string_view value) {
    if (value.empty() || value.size() > 128) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](unsigned char character) {
        return is_alphanumeric_ref_char(character);
    });
}

void require_safe_project_ref(std::string_view value, std::string_view label) {
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

std::string normalize_loader(std::string_view loader) {
    const std::string normalized = to_lower_ascii(trim(loader));
    if (!is_supported_mod_loader(normalized)) {
        fail("Unsupported mod loader '" + std::string(loader) +
             "' (expected fabric, quilt, forge or neoforge)");
    }
    return normalized;
}

std::string normalize_game_version(std::string_view game_version) {
    const std::string normalized = trim(game_version);
    if (normalized.empty()) {
        fail("A Minecraft version is required");
    }
    if (!is_safe_mod_game_version(normalized)) {
        fail("Unsafe Minecraft version '" + normalized + "'");
    }
    return normalized;
}

std::vector<std::string> parse_string_array(const Json& object, const char* key) {
    std::vector<std::string> values;
    const Json* array = json_array(object, key);
    if (array == nullptr) {
        return values;
    }
    for (const Json& item : *array) {
        if (item.is_string()) {
            values.push_back(item.get<std::string>());
        }
    }
    return values;
}

std::uint64_t json_size(const Json& object, const char* key) {
    return static_cast<std::uint64_t>(
        std::max<std::int64_t>(0, json_integer(object, key)));
}

ModrinthFile parse_file(const Json& value) {
    ModrinthFile file;
    file.url = json_string(value, "url");
    file.filename = json_string(value, "filename");
    file.size = json_size(value, "size");
    file.primary = json_boolean(value, "primary");
    if (const Json* hashes = json_object(value, "hashes")) {
        file.sha1 = to_lower_ascii(json_string(*hashes, "sha1"));
        file.sha512 = to_lower_ascii(json_string(*hashes, "sha512"));
    }
    return file;
}

ModrinthVersion parse_version_object(const Json& value, std::string_view source) {
    if (!value.is_object()) {
        fail("Modrinth version response from " + std::string(source) +
             " contains a non-object entry");
    }

    ModrinthVersion version;
    version.id = json_string(value, "id");
    if (version.id.empty()) {
        fail("Modrinth version from " + std::string(source) + " has no id");
    }
    version.project_id = json_string(value, "project_id");
    version.name = json_string(value, "name");
    version.version_number = json_string(value, "version_number");
    version.version_type = json_string(value, "version_type");
    version.date_published = json_string(value, "date_published");
    version.game_versions = parse_string_array(value, "game_versions");
    version.loaders = parse_string_array(value, "loaders");

    if (const Json* files = json_array(value, "files")) {
        for (const Json& item : *files) {
            ModrinthFile file = parse_file(item);
            if (file.url.empty() || file.filename.empty()) {
                continue;
            }
            version.files.push_back(std::move(file));
        }
    }
    if (const Json* dependencies = json_array(value, "dependencies")) {
        for (const Json& item : *dependencies) {
            ModrinthDependency dependency;
            dependency.project_id = json_string(item, "project_id");
            dependency.version_id = json_string(item, "version_id");
            dependency.dependency_type = json_string(item, "dependency_type");
            if (dependency.project_id.empty() && dependency.version_id.empty()) {
                continue;
            }
            version.dependencies.push_back(std::move(dependency));
        }
    }
    return version;
}

struct InstallContext {
    const ModrinthInstallOptions& options;
    std::filesystem::path mods_dir;
    std::string loader;
    std::string game_version;
    ModrinthFetcher fetch;
    ModrinthDownloader download;
    std::set<std::string> installed_projects;
    std::set<std::string> installed_versions;
    std::map<std::string, std::string> file_owners;
    ModrinthInstallResult result;
};

std::string fetch_text(InstallContext& context, const std::string& url) {
    return context.fetch(url);
}

ModrinthProject fetch_project(InstallContext& context, std::string_view project) {
    const std::string url = modrinth_project_url(project, context.options.endpoints);
    return parse_modrinth_project(fetch_text(context, url), url);
}

std::vector<ModrinthVersion> fetch_project_versions(InstallContext& context,
                                                    std::string_view project) {
    const std::string url = modrinth_project_versions_url(
        project, context.loader, context.game_version, context.options.endpoints);
    return parse_modrinth_versions(fetch_text(context, url), url);
}

ModrinthVersion fetch_version(InstallContext& context, std::string_view version_id) {
    const std::string url = modrinth_version_url(version_id, context.options.endpoints);
    return parse_modrinth_version(fetch_text(context, url), url);
}

ModrinthInstalledMod install_file(InstallContext& context,
                                  const ModrinthProject& project,
                                  const ModrinthVersion& version,
                                  bool dependency) {
    const ModrinthFile* file = select_modrinth_file(version);
    if (file == nullptr) {
        fail("Modrinth version " + version.version_number + " of " + project.slug +
             " has no usable .jar file with a SHA-1");
    }
    if (!is_https_url(file->url)) {
        fail("Refusing to download a mod from a non-HTTPS URL: " + file->url);
    }
    if (!is_safe_mod_filename(file->filename)) {
        fail("Unsafe mod file name: " + file->filename);
    }

    const std::filesystem::path destination =
        context.mods_dir / path_from_utf8(file->filename);
    if (destination.parent_path() != context.mods_dir) {
        fail("Mod file escapes the mods directory: " + path_to_utf8(destination));
    }
    const std::string file_key = to_lower_ascii(file->filename);
    const auto owner = context.file_owners.find(file_key);
    if (owner != context.file_owners.end() && owner->second != project.id) {
        fail("Mods '" + owner->second + "' and '" + project.id +
             "' would write the same file: " + file->filename);
    }
    context.file_owners[file_key] = project.id;

    bool downloaded = false;
    std::error_code error;
    if (context.options.force) {
        std::filesystem::remove(destination, error);
        error.clear();
    }
    const bool present =
        !context.options.force && std::filesystem::is_regular_file(destination, error);
    if (present && sha1_file(destination) == file->sha1) {
        downloaded = false;
    } else {
        context.download(file->url, destination, file->sha1);
        if (!std::filesystem::is_regular_file(destination, error)) {
            fail("Mod download did not produce a file: " + path_to_utf8(destination));
        }
        if (file->size > 0) {
            const auto actual_size = std::filesystem::file_size(destination, error);
            if (error || actual_size != file->size) {
                std::filesystem::remove(destination, error);
                fail("Size mismatch after downloading " + file->filename);
            }
        }
        if (sha1_file(destination) != file->sha1) {
            std::filesystem::remove(destination, error);
            fail("SHA-1 mismatch after downloading " + file->filename);
        }
        downloaded = true;
    }

    ModrinthInstalledMod installed;
    installed.project = project;
    installed.version = version;
    installed.file = *file;
    installed.file_path = destination;
    installed.downloaded = downloaded;
    installed.dependency = dependency;
    return installed;
}

void install_dependencies(InstallContext& context,
                          const ModrinthProject& parent_project,
                          const ModrinthVersion& parent_version,
                          int depth) {
    if (!context.options.install_dependencies) {
        return;
    }
    if (depth >= 16) {
        fail("Modrinth dependency chain is too deep at " + parent_project.slug);
    }

    for (const ModrinthDependency& dependency : parent_version.dependencies) {
        if (dependency.dependency_type != "required") {
            continue;
        }
        if (dependency.project_id.empty() && dependency.version_id.empty()) {
            log_warn("Required dependency of " + parent_project.slug +
                     " has no project or version id; skipping");
            continue;
        }

        std::optional<ModrinthVersion> dependency_version;
        if (!dependency.version_id.empty()) {
            dependency_version = fetch_version(context, dependency.version_id);
        } else {
            const auto versions = fetch_project_versions(context, dependency.project_id);
            dependency_version = select_modrinth_version(
                versions, "", context.options.allow_prerelease);
            if (!dependency_version) {
                fail("No compatible version of required dependency '" +
                     dependency.project_id + "' for " + context.loader + " " +
                     context.game_version);
            }
        }

        const std::string project_id = !dependency_version->project_id.empty()
                                           ? dependency_version->project_id
                                           : dependency.project_id;
        if (project_id.empty()) {
            fail("Required dependency of " + parent_project.slug + " has no project id");
        }
        if (context.installed_projects.contains(project_id) ||
            context.installed_versions.contains(dependency_version->id)) {
            continue;
        }

        ModrinthProject dependency_project;
        dependency_project.id = project_id;
        dependency_project.slug = project_id;
        dependency_project.title = project_id;
        try {
            dependency_project = fetch_project(context, project_id);
        } catch (const Error& error) {
            log_warn("Unable to fetch metadata for dependency " + project_id + ": " +
                     error.what());
            dependency_project.id = project_id;
            dependency_project.slug = project_id;
            dependency_project.title = project_id;
        }
        dependency_project.id = project_id;
        if (!dependency_project.project_type.empty() &&
            dependency_project.project_type != "mod") {
            fail("Required dependency '" + dependency_project.slug + "' is a " +
                 dependency_project.project_type + ", not a mod");
        }
        if (dependency_project.title.empty()) {
            dependency_project.title = dependency_project.slug;
        }

        context.installed_projects.insert(project_id);
        context.installed_versions.insert(dependency_version->id);
        ModrinthInstalledMod installed = install_file(
            context, dependency_project, *dependency_version, true);
        context.result.dependencies.push_back(std::move(installed));
        install_dependencies(
            context, dependency_project, *dependency_version, depth + 1);
    }
}

} // namespace

bool is_supported_mod_loader(std::string_view loader) {
    const std::string normalized = to_lower_ascii(trim(loader));
    return normalized == "fabric" || normalized == "quilt" ||
           normalized == "forge" || normalized == "neoforge";
}

bool is_safe_mod_game_version(std::string_view game_version) {
    const std::string value = trim(game_version);
    if (value.empty() || value.size() > 64) {
        return false;
    }
    if (value.front() == '.' || value.back() == '.') {
        return false;
    }
    for (const unsigned char character : value) {
        const bool allowed =
            std::isalnum(character) != 0 || character == '.' || character == '_' ||
            character == '-' || character == '+';
        if (!allowed) {
            return false;
        }
    }
    return true;
}

std::string modrinth_search_url(std::string_view query,
                                std::string_view loader,
                                std::string_view game_version,
                                int limit,
                                const ModrinthEndpoints& endpoints) {
    const std::string root = normalized_api_root(endpoints);
    if (limit < 1 || limit > 100) {
        fail("Modrinth search limit must be between 1 and 100");
    }

    Json facets = Json::array();
    facets.push_back(Json::array({"project_type:mod"}));
    if (!trim(loader).empty()) {
        facets.push_back(
            Json::array({"categories:" + normalize_loader(loader)}));
    }
    if (!trim(game_version).empty()) {
        facets.push_back(
            Json::array({"versions:" + normalize_game_version(game_version)}));
    }

    std::string url = root + "/v2/search?query=" + url_encode(trim(query)) +
                      "&limit=" + std::to_string(limit) +
                      "&index=relevance&facets=" + url_encode(facets.dump());
    return url;
}

std::string modrinth_project_url(std::string_view project,
                                 const ModrinthEndpoints& endpoints) {
    const std::string root = normalized_api_root(endpoints);
    const std::string project_ref = trim(project);
    require_safe_project_ref(project_ref, "Modrinth project");
    return root + "/v2/project/" + project_ref;
}

std::string modrinth_project_versions_url(std::string_view project,
                                          std::string_view loader,
                                          std::string_view game_version,
                                          const ModrinthEndpoints& endpoints) {
    const std::string root = normalized_api_root(endpoints);
    const std::string project_ref = trim(project);
    require_safe_project_ref(project_ref, "Modrinth project");

    std::string query;
    if (!trim(loader).empty()) {
        query += "loaders=" +
                 url_encode(Json::array({normalize_loader(loader)}).dump());
    }
    if (!trim(game_version).empty()) {
        if (!query.empty()) {
            query.push_back('&');
        }
        query += "game_versions=" +
                 url_encode(Json::array({normalize_game_version(game_version)}).dump());
    }

    std::string url = root + "/v2/project/" + project_ref + "/version";
    if (!query.empty()) {
        url.push_back('?');
        url += query;
    }
    return url;
}

std::string modrinth_version_url(std::string_view version_id,
                                 const ModrinthEndpoints& endpoints) {
    const std::string root = normalized_api_root(endpoints);
    const std::string id = trim(version_id);
    require_safe_project_ref(id, "Modrinth version id");
    return root + "/v2/version/" + id;
}

std::string modrinth_version_file_url(std::string_view sha1,
                                      const ModrinthEndpoints& endpoints) {
    const std::string root = normalized_api_root(endpoints);
    const std::string hash = to_lower_ascii(trim(sha1));
    if (!is_sha1_hex(hash)) {
        fail("Invalid SHA-1 for Modrinth version lookup");
    }
    return root + "/v2/version_file/" + hash + "?algorithm=sha1";
}

ModrinthSearchResult parse_modrinth_search(std::string_view json,
                                           std::string_view source) {
    const Json root = parse_json(json, source);
    if (!root.is_object()) {
        fail("Modrinth search response from " + std::string(source) +
             " must be an object");
    }

    ModrinthSearchResult result;
    result.total_hits = json_size(root, "total_hits");
    const Json* hits = json_array(root, "hits");
    if (hits == nullptr) {
        fail("Modrinth search response from " + std::string(source) +
             " has no hits array");
    }

    for (const Json& item : *hits) {
        if (!item.is_object()) {
            fail("Modrinth search response from " + std::string(source) +
                 " contains a non-object hit");
        }
        ModrinthSearchHit hit;
        hit.project_id = json_string(item, "project_id");
        hit.slug = json_string(item, "slug");
        hit.title = json_string(item, "title");
        hit.description = json_string(item, "description");
        hit.author = json_string(item, "author");
        hit.downloads = json_size(item, "downloads");
        hit.categories = parse_string_array(item, "categories");
        if (hit.slug.empty() && hit.project_id.empty()) {
            fail("Modrinth search result from " + std::string(source) +
                 " contains a hit without slug or project id");
        }
        if (hit.title.empty()) {
            hit.title = hit.slug.empty() ? hit.project_id : hit.slug;
        }
        result.hits.push_back(std::move(hit));
    }
    return result;
}

ModrinthProject parse_modrinth_project(std::string_view json,
                                       std::string_view source) {
    const Json root = parse_json(json, source);
    if (!root.is_object()) {
        fail("Modrinth project response from " + std::string(source) +
             " must be an object");
    }

    ModrinthProject project;
    project.id = json_string(root, "id");
    project.slug = json_string(root, "slug");
    project.title = json_string(root, "title");
    project.description = json_string(root, "description");
    project.project_type = json_string(root, "project_type");
    if (project.id.empty()) {
        fail("Modrinth project from " + std::string(source) + " has no id");
    }
    if (project.slug.empty()) {
        project.slug = project.id;
    }
    if (project.title.empty()) {
        project.title = project.slug;
    }
    return project;
}

ModrinthVersion parse_modrinth_version(std::string_view json,
                                       std::string_view source) {
    return parse_version_object(parse_json(json, source), source);
}

std::vector<ModrinthVersion> parse_modrinth_versions(std::string_view json,
                                                     std::string_view source) {
    const Json root = parse_json(json, source);
    if (!root.is_array()) {
        fail("Modrinth versions response from " + std::string(source) +
             " must be an array");
    }
    std::vector<ModrinthVersion> versions;
    versions.reserve(root.size());
    for (const Json& item : root) {
        versions.push_back(parse_version_object(item, source));
    }
    return versions;
}

std::optional<ModrinthVersion> select_modrinth_version(
    const std::vector<ModrinthVersion>& versions,
    std::string_view requested_version_id,
    bool allow_prerelease) {
    const std::string requested = trim(requested_version_id);
    if (!requested.empty()) {
        for (const ModrinthVersion& version : versions) {
            if (version.id == requested) {
                return version;
            }
        }
        return std::nullopt;
    }

    std::vector<const ModrinthVersion*> eligible;
    for (const ModrinthVersion& version : versions) {
        if (!allow_prerelease && version.version_type != "release") {
            continue;
        }
        eligible.push_back(&version);
    }
    if (eligible.empty()) {
        return std::nullopt;
    }
    std::stable_sort(
        eligible.begin(),
        eligible.end(),
        [](const ModrinthVersion* left, const ModrinthVersion* right) {
            return left->date_published > right->date_published;
        });
    return *eligible.front();
}

const ModrinthFile* select_modrinth_file(const ModrinthVersion& version) {
    const ModrinthFile* fallback = nullptr;
    for (const ModrinthFile& file : version.files) {
        if (!is_safe_mod_filename(file.filename) || !is_sha1_hex(file.sha1) ||
            !is_https_url(file.url)) {
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

ModrinthSearchResult search_modrinth(const ModrinthSearchOptions& options) {
    const std::string url = modrinth_search_url(
        options.query, options.loader, options.game_version, options.limit,
        options.endpoints);
    const std::string body = options.fetch ? options.fetch(url) : http_get(url);
    return parse_modrinth_search(body, url);
}

ModrinthInstallResult install_modrinth_mod(const ModrinthInstallOptions& options) {
    if (options.game_dir.empty()) {
        fail("Game directory is empty");
    }
    const std::string project_ref = trim(options.project);
    require_safe_project_ref(project_ref, "Modrinth project");
    const std::string loader = normalize_loader(options.loader);
    const std::string game_version = normalize_game_version(options.game_version);
    if (!options.version_id.empty()) {
        require_safe_project_ref(trim(options.version_id), "Modrinth version id");
    }

    const std::filesystem::path game_root = absolute_path(options.game_dir);
    ensure_directory(game_root);
    const std::filesystem::path mods_dir = game_root / "mods";
    ensure_directory(mods_dir);

    HttpDownloadOptions http_options;
    http_options.max_retries = std::max(0, options.download_retries);
    http_options.mirrors = options.download_mirrors;

    InstallContext context{
        options,
        mods_dir,
        loader,
        game_version,
        options.fetch
            ? options.fetch
            : ModrinthFetcher([](std::string_view url) { return http_get(url); }),
        options.download
            ? options.download
            : ModrinthDownloader([http_options](std::string_view url,
                                               const std::filesystem::path& destination,
                                               std::string_view sha1) {
                  http_download(url, destination, http_options, sha1);
              }),
        {},
        {},
        {},
        {},
    };

    ModrinthProject root_project = fetch_project(context, project_ref);
    if (!root_project.project_type.empty() && root_project.project_type != "mod") {
        fail("Modrinth project '" + root_project.slug + "' is a " +
             root_project.project_type + ", not a mod");
    }

    const auto versions = fetch_project_versions(context, project_ref);
    const auto version = select_modrinth_version(
        versions, options.version_id, options.allow_prerelease);
    if (!version) {
        if (!options.version_id.empty()) {
            fail("Modrinth version '" + trim(options.version_id) +
                 "' was not found for project '" + root_project.slug + "'");
        }
        if (options.allow_prerelease) {
            fail("No compatible version of '" + root_project.title + "' for " +
                 loader + " " + game_version);
        }
        fail("No release version of '" + root_project.title + "' for " + loader +
             " " + game_version +
             ". Use --prerelease to include alpha/beta versions.");
    }

    context.installed_projects.insert(root_project.id);
    context.installed_versions.insert(version->id);
    context.result.root = install_file(context, root_project, *version, false);
    install_dependencies(context, root_project, *version, 0);
    return std::move(context.result);
}

namespace {

struct VersionFileLookup {
    bool found = false;
    bool not_found = false;
    std::string error;
    ModrinthVersion version;
};

VersionFileLookup lookup_version_file(std::string_view sha1,
                                      const ModrinthFetcher& fetcher,
                                      const ModrinthEndpoints& endpoints) {
    VersionFileLookup result;
    try {
        result.version =
            parse_modrinth_version(fetcher(modrinth_version_file_url(sha1, endpoints)));
        result.found = true;
    } catch (const HttpStatusError& error) {
        if (error.status() == 404) {
            result.not_found = true;
        } else {
            result.error = error.what();
        }
    } catch (const Error& error) {
        result.error = error.what();
    }
    return result;
}

struct LatestVersionLookup {
    bool failed = false;
    std::string error;
    std::optional<ModrinthVersion> version;
};

bool is_newer_than(const ModrinthVersion& candidate, const ModrinthVersion& current) {
    if (candidate.id == current.id) {
        return false;
    }
    if (!candidate.date_published.empty() && !current.date_published.empty()) {
        return candidate.date_published > current.date_published;
    }
    return true;
}

bool same_file_path(const std::filesystem::path& left, const std::filesystem::path& right) {
    return to_lower_ascii(path_to_utf8(left.lexically_normal())) ==
           to_lower_ascii(path_to_utf8(right.lexically_normal()));
}

struct ApplyOutcome {
    bool applied = false;
    std::filesystem::path installed;
    std::string error;
};

ApplyOutcome apply_update_file(const std::filesystem::path& mods_dir,
                               const std::filesystem::path& local_path,
                               bool local_enabled,
                               const ModrinthFile& file,
                               const ModrinthDownloader& downloader) {
    ApplyOutcome outcome;
    if (!is_safe_mod_filename(file.filename) || !is_sha1_hex(file.sha1) ||
        !is_https_url(file.url)) {
        outcome.error = "the newest file is not a safe, verifiable HTTPS JAR";
        return outcome;
    }
    const std::string target_name =
        local_enabled ? file.filename : file.filename + ".disabled";
    const std::filesystem::path target = mods_dir / path_from_utf8(target_name);

    std::error_code error;
    std::filesystem::directory_iterator iterator(mods_dir, error);
    if (error) {
        outcome.error = "cannot read mods directory: " + error.message();
        return outcome;
    }
    const std::filesystem::directory_iterator end;
    while (iterator != end) {
        std::error_code status_error;
        const auto status = iterator->symlink_status(status_error);
        if (!status_error && std::filesystem::is_regular_file(status) &&
            !same_file_path(iterator->path(), local_path) &&
            iequals(path_to_utf8(iterator->path().filename()), target_name)) {
            outcome.error = "target already exists: " + path_to_utf8(iterator->path());
            return outcome;
        }
        iterator.increment(error);
        if (error) {
            outcome.error = "cannot read mods directory: " + error.message();
            return outcome;
        }
    }

    try {
        downloader(file.url, target, file.sha1);
        if (!checksum_matches(file.sha1, sha1_file(target))) {
            outcome.error = "downloaded file did not match its SHA-1";
            return outcome;
        }
    } catch (const Error& download_error) {
        outcome.error = download_error.what();
        return outcome;
    }

    if (!same_file_path(target, local_path)) {
        if (std::filesystem::is_regular_file(local_path, error)) {
            std::filesystem::remove(local_path, error);
            if (error) {
                outcome.applied = true;
                outcome.installed = target;
                outcome.error = "installed " + path_to_utf8(target) +
                                ", but could not remove the old file: " +
                                error.message();
                return outcome;
            }
        }
    }
    outcome.applied = true;
    outcome.installed = target;
    return outcome;
}

} // namespace

ModrinthUpdateCheckResult check_modrinth_updates(
    const ModrinthUpdateOptions& options) {
    if (options.game_dir.empty()) {
        fail("Game directory is empty");
    }
    const std::string loader = normalize_loader(options.loader);
    const std::string game_version = normalize_game_version(options.game_version);
    ModrinthFetcher fetcher = options.fetch;
    if (!fetcher) {
        fetcher = [](std::string_view url) { return http_get(url); };
    }
    ModrinthDownloader downloader = options.download;
    if (options.apply && !downloader) {
        HttpDownloadOptions http_options;
        http_options.max_retries = std::max(0, options.download_retries);
        http_options.mirrors = options.download_mirrors;
        downloader = [http_options](std::string_view url,
                                    const std::filesystem::path& destination,
                                    std::string_view sha1) {
            http_download(url, destination, http_options, sha1);
        };
    }

    const std::vector<InstalledMod> mods = scan_mods(options.game_dir, true);
    const std::filesystem::path mods_dir = mods_directory(options.game_dir);
    ModrinthUpdateCheckResult result;
    std::map<std::string, VersionFileLookup> version_cache;
    std::map<std::string, LatestVersionLookup> latest_cache;

    for (const InstalledMod& mod : mods) {
        if (!options.include_disabled && !mod.enabled) {
            continue;
        }
        ModrinthUpdateEntry entry;
        entry.local_file = mod.filename;
        entry.local_enabled = mod.enabled;
        entry.sha1 = mod.sha1;

        if (mod.sha1.empty()) {
            entry.status = ModrinthUpdateStatus::Unreadable;
            entry.error = "cannot hash the local file";
            ++result.failures;
            result.entries.push_back(std::move(entry));
            continue;
        }

        auto lookup_it = version_cache.find(mod.sha1);
        if (lookup_it == version_cache.end()) {
            lookup_it = version_cache
                            .emplace(mod.sha1,
                                     lookup_version_file(
                                         mod.sha1, fetcher, options.endpoints))
                            .first;
        }
        const VersionFileLookup& lookup = lookup_it->second;
        if (lookup.not_found) {
            entry.status = ModrinthUpdateStatus::NotOnModrinth;
            ++result.not_on_modrinth;
            result.entries.push_back(std::move(entry));
            continue;
        }
        if (!lookup.found) {
            entry.status = ModrinthUpdateStatus::LookupFailed;
            entry.error = lookup.error;
            ++result.failures;
            result.entries.push_back(std::move(entry));
            continue;
        }
        entry.current = lookup.version;

        auto latest_it = latest_cache.find(lookup.version.project_id);
        if (latest_it == latest_cache.end()) {
            LatestVersionLookup latest;
            try {
                const std::vector<ModrinthVersion> versions = parse_modrinth_versions(
                    fetcher(modrinth_project_versions_url(
                        lookup.version.project_id, loader, game_version,
                        options.endpoints)));
                latest.version = select_modrinth_version(
                    versions, "", options.allow_prerelease);
            } catch (const Error& error) {
                latest.failed = true;
                latest.error = error.what();
            }
            latest_it =
                latest_cache.emplace(lookup.version.project_id, std::move(latest)).first;
        }
        const LatestVersionLookup& latest = latest_it->second;
        if (latest.failed) {
            entry.status = ModrinthUpdateStatus::LookupFailed;
            entry.error = latest.error;
            ++result.failures;
            result.entries.push_back(std::move(entry));
            continue;
        }
        if (!latest.version || !is_newer_than(*latest.version, lookup.version)) {
            entry.status = ModrinthUpdateStatus::UpToDate;
            ++result.up_to_date;
            result.entries.push_back(std::move(entry));
            continue;
        }

        entry.latest = *latest.version;
        const ModrinthFile* file = select_modrinth_file(*latest.version);
        if (file != nullptr) {
            entry.latest_file = *file;
            if (is_sha1_hex(file->sha1) && iequals(file->sha1, mod.sha1)) {
                // The matching version publishes the same JAR that is already
                // installed; nothing to download.
                entry.status = ModrinthUpdateStatus::UpToDate;
                entry.latest.reset();
                entry.latest_file.reset();
                ++result.up_to_date;
                result.entries.push_back(std::move(entry));
                continue;
            }
        }
        entry.status = ModrinthUpdateStatus::UpdateAvailable;
        if (file == nullptr) {
            entry.error = "the newest version has no installable file";
        } else if (options.apply) {
            const ApplyOutcome outcome = apply_update_file(
                mods_dir, mod.path, mod.enabled, *file, downloader);
            if (outcome.applied) {
                entry.applied = true;
                entry.installed_file = outcome.installed;
                ++result.applied;
            }
            if (!outcome.error.empty()) {
                entry.error = outcome.error;
            }
        }
        ++result.updates;
        result.entries.push_back(std::move(entry));
    }

    return result;
}

} // namespace aurora
