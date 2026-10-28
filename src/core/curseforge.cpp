#include "core/curseforge.hpp"

#include "core/error.hpp"
#include "core/hash.hpp"
#include "core/http.hpp"
#include "core/http_post.hpp"
#include "core/json.hpp"
#include "core/mods.hpp"
#include "core/paths.hpp"
#include "core/strings.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <map>
#include <set>
#include <system_error>
#include <utility>

namespace aurora {

namespace {

constexpr int kCurseForgeGameId = 432;
constexpr int kCurseForgeModsClassId = 6;
constexpr int kRequiredDependency = 3;
constexpr int kMaxDependencyDepth = 16;

bool is_https_url(std::string_view url) {
    return url.size() >= 8 &&
           to_lower_ascii(std::string(url.substr(0, 8))) == "https://";
}

std::string url_encode(std::string_view value) {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(value.size());
    for (const unsigned char character : value) {
        if (std::isalnum(character) != 0 || character == '-' || character == '_' ||
            character == '.' || character == '~') {
            result.push_back(static_cast<char>(character));
        } else {
            result.push_back('%');
            result.push_back(hex[character >> 4]);
            result.push_back(hex[character & 0x0F]);
        }
    }
    return result;
}

std::string normalized_root(const CurseForgeEndpoints& endpoints) {
    std::string root = trim(endpoints.api_root);
    while (!root.empty() && root.back() == '/') {
        root.pop_back();
    }
    if (root.empty()) {
        fail("CurseForge API root is empty");
    }
    if (!is_https_url(root)) {
        fail("CurseForge API root must use HTTPS: " + root);
    }
    return root;
}

bool is_sha1_hex(std::string_view value) {
    if (value.size() != 40) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](unsigned char character) {
        return std::isxdigit(character) != 0;
    });
}

std::string require_https_download(std::string_view url, std::string_view context) {
    const std::string value = trim(url);
    if (!is_https_url(value)) {
        fail(std::string(context) + " must be an HTTPS URL: " + value);
    }
    return value;
}

CurseForgeFile parse_file_object(const Json& value, std::string_view source) {
    if (!value.is_object()) {
        fail("CurseForge file response from " + std::string(source) +
             " contains a non-object entry");
    }

    CurseForgeFile file;
    file.id = static_cast<std::uint64_t>(json_integer(value, "id"));
    if (file.id == 0) {
        fail("CurseForge file from " + std::string(source) + " has no id");
    }
    file.mod_id = static_cast<std::uint64_t>(json_integer(value, "modId"));
    file.display_name = json_string(value, "displayName");
    file.filename = json_string(value, "fileName");
    file.download_url = json_string(value, "downloadUrl");
    file.length = static_cast<std::uint64_t>(json_integer(value, "fileLength"));
    file.release_type = static_cast<int>(json_integer(value, "releaseType"));
    file.file_date = json_string(value, "fileDate");
    file.available = json_boolean(value, "isAvailable", true);

    if (const Json* hashes = json_array(value, "hashes")) {
        for (const Json& hash : *hashes) {
            if (json_integer(hash, "algo") != 1) {
                continue;
            }
            const std::string sha1 = to_lower_ascii(trim(json_string(hash, "value")));
            if (is_sha1_hex(sha1)) {
                file.sha1 = sha1;
                break;
            }
        }
    }
    if (const Json* dependencies = json_array(value, "dependencies")) {
        for (const Json& item : *dependencies) {
            CurseForgeDependency dependency;
            dependency.mod_id = static_cast<std::uint64_t>(json_integer(item, "modId"));
            dependency.relation_type =
                static_cast<int>(json_integer(item, "relationType"));
            if (dependency.mod_id != 0) {
                file.dependencies.push_back(dependency);
            }
        }
    }
    return file;
}

CurseForgeMod parse_mod_object(const Json& value, std::string_view source) {
    if (!value.is_object()) {
        fail("CurseForge mod response from " + std::string(source) +
             " contains a non-object entry");
    }

    CurseForgeMod mod;
    mod.id = static_cast<std::uint64_t>(json_integer(value, "id"));
    if (mod.id == 0) {
        fail("CurseForge mod from " + std::string(source) + " has no id");
    }
    mod.name = json_string(value, "name");
    mod.slug = json_string(value, "slug");
    mod.summary = json_string(value, "summary");
    mod.downloads = static_cast<std::uint64_t>(json_integer(value, "downloadCount"));
    mod.class_id = static_cast<int>(json_integer(value, "classId"));
    mod.allow_mod_distribution = json_boolean(value, "allowModDistribution", true);
    if (const Json* authors = json_array(value, "authors")) {
        for (const Json& author : *authors) {
            const std::string name = json_string(author, "name");
            if (!name.empty()) {
                mod.author = name;
                break;
            }
        }
    }
    return mod;
}

bool is_downloadable_file(const CurseForgeFile& file) {
    return file.available && is_safe_mod_filename(file.filename) &&
           is_https_url(file.download_url) && is_sha1_hex(file.sha1);
}

std::uint32_t murmur_hash2_32(const std::uint8_t* data,
                              std::size_t length,
                              std::uint32_t seed) {
    constexpr std::uint32_t m = 0x5bd1e995U;
    constexpr int r = 24;
    std::uint32_t hash = seed ^ static_cast<std::uint32_t>(length);
    std::size_t index = 0;
    while (index + 4 <= length) {
        std::uint32_t k = static_cast<std::uint32_t>(data[index]) |
                          (static_cast<std::uint32_t>(data[index + 1]) << 8) |
                          (static_cast<std::uint32_t>(data[index + 2]) << 16) |
                          (static_cast<std::uint32_t>(data[index + 3]) << 24);
        k *= m;
        k ^= k >> r;
        k *= m;
        hash *= m;
        hash ^= k;
        index += 4;
    }
    switch (length - index) {
    case 3:
        hash ^= static_cast<std::uint32_t>(data[index + 2]) << 16;
        [[fallthrough]];
    case 2:
        hash ^= static_cast<std::uint32_t>(data[index + 1]) << 8;
        [[fallthrough]];
    case 1:
        hash ^= static_cast<std::uint32_t>(data[index]);
        hash *= m;
        break;
    default:
        break;
    }
    hash ^= hash >> 13;
    hash *= m;
    hash ^= hash >> 15;
    return hash;
}

} // namespace

std::uint32_t curseforge_fingerprint(const std::filesystem::path& jar) {
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(jar, error);
    if (error) {
        fail("Cannot read " + path_to_utf8(jar) + ": " + error.message());
    }
    constexpr std::uintmax_t kMaxFingerprintBytes = 512ULL * 1024ULL * 1024ULL;
    if (size > kMaxFingerprintBytes) {
        fail("Mod JAR is too large to fingerprint: " + path_to_utf8(jar));
    }
    std::ifstream input(jar, std::ios::binary);
    if (!input) {
        fail("Cannot open mod JAR: " + path_to_utf8(jar));
    }
    std::vector<std::uint8_t> filtered;
    filtered.reserve(static_cast<std::size_t>(size));
    std::vector<char> buffer(64 * 1024);
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize read = input.gcount();
        for (std::streamsize index = 0; index < read; ++index) {
            const unsigned char byte = static_cast<unsigned char>(buffer[index]);
            if (byte != 0x09 && byte != 0x0A && byte != 0x0D && byte != 0x20) {
                filtered.push_back(byte);
            }
        }
    }
    if (input.bad()) {
        fail("Cannot read mod JAR: " + path_to_utf8(jar));
    }
    return murmur_hash2_32(filtered.data(), filtered.size(), 1);
}

std::string curseforge_fingerprints_url(const CurseForgeEndpoints& endpoints) {
    return normalized_root(endpoints) + "/v1/fingerprints/432";
}

std::string curseforge_fingerprints_body(
    const std::vector<std::uint32_t>& fingerprints) {
    Json body = Json::object();
    Json values = Json::array();
    for (const std::uint32_t fingerprint : fingerprints) {
        values.push_back(fingerprint);
    }
    body["fingerprints"] = std::move(values);
    return body.dump();
}

std::vector<CurseForgeFingerprintMatch> parse_curseforge_fingerprint_matches(
    std::string_view json,
    const std::vector<std::uint32_t>& requested,
    std::string_view source) {
    const Json root = parse_json(json, source);
    const Json* data = json_object(root, "data");
    if (data == nullptr) {
        fail("CurseForge fingerprint response from " + std::string(source) +
             " has no data object");
    }
    const Json* matches = json_array(*data, "exactMatches");
    if (matches == nullptr) {
        return {};
    }
    std::vector<CurseForgeFingerprintMatch> result;
    for (const Json& item : *matches) {
        if (!item.is_object()) {
            continue;
        }
        const Json* file = json_object(item, "file");
        if (file == nullptr) {
            continue;
        }
        CurseForgeFingerprintMatch match;
        const auto id = item.find("id");
        if (id != item.end() && id->is_number_integer()) {
            match.fingerprint = static_cast<std::uint32_t>(id->get<std::int64_t>());
        } else if (requested.size() == 1) {
            match.fingerprint = requested.front();
        }
        try {
            match.file = parse_file_object(*file, source);
        } catch (const Error&) {
            continue;
        }
        result.push_back(std::move(match));
    }
    return result;
}

std::string curseforge_http_post_json(std::string_view url,
                                      std::string_view body,
                                      std::string_view api_key) {
    const std::string key = trim(api_key);
    if (key.empty()) {
        fail("CurseForge API key is required");
    }
    const std::string address = require_https_download(url, "CurseForge API URL");
    const HttpResponse response = http_post_json(
        address, body,
        {{"Accept", "application/json"},
         {"Content-Type", "application/json"},
         {"x-api-key", key}});
    if (response.status < 200 || response.status >= 300) {
        throw HttpStatusError(response.status,
                              "CurseForge API returned HTTP " +
                                  std::to_string(response.status) + " for " +
                                  address);
    }
    return response.body;
}

std::vector<CurseForgeFingerprintMatch> lookup_curseforge_fingerprints(
    const std::vector<std::uint32_t>& fingerprints,
    const CurseForgeEndpoints& endpoints,
    std::string_view api_key,
    const CurseForgePostFetcher& post) {
    std::vector<CurseForgeFingerprintMatch> result;
    if (fingerprints.empty()) {
        return result;
    }
    const std::string url = curseforge_fingerprints_url(endpoints);
    constexpr std::size_t kBatchSize = 100;
    for (std::size_t start = 0; start < fingerprints.size(); start += kBatchSize) {
        const std::size_t end = std::min(fingerprints.size(), start + kBatchSize);
        std::vector<std::uint32_t> batch(fingerprints.begin() + start,
                                         fingerprints.begin() + end);
        const std::string body = curseforge_fingerprints_body(batch);
        const std::string response =
            post ? post(url, body) : curseforge_http_post_json(url, body, api_key);
        std::vector<CurseForgeFingerprintMatch> matches =
            parse_curseforge_fingerprint_matches(response, batch, url);
        result.insert(result.end(),
                      std::make_move_iterator(matches.begin()),
                      std::make_move_iterator(matches.end()));
    }
    return result;
}

int curseforge_loader_type(std::string_view loader) {
    const std::string value = to_lower_ascii(trim(loader));
    if (value.empty()) {
        return 0;
    }
    if (value == "forge") {
        return 1;
    }
    if (value == "fabric") {
        return 4;
    }
    if (value == "quilt") {
        return 5;
    }
    if (value == "neoforge") {
        return 6;
    }
    fail("Unsupported CurseForge loader '" + std::string(loader) +
         "' (expected fabric, quilt, forge or neoforge)");
}

bool is_safe_curseforge_game_version(std::string_view game_version) {
    if (game_version.empty() || game_version.size() > 64 ||
        std::isalnum(static_cast<unsigned char>(game_version.front())) == 0) {
        return false;
    }
    return std::all_of(
        game_version.begin(), game_version.end(), [](unsigned char character) {
            return std::isalnum(character) != 0 || character == '.' || character == '-' ||
                   character == '_' || character == '+';
        });
}

std::string curseforge_search_url(std::string_view query,
                                  std::string_view loader,
                                  std::string_view game_version,
                                  int limit,
                                  const CurseForgeEndpoints& endpoints) {
    const std::string root = normalized_root(endpoints);
    if (limit < 1 || limit > 50) {
        fail("CurseForge search limit must be between 1 and 50");
    }

    std::string url = root + "/v1/mods/search?gameId=" +
                      std::to_string(kCurseForgeGameId) + "&classId=" +
                      std::to_string(kCurseForgeModsClassId) +
                      "&sortField=2&sortOrder=desc&pageSize=" +
                      std::to_string(limit);
    const std::string search = trim(query);
    if (!search.empty()) {
        url += "&searchFilter=" + url_encode(search);
    }
    const int loader_type = curseforge_loader_type(loader);
    if (loader_type != 0) {
        url += "&modLoaderType=" + std::to_string(loader_type);
    }
    const std::string version = trim(game_version);
    if (!version.empty()) {
        if (!is_safe_curseforge_game_version(version)) {
            fail("Invalid CurseForge game version: " + version);
        }
        url += "&gameVersion=" + url_encode(version);
    }
    return url;
}

std::string curseforge_mod_url(std::uint64_t mod_id,
                               const CurseForgeEndpoints& endpoints) {
    if (mod_id == 0) {
        fail("CurseForge mod id must be positive");
    }
    return normalized_root(endpoints) + "/v1/mods/" + std::to_string(mod_id);
}

std::string curseforge_files_url(std::uint64_t mod_id,
                                 std::string_view loader,
                                 std::string_view game_version,
                                 const CurseForgeEndpoints& endpoints) {
    std::string url = curseforge_mod_url(mod_id, endpoints) + "/files?pageSize=50";
    const int loader_type = curseforge_loader_type(loader);
    if (loader_type != 0) {
        url += "&modLoaderType=" + std::to_string(loader_type);
    }
    const std::string version = trim(game_version);
    if (!version.empty()) {
        if (!is_safe_curseforge_game_version(version)) {
            fail("Invalid CurseForge game version: " + version);
        }
        url += "&gameVersion=" + url_encode(version);
    }
    return url;
}

std::string curseforge_file_url(std::uint64_t mod_id,
                                std::uint64_t file_id,
                                const CurseForgeEndpoints& endpoints) {
    if (file_id == 0) {
        fail("CurseForge file id must be positive");
    }
    return curseforge_mod_url(mod_id, endpoints) + "/files/" +
           std::to_string(file_id);
}

CurseForgeSearchResult parse_curseforge_search(std::string_view json,
                                               std::string_view source) {
    const Json root = parse_json(json, source);
    if (!root.is_object()) {
        fail("CurseForge search response from " + std::string(source) +
             " must be an object");
    }
    const Json* data = json_array(root, "data");
    if (data == nullptr) {
        fail("CurseForge search response from " + std::string(source) +
             " has no data array");
    }

    CurseForgeSearchResult result;
    if (const Json* pagination = json_object(root, "pagination")) {
        result.total_count =
            static_cast<std::uint64_t>(json_integer(*pagination, "totalCount"));
    }
    for (const Json& item : *data) {
        if (!item.is_object() || json_integer(item, "id") <= 0) {
            continue;
        }
        result.mods.push_back(parse_mod_object(item, source));
    }
    return result;
}

CurseForgeMod parse_curseforge_mod(std::string_view json, std::string_view source) {
    const Json root = parse_json(json, source);
    const Json* data = json_object(root, "data");
    if (data == nullptr) {
        fail("CurseForge mod response from " + std::string(source) +
             " has no data object");
    }
    return parse_mod_object(*data, source);
}

std::vector<CurseForgeFile> parse_curseforge_files(std::string_view json,
                                                   std::string_view source) {
    const Json root = parse_json(json, source);
    const Json* data = json_array(root, "data");
    if (data == nullptr) {
        fail("CurseForge files response from " + std::string(source) +
             " has no data array");
    }

    std::vector<CurseForgeFile> files;
    files.reserve(data->size());
    for (const Json& item : *data) {
        if (!item.is_object()) {
            continue;
        }
        files.push_back(parse_file_object(item, source));
    }
    return files;
}

CurseForgeFile parse_curseforge_file(std::string_view json, std::string_view source) {
    const Json root = parse_json(json, source);
    const Json* data = json_object(root, "data");
    if (data == nullptr) {
        fail("CurseForge file response from " + std::string(source) +
             " has no data object");
    }
    return parse_file_object(*data, source);
}

std::optional<CurseForgeFile> select_curseforge_file(
    const std::vector<CurseForgeFile>& files,
    std::uint64_t explicit_file_id,
    bool allow_prerelease) {
    if (explicit_file_id != 0) {
        for (const CurseForgeFile& file : files) {
            if (file.id == explicit_file_id) {
                return file;
            }
        }
        return std::nullopt;
    }

    const CurseForgeFile* best = nullptr;
    for (const CurseForgeFile& file : files) {
        if (!is_downloadable_file(file)) {
            continue;
        }
        if (!allow_prerelease && file.release_type != 1) {
            continue;
        }
        if (best == nullptr || file.file_date > best->file_date ||
            (file.file_date == best->file_date && file.id > best->id)) {
            best = &file;
        }
    }
    if (best == nullptr) {
        return std::nullopt;
    }
    return *best;
}

std::string curseforge_http_get(std::string_view url, std::string_view api_key) {
    const std::string key = trim(api_key);
    if (key.empty()) {
        fail("CurseForge API key is required");
    }
    const std::string address = require_https_download(url, "CurseForge API URL");
    const HttpResponse response = http_get_with_headers(
        address, {{"Accept", "application/json"}, {"x-api-key", key}});
    if (response.status < 200 || response.status >= 300) {
        throw HttpStatusError(response.status,
                              "CurseForge API returned HTTP " +
                                  std::to_string(response.status) + " for " +
                                  address);
    }
    return response.body;
}

CurseForgeSearchResult search_curseforge(const CurseForgeSearchOptions& options) {
    const std::string url = curseforge_search_url(
        options.query, options.loader, options.game_version, options.limit,
        options.endpoints);
    CurseForgeFetcher fetcher = options.fetch;
    if (!fetcher) {
        const std::string key = trim(options.api_key);
        fetcher = [key](std::string_view address) {
            return curseforge_http_get(address, key);
        };
    }
    return parse_curseforge_search(fetcher(url), url);
}

namespace {

struct InstallContext {
    const CurseForgeInstallOptions& options;
    std::filesystem::path mods_dir;
    int loader_type = 0;
    std::string game_version;
    CurseForgeFetcher fetch;
    CurseForgeDownloader download;
    std::set<std::uint64_t> installed_mods;
    CurseForgeInstallResult result;
};

CurseForgeMod fetch_mod(InstallContext& context, std::uint64_t mod_id) {
    const std::string url =
        curseforge_mod_url(mod_id, context.options.endpoints);
    return parse_curseforge_mod(context.fetch(url), url);
}

std::vector<CurseForgeFile> fetch_files(InstallContext& context,
                                        std::uint64_t mod_id) {
    const std::string url = curseforge_files_url(
        mod_id, context.options.loader, context.game_version,
        context.options.endpoints);
    return parse_curseforge_files(context.fetch(url), url);
}

CurseForgeFile fetch_file(InstallContext& context,
                          std::uint64_t mod_id,
                          std::uint64_t file_id) {
    const std::string url =
        curseforge_file_url(mod_id, file_id, context.options.endpoints);
    return parse_curseforge_file(context.fetch(url), url);
}

CurseForgeInstalledMod install_file(InstallContext& context,
                                    const CurseForgeMod& mod,
                                    const CurseForgeFile& file,
                                    bool dependency) {
    if (!file.available) {
        fail("CurseForge file " + std::to_string(file.id) + " for '" + mod.name +
             "' is not available");
    }
    if (file.mod_id != 0 && file.mod_id != mod.id) {
        fail("CurseForge file " + std::to_string(file.id) + " belongs to mod " +
             std::to_string(file.mod_id) + ", not " + std::to_string(mod.id));
    }
    if (trim(file.download_url).empty()) {
        fail("CurseForge file " + std::to_string(file.id) + " for '" + mod.name +
             "' has no download URL; the author disabled third-party distribution");
    }
    require_https_download(file.download_url, "CurseForge download URL");
    if (!is_safe_mod_filename(file.filename)) {
        fail("Unsafe CurseForge file name: " + file.filename);
    }
    if (!is_sha1_hex(file.sha1)) {
        fail("CurseForge file " + std::to_string(file.id) + " for '" + mod.name +
             "' has no SHA-1 hash");
    }

    const std::filesystem::path destination =
        context.mods_dir / path_from_utf8(file.filename);
    if (destination.parent_path() != context.mods_dir) {
        fail("CurseForge file escapes the mods directory: " +
             path_to_utf8(destination));
    }

    CurseForgeInstalledMod installed;
    installed.mod = mod;
    installed.file = file;
    installed.file_path = destination;
    installed.dependency = dependency;

    std::error_code error;
    if (std::filesystem::is_regular_file(destination, error)) {
        try {
            if (checksum_matches(file.sha1, sha1_file(destination))) {
                return installed;
            }
        } catch (const Error&) {
        }
        if (!context.options.force) {
            fail("File already exists with different contents: " +
                 path_to_utf8(destination) + " (use --force to replace)");
        }
    }

    context.download(file.download_url, destination, file.sha1);
    try {
        if (!checksum_matches(file.sha1, sha1_file(destination))) {
            fail("Downloaded CurseForge file did not match its SHA-1: " +
                 path_to_utf8(destination));
        }
    } catch (const Error& hash_error) {
        fail("Unable to verify " + path_to_utf8(destination) + ": " +
             std::string(hash_error.what()));
    }
    installed.downloaded = true;
    return installed;
}

void install_dependencies(InstallContext& context,
                          const CurseForgeFile& parent,
                          int depth) {
    if (!context.options.install_dependencies || depth >= kMaxDependencyDepth) {
        return;
    }
    for (const CurseForgeDependency& dependency : parent.dependencies) {
        if (dependency.relation_type != kRequiredDependency ||
            dependency.mod_id == 0) {
            continue;
        }
        if (!context.installed_mods.insert(dependency.mod_id).second) {
            continue;
        }
        const CurseForgeMod mod = fetch_mod(context, dependency.mod_id);
        const std::vector<CurseForgeFile> files =
            fetch_files(context, dependency.mod_id);
        const auto selected =
            select_curseforge_file(files, 0, context.options.allow_prerelease);
        if (!selected) {
            fail("No downloadable CurseForge file for required dependency '" +
                 mod.name + "' (" + std::to_string(dependency.mod_id) + ")");
        }
        context.result.dependencies.push_back(
            install_file(context, mod, *selected, true));
        install_dependencies(context, *selected, depth + 1);
    }
}

} // namespace

CurseForgeInstallResult install_curseforge_mod(
    const CurseForgeInstallOptions& options) {
    if (options.game_dir.empty()) {
        fail("Game directory is empty");
    }
    if (options.mod_id == 0) {
        fail("CurseForge mod id must be positive");
    }
    if (options.loader.empty()) {
        fail("CurseForge install requires a loader");
    }
    const std::string game_version = trim(options.game_version);
    if (!is_safe_curseforge_game_version(game_version)) {
        fail("Invalid CurseForge game version: " + options.game_version);
    }

    CurseForgeFetcher fetcher = options.fetch;
    if (!fetcher) {
        const std::string key = trim(options.api_key);
        if (key.empty()) {
            fail("CurseForge API key is required (--api-key, CURSEFORGE_API_KEY "
                 "or curseForgeApiKey)");
        }
        fetcher = [key](std::string_view address) {
            return curseforge_http_get(address, key);
        };
    }
    CurseForgeDownloader downloader = options.download;
    if (!downloader) {
        HttpDownloadOptions http_options;
        http_options.max_retries = std::max(0, options.download_retries);
        http_options.mirrors = options.download_mirrors;
        downloader = [http_options](std::string_view url,
                                    const std::filesystem::path& destination,
                                    std::string_view sha1) {
            http_download(url, destination, http_options, sha1);
        };
    }

    const std::filesystem::path mods_dir = mods_directory(options.game_dir);
    ensure_directory(options.game_dir);
    ensure_directory(mods_dir);

    InstallContext context{
        options,
        mods_dir,
        curseforge_loader_type(options.loader),
        game_version,
        std::move(fetcher),
        std::move(downloader),
        {},
        {},
    };

    const CurseForgeMod mod = fetch_mod(context, options.mod_id);
    std::optional<CurseForgeFile> file;
    if (options.file_id != 0) {
        file = fetch_file(context, options.mod_id, options.file_id);
    } else {
        file = select_curseforge_file(
            fetch_files(context, options.mod_id), 0, options.allow_prerelease);
    }
    if (!file) {
        fail("No downloadable CurseForge file for '" + mod.name + "' (" +
             std::to_string(options.mod_id) + ")");
    }

    context.installed_mods.insert(mod.id);
    context.result.root = install_file(context, mod, *file, false);
    install_dependencies(context, *file, 0);
    return std::move(context.result);
}

namespace {

std::string file_sha1_or_empty(const std::filesystem::path& path) {
    try {
        return sha1_file(path);
    } catch (const Error&) {
        return {};
    }
}

} // namespace

CurseForgeUpdateResult check_curseforge_updates(
    const CurseForgeUpdateOptions& options) {
    if (options.game_dir.empty()) {
        fail("Game directory is empty");
    }
    if (options.loader.empty()) {
        fail("CurseForge update requires a loader");
    }
    const std::string game_version = trim(options.game_version);
    if (!is_safe_curseforge_game_version(game_version)) {
        fail("Invalid CurseForge game version: " + options.game_version);
    }
    curseforge_loader_type(options.loader);

    const std::string key = trim(options.api_key);
    CurseForgeFetcher fetcher = options.fetch;
    if (!fetcher) {
        if (key.empty()) {
            fail("CurseForge API key is required (--api-key, CURSEFORGE_API_KEY "
                 "or curseForgeApiKey)");
        }
        fetcher = [key](std::string_view address) {
            return curseforge_http_get(address, key);
        };
    }
    CurseForgePostFetcher post = options.post;
    if (!post) {
        post = [key](std::string_view address, std::string_view body) {
            return curseforge_http_post_json(address, body, key);
        };
    }
    CurseForgeDownloader downloader = options.download;
    if (!downloader) {
        HttpDownloadOptions http_options;
        http_options.max_retries = std::max(0, options.download_retries);
        http_options.mirrors = options.download_mirrors;
        downloader = [http_options](std::string_view url,
                                    const std::filesystem::path& destination,
                                    std::string_view sha1) {
            http_download(url, destination, http_options, sha1);
        };
    }

    CurseForgeUpdateResult result;
    const std::filesystem::path mods_dir = mods_directory(options.game_dir);
    const std::vector<InstalledMod> mods = scan_mods(options.game_dir, true);
    std::vector<std::uint32_t> fingerprints;
    for (const InstalledMod& mod : mods) {
        if (!options.include_disabled && !mod.enabled) {
            continue;
        }
        CurseForgeUpdateEntry entry;
        entry.local_file = mod.filename;
        entry.enabled = mod.enabled;
        entry.installed_file = mod.path;
        try {
            entry.fingerprint = curseforge_fingerprint(mod.path);
        } catch (const Error& error) {
            entry.status = CurseForgeUpdateStatus::Unreadable;
            entry.error = error.what();
            ++result.failures;
            result.entries.push_back(std::move(entry));
            continue;
        }
        if (entry.fingerprint != 0 && entry.fingerprint != 811513880U) {
            fingerprints.push_back(entry.fingerprint);
        }
        result.entries.push_back(std::move(entry));
    }

    std::vector<CurseForgeFingerprintMatch> matches;
    try {
        matches = lookup_curseforge_fingerprints(fingerprints,
                                                 options.endpoints, key, post);
    } catch (const Error& error) {
        for (CurseForgeUpdateEntry& entry : result.entries) {
            if (entry.status == CurseForgeUpdateStatus::Unreadable) {
                continue;
            }
            entry.status = CurseForgeUpdateStatus::LookupFailed;
            entry.error = error.what();
            ++result.failures;
        }
        return result;
    }

    std::map<std::uint32_t, CurseForgeFile> by_fingerprint;
    std::map<std::string, CurseForgeFile> by_sha1;
    for (const CurseForgeFingerprintMatch& match : matches) {
        if (match.fingerprint != 0) {
            by_fingerprint.emplace(match.fingerprint, match.file);
        }
        if (is_sha1_hex(match.file.sha1)) {
            by_sha1.emplace(match.file.sha1, match.file);
        }
    }

    std::map<std::uint64_t, std::vector<CurseForgeFile>> project_files;
    const auto files_for = [&](std::uint64_t mod_id) -> const std::vector<CurseForgeFile>& {
        auto it = project_files.find(mod_id);
        if (it != project_files.end()) {
            return it->second;
        }
        const std::string url = curseforge_files_url(
            mod_id, options.loader, game_version, options.endpoints);
        std::vector<CurseForgeFile> files = parse_curseforge_files(fetcher(url), url);
        return project_files.emplace(mod_id, std::move(files)).first->second;
    };

    for (CurseForgeUpdateEntry& entry : result.entries) {
        if (entry.status == CurseForgeUpdateStatus::Unreadable) {
            continue;
        }
        const auto local = std::find_if(
            mods.begin(), mods.end(), [&entry](const InstalledMod& mod) {
                return mod.filename == entry.local_file;
            });
        const std::string local_sha1 =
            local == mods.end() ? std::string() : to_lower_ascii(local->sha1);

        std::optional<CurseForgeFile> current;
        if (const auto it = by_fingerprint.find(entry.fingerprint);
            it != by_fingerprint.end()) {
            current = it->second;
        } else if (!local_sha1.empty()) {
            if (const auto by_hash = by_sha1.find(local_sha1);
                by_hash != by_sha1.end()) {
                current = by_hash->second;
            }
        }
        if (!current) {
            entry.status = CurseForgeUpdateStatus::NotOnCurseForge;
            ++result.not_on_curseforge;
            continue;
        }
        entry.current = current;

        std::optional<CurseForgeFile> latest;
        try {
            latest = select_curseforge_file(
                files_for(current->mod_id), 0, options.allow_prerelease);
        } catch (const Error& error) {
            entry.status = CurseForgeUpdateStatus::LookupFailed;
            entry.error = error.what();
            ++result.failures;
            continue;
        }
        if (!latest) {
            entry.status = CurseForgeUpdateStatus::LookupFailed;
            entry.error = "no downloadable file for this loader/game version";
            ++result.failures;
            continue;
        }
        entry.latest = latest;
        if (latest->id == current->id) {
            entry.status = CurseForgeUpdateStatus::UpToDate;
            ++result.up_to_date;
            continue;
        }
        entry.status = CurseForgeUpdateStatus::UpdateAvailable;
        ++result.updates;

        if (!options.apply) {
            continue;
        }
        if (!is_downloadable_file(*latest)) {
            entry.error = "the newest file is not downloadable";
            ++result.failures;
            continue;
        }
        try {
            const std::filesystem::path old_path = entry.installed_file;
            const std::filesystem::path target =
                mods_dir / path_from_utf8(latest->filename);
            const std::filesystem::path final_target =
                entry.enabled
                    ? target
                    : mods_dir / path_from_utf8(latest->filename + ".disabled");
            std::error_code error;
            if (final_target != old_path &&
                std::filesystem::is_regular_file(final_target, error)) {
                if (!checksum_matches(latest->sha1,
                                      file_sha1_or_empty(final_target))) {
                    fail("Target already exists with different contents: " +
                         path_to_utf8(final_target));
                }
            }
            if (target != old_path &&
                std::filesystem::is_regular_file(target, error) &&
                target != final_target &&
                !checksum_matches(latest->sha1, file_sha1_or_empty(target))) {
                fail("Target already exists with different contents: " +
                     path_to_utf8(target));
            }
            if (!std::filesystem::is_regular_file(target, error) ||
                !checksum_matches(latest->sha1, file_sha1_or_empty(target))) {
                downloader(latest->download_url, target, latest->sha1);
            }
            if (!checksum_matches(latest->sha1, file_sha1_or_empty(target))) {
                fail("Downloaded file did not match its SHA-1: " +
                     path_to_utf8(target));
            }
            if (final_target != target) {
                if (std::filesystem::is_regular_file(final_target, error) &&
                    final_target != old_path) {
                    std::filesystem::remove(final_target, error);
                    if (error) {
                        fail("Cannot replace " + path_to_utf8(final_target) +
                             ": " + error.message());
                    }
                }
                if (std::filesystem::exists(old_path, error) &&
                    old_path != final_target) {
                    std::filesystem::remove(old_path, error);
                }
                std::filesystem::rename(target, final_target, error);
                if (error) {
                    fail("Cannot rename the updated mod: " + error.message());
                }
            } else if (old_path != final_target &&
                       std::filesystem::is_regular_file(old_path, error)) {
                std::filesystem::remove(old_path, error);
            }
            entry.installed_file = final_target;
            entry.applied = true;
            ++result.applied;
        } catch (const Error& error) {
            entry.error = error.what();
            ++result.failures;
        }
    }
    return result;
}

} // namespace aurora
