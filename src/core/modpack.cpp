#include "core/modpack.hpp"

#include "core/curseforge.hpp"
#include "core/error.hpp"
#include "core/forge.hpp"
#include "core/hash.hpp"
#include "core/http.hpp"
#include "core/install.hpp"
#include "core/json.hpp"
#include "core/loader.hpp"
#include "core/log.hpp"
#include "core/manifest.hpp"
#include "core/paths.hpp"
#include "core/rules.hpp"
#include "core/strings.hpp"
#include "core/zip.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <map>
#include <optional>
#include <set>
#include <system_error>
#include <utility>

namespace aurora {

namespace {

constexpr std::string_view kModpackMarkerName = "modpack.json";

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

bool is_safe_pack_version(std::string_view value) {
    if (value.empty() || value.size() > 96 ||
        std::isalnum(static_cast<unsigned char>(value.front())) == 0) {
        return false;
    }
    return std::all_of(
        value.begin(), value.end(), [](unsigned char character) {
            return std::isalnum(character) != 0 || character == '.' || character == '-' ||
                   character == '_' || character == '+';
        });
}

bool is_reserved_component(std::string_view component) {
    const std::string lower = to_lower_ascii(std::string(component));
    const std::string stem = lower.substr(0, lower.find('.'));
    if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul") {
        return true;
    }
    if (stem.size() == 4 &&
        (stem.compare(0, 3, "com") == 0 || stem.compare(0, 3, "lpt") == 0) &&
        stem[3] >= '1' && stem[3] <= '9') {
        return true;
    }
    return false;
}

bool is_safe_component(std::string_view component) {
    if (component.empty() || component == "." || component == ".." ||
        component.back() == '.' || component.back() == ' ') {
        return false;
    }
    for (const unsigned char character : component) {
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
    return !is_reserved_component(component);
}

std::filesystem::path checked_modpack_path(std::string_view value,
                                           std::string_view context) {
    if (value.empty() || value.front() == '/') {
        fail("Unsafe " + std::string(context) + " path: " + std::string(value));
    }
    std::filesystem::path result;
    for (const std::string& part : split(value, '/')) {
        if (!is_safe_component(part)) {
            fail("Unsafe " + std::string(context) + " path: " + std::string(value));
        }
        result /= path_from_utf8(part);
    }
    return result;
}

std::string checked_prefix(std::string_view value, std::string_view context) {
    std::string prefix = trim(value);
    while (!prefix.empty() && prefix.back() == '/') {
        prefix.pop_back();
    }
    if (prefix.empty()) {
        return {};
    }
    const std::filesystem::path relative = checked_modpack_path(prefix, context);
    return path_to_utf8(relative) + "/";
}

struct LoaderDependency {
    std::string_view key;
    std::string_view loader;
};

constexpr LoaderDependency kModrinthLoaders[] = {
    {"fabric-loader", "fabric"},
    {"quilt-loader", "quilt"},
    {"forge", "forge"},
    {"neoforge", "neoforge"},
};

std::string curseforge_class_directory(int class_id) {
    switch (class_id) {
    case 12:
        return "resourcepacks";
    case 17:
        return "saves";
    case 6552:
        return "shaderpacks";
    default:
        return "mods";
    }
}

std::string safe_file_sha1(const std::filesystem::path& path) {
    try {
        return sha1_file(path);
    } catch (const Error&) {
        return {};
    }
}

std::string portable_relative(const std::filesystem::path& path) {
    std::string text = path_to_utf8(path);
    std::replace(text.begin(), text.end(), '\\', '/');
    return text;
}

bool is_regular(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error) && !error;
}

std::string sanitize_version_id(std::string_view name) {
    std::string result;
    for (const unsigned char character : trim(name)) {
        if (character < 0x20 || character == 0x7F) {
            continue;
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
            result.push_back('-');
            break;
        default:
            result.push_back(static_cast<char>(character));
            break;
        }
    }
    while (!result.empty() && (result.back() == '.' || result.back() == ' ')) {
        result.pop_back();
    }
    while (!result.empty() && (result.front() == '.' || result.front() == ' ')) {
        result.erase(result.begin());
    }
    if (result.empty() || is_reserved_component(result)) {
        result = "Modpack";
    }
    constexpr std::size_t kMaxIdBytes = 120;
    if (result.size() > kMaxIdBytes) {
        result.resize(kMaxIdBytes);
        while (!result.empty() && (result.back() == '.' || result.back() == ' ')) {
            result.pop_back();
        }
    }
    return result;
}

struct ManagedFile {
    std::string path;
    std::string sha1;
    std::uint64_t project_id = 0;
    std::uint64_t file_id = 0;
};

struct ManagedOverride {
    std::string path;
    std::string sha1;
};

struct ExistingInstance {
    bool present = false;
    std::string format;
    std::string name;
    std::string version;
    std::vector<ManagedFile> files;
    std::vector<ManagedOverride> overrides;
};

ExistingInstance load_instance_marker(const std::filesystem::path& instance_dir) {
    ExistingInstance marker;
    const std::filesystem::path path = instance_dir / kModpackMarkerName;
    std::error_code error;
    if (!std::filesystem::exists(path, error)) {
        return marker;
    }
    const Json root = read_json_file(path);
    if (!root.is_object()) {
        fail("Modpack marker is not a JSON object: " + path_to_utf8(path));
    }
    marker.present = true;
    marker.format = json_string(root, "format");
    marker.name = json_string(root, "name");
    marker.version = json_string(root, "version");
    if (const Json* files = json_array(root, "files")) {
        for (const Json& item : *files) {
            ManagedFile file;
            file.path = json_string(item, "path");
            file.sha1 = to_lower_ascii(trim(json_string(item, "sha1")));
            file.project_id = static_cast<std::uint64_t>(json_integer(item, "projectId"));
            file.file_id = static_cast<std::uint64_t>(json_integer(item, "fileId"));
            if (!file.path.empty()) {
                marker.files.push_back(std::move(file));
            }
        }
    }
    if (const Json* overrides = json_array(root, "overrides")) {
        for (const Json& item : *overrides) {
            ManagedOverride entry;
            entry.path = json_string(item, "path");
            entry.sha1 = to_lower_ascii(trim(json_string(item, "sha1")));
            if (!entry.path.empty()) {
                marker.overrides.push_back(std::move(entry));
            }
        }
    }
    return marker;
}

void copy_tree(const std::filesystem::path& source,
               const std::filesystem::path& destination) {
    std::error_code error;
    std::filesystem::copy(source, destination,
                          std::filesystem::copy_options::recursive |
                              std::filesystem::copy_options::overwrite_existing,
                          error);
    if (error) {
        fail("Unable to copy " + path_to_utf8(source) + " to " +
             path_to_utf8(destination) + ": " + error.message());
    }
}

std::filesystem::path make_backup_path(const std::filesystem::path& game_dir,
                                       const std::string& version_id) {
    const std::filesystem::path root = game_dir / "backup";
    ensure_directory(root);
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    for (int attempt = 0; attempt < 100; ++attempt) {
        const std::filesystem::path candidate =
            root / path_from_utf8(version_id + "-" + std::to_string(stamp) +
                                  (attempt == 0 ? "" : "-" + std::to_string(attempt)));
        std::error_code error;
        if (!std::filesystem::exists(candidate, error)) {
            return candidate;
        }
    }
    fail("Unable to allocate a modpack backup directory in " + path_to_utf8(root));
}

void remove_tree(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::remove_all(path, error);
}

void write_instance_json(const std::filesystem::path& instance_dir,
                         const std::string& version_id,
                         const std::string& parent) {
    Json profile = Json::object();
    profile["id"] = version_id;
    profile["inheritsFrom"] = parent;
    profile["type"] = "release";
    profile["time"] = now_iso8601();
    ensure_directory(instance_dir);
    write_text_file(instance_dir / path_from_utf8(version_id + ".json"),
                    json_pretty(profile) + "\n");
}

std::string install_pack_loader(const ModpackInfo& info,
                                const ModpackInstallOptions& options,
                                const std::filesystem::path& game_dir) {
    if (info.loader.empty()) {
        return {};
    }
    if (options.loader_installer) {
        return options.loader_installer(info, game_dir);
    }
    if (info.loader == "fabric" || info.loader == "quilt") {
        LoaderInstallOptions loader;
        loader.kind = info.loader == "fabric" ? LoaderKind::Fabric : LoaderKind::Quilt;
        loader.game_version = info.minecraft_version;
        loader.loader_version = info.loader_version;
        loader.force = options.force;
        return install_loader(game_dir, loader).id;
    }
    if (info.loader == "forge" || info.loader == "neoforge") {
        ForgeInstallRequest request;
        request.kind = info.loader == "forge" ? ForgeKind::Forge : ForgeKind::NeoForge;
        request.game_dir = game_dir;
        request.minecraft_version = info.minecraft_version;
        request.loader_version = info.loader_version;
        request.java_home = options.java_home;
        return install_forge(request).version_id;
    }
    fail("Unsupported modpack loader: " + info.loader);
}

void install_parent_files(const std::filesystem::path& game_dir,
                          const std::string& parent,
                          const ModpackInstallOptions& options) {
    const ResolvedVersion resolved =
        resolve_or_fetch_version(game_dir, parent, true);
    InstallOptions install;
    install.download_retries = options.download_retries;
    install.download_mirrors = options.download_mirrors;
    install_version(resolved, detect_environment(), install);
}

struct ResolvedPackFile {
    std::filesystem::path relative;
    std::string url;
    std::string sha1;
    std::uint64_t project_id = 0;
    std::uint64_t file_id = 0;
};

struct StagedOverride {
    std::filesystem::path relative;
    std::string key;
    std::string sha1;
};

} // namespace

ModpackInfo parse_modrinth_index(std::string_view json, std::string_view source) {
    const Json root = parse_json(json, source);
    if (!root.is_object()) {
        fail("Modrinth pack index from " + std::string(source) + " must be an object");
    }

    ModpackInfo info;
    info.format = "modrinth";
    if (json_integer(root, "formatVersion") != 1) {
        fail("Unsupported Modrinth pack formatVersion (expected 1)");
    }
    const std::string game = json_string(root, "game");
    if (!game.empty() && game != "minecraft") {
        fail("Modrinth pack targets unsupported game: " + game);
    }
    info.name = json_string(root, "name");
    if (info.name.empty()) {
        info.name = "Modrinth pack";
    }
    info.version = json_string(root, "versionId");
    info.summary = json_string(root, "summary");

    const Json* dependencies = json_object(root, "dependencies");
    if (dependencies == nullptr) {
        fail("Modrinth pack has no dependencies object");
    }
    info.minecraft_version = trim(json_string(*dependencies, "minecraft"));
    if (!is_safe_pack_version(info.minecraft_version)) {
        fail("Modrinth pack has no valid Minecraft version");
    }
    for (const LoaderDependency& mapping : kModrinthLoaders) {
        const std::string version =
            trim(json_string(*dependencies, mapping.key.data()));
        if (version.empty()) {
            continue;
        }
        if (!info.loader.empty()) {
            fail("Modrinth pack lists more than one mod loader");
        }
        if (!is_safe_pack_version(version)) {
            fail("Modrinth pack has an unsafe loader version: " + version);
        }
        info.loader = mapping.loader;
        info.loader_version = version;
    }

    const Json* files = json_array(root, "files");
    if (files == nullptr) {
        fail("Modrinth pack has no files array");
    }
    for (const Json& item : *files) {
        if (!item.is_object()) {
            fail("Modrinth pack contains a non-object file entry");
        }
        if (const Json* environment = json_object(item, "env")) {
            if (json_string(*environment, "client") == "unsupported") {
                continue;
            }
        }
        ModpackFile file;
        file.path = json_string(item, "path");
        if (file.path.empty()) {
            fail("Modrinth pack file has no path");
        }
        file.size = static_cast<std::uint64_t>(json_integer(item, "fileSize"));

        if (const Json* hashes = json_object(item, "hashes")) {
            file.sha1 = to_lower_ascii(trim(json_string(*hashes, "sha1")));
        }
        if (!is_sha1_hex(file.sha1)) {
            fail("Modrinth pack file has no SHA-1: " + file.path);
        }
        if (const Json* downloads = json_array(item, "downloads")) {
            for (const Json& download : *downloads) {
                if (!download.is_string()) {
                    continue;
                }
                const std::string url = trim(download.get<std::string>());
                if (is_https_url(url)) {
                    file.url = url;
                    break;
                }
            }
        }
        if (file.url.empty()) {
            fail("Modrinth pack file has no HTTPS download URL: " + file.path);
        }
        info.files.push_back(std::move(file));
    }
    info.override_prefixes = {"overrides/", "client-overrides/"};
    return info;
}

ModpackInfo parse_curseforge_manifest(std::string_view json,
                                      std::string_view source) {
    const Json root = parse_json(json, source);
    if (!root.is_object()) {
        fail("CurseForge manifest from " + std::string(source) + " must be an object");
    }

    ModpackInfo info;
    info.format = "curseforge";
    if (json_string(root, "manifestType") != "minecraftModpack") {
        fail("CurseForge archive is not a minecraftModpack manifest");
    }
    if (json_integer(root, "manifestVersion") != 1) {
        fail("Unsupported CurseForge manifestVersion (expected 1)");
    }
    info.name = json_string(root, "name");
    if (info.name.empty()) {
        info.name = "CurseForge pack";
    }
    info.version = json_string(root, "version");
    info.author = json_string(root, "author");

    const Json* minecraft = json_object(root, "minecraft");
    if (minecraft == nullptr) {
        fail("CurseForge manifest has no minecraft object");
    }
    info.minecraft_version = trim(json_string(*minecraft, "version"));
    if (!is_safe_pack_version(info.minecraft_version)) {
        fail("CurseForge manifest has no valid Minecraft version");
    }

    std::string loader_id;
    if (const Json* loaders = json_array(*minecraft, "modLoaders")) {
        for (const Json& item : *loaders) {
            const std::string id = trim(json_string(item, "id"));
            if (id.empty()) {
                continue;
            }
            if (loader_id.empty() || json_boolean(item, "primary")) {
                loader_id = id;
                if (json_boolean(item, "primary")) {
                    break;
                }
            }
        }
    }
    if (!loader_id.empty()) {
        const std::size_t separator = loader_id.find('-');
        if (separator == std::string::npos || separator == 0) {
            fail("Unsupported CurseForge mod loader id: " + loader_id);
        }
        const std::string loader = to_lower_ascii(loader_id.substr(0, separator));
        const std::string version = loader_id.substr(separator + 1);
        if (loader != "forge" && loader != "fabric" && loader != "quilt" &&
            loader != "neoforge") {
            fail("Unsupported CurseForge mod loader: " + loader);
        }
        if (!is_safe_pack_version(version)) {
            fail("CurseForge manifest has an unsafe loader version: " + version);
        }
        info.loader = loader;
        info.loader_version = version;
    }

    const Json* files = json_array(root, "files");
    if (files == nullptr) {
        fail("CurseForge manifest has no files array");
    }
    for (const Json& item : *files) {
        if (!item.is_object() || !json_boolean(item, "required", true)) {
            continue;
        }
        ModpackFile file;
        file.project_id = static_cast<std::uint64_t>(json_integer(item, "projectID"));
        file.file_id = static_cast<std::uint64_t>(json_integer(item, "fileID"));
        if (file.project_id == 0 || file.file_id == 0) {
            fail("CurseForge manifest contains a file without project/file ids");
        }
        info.files.push_back(std::move(file));
    }

    const std::string overrides = trim(json_string(root, "overrides", "overrides"));
    if (!overrides.empty()) {
        info.override_prefixes.push_back(
            checked_prefix(overrides, "CurseForge overrides"));
    }
    return info;
}

ModpackInfo read_modpack_info(const std::filesystem::path& archive) {
    if (const auto index = read_zip_entry(archive, "modrinth.index.json")) {
        return parse_modrinth_index(
            *index, path_to_utf8(archive) + "!modrinth.index.json");
    }
    if (const auto manifest = read_zip_entry(archive, "manifest.json")) {
        return parse_curseforge_manifest(
            *manifest, path_to_utf8(archive) + "!manifest.json");
    }
    fail("Not a Modrinth .mrpack or CurseForge modpack archive: " +
         path_to_utf8(archive));
}

std::string modpack_loader_command(const ModpackInfo& info,
                                   const std::filesystem::path& game_dir) {
    if (info.loader.empty()) {
        return {};
    }
    std::string command;
    if (info.loader == "fabric" || info.loader == "quilt") {
        command = "aurora loader install " + info.loader + " " +
                  info.minecraft_version;
    } else if (info.loader == "forge" || info.loader == "neoforge") {
        command = "aurora forge install " + info.loader + " " +
                  info.minecraft_version;
    } else {
        return {};
    }
    if (!info.loader_version.empty()) {
        command += " --loader " + info.loader_version;
    }
    command += " --game-dir " + quote_windows_arg(path_to_utf8(game_dir));
    return command;
}

std::string modpack_version_id(const ModpackInfo& info) {
    return sanitize_version_id(info.name);
}

ModpackInstallResult install_modpack(const ModpackInstallOptions& options) {
    if (options.archive.empty()) {
        fail("Modpack archive is required");
    }
    if (options.game_dir.empty()) {
        fail("Game directory is empty");
    }
    std::error_code error;
    const auto archive_status =
        std::filesystem::symlink_status(options.archive, error);
    if (error || !std::filesystem::is_regular_file(archive_status)) {
        fail("Modpack archive not found: " + path_to_utf8(options.archive));
    }

    ModpackInstallResult result;
    result.info = read_modpack_info(options.archive);
    const ModpackInfo& info = result.info;

    const std::filesystem::path game_dir = absolute_path(options.game_dir);
    ensure_directory(game_dir);

    result.version_id =
        options.version_id.empty() ? modpack_version_id(info)
                                   : sanitize_version_id(options.version_id);
    const std::filesystem::path instance_dir =
        game_dir / "versions" / path_from_utf8(result.version_id);
    const ExistingInstance old_marker = load_instance_marker(instance_dir);
    if (std::filesystem::exists(instance_dir, error)) {
        if (!old_marker.present) {
            fail("Version '" + result.version_id +
                 "' already exists and is not an Aurora modpack instance. "
                 "Use --version-id to install under another id.");
        }
        if (old_marker.format != info.format || old_marker.name != info.name) {
            fail("Version '" + result.version_id + "' belongs to modpack '" +
                 old_marker.name + "' (" + old_marker.format +
                 "). Use --version-id to install this pack separately.");
        }
        result.upgraded = true;
    }

    ModpackDownloader downloader = options.download;
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
    ModpackFetcher fetcher = options.fetch;
    if (!fetcher) {
        const std::string key = trim(options.curseforge_api_key);
        if (info.format == "curseforge" && key.empty()) {
            fail("CurseForge modpacks require an API key (--api-key, "
                 "CURSEFORGE_API_KEY or curseForgeApiKey)");
        }
        fetcher = [key](std::string_view url) {
            return curseforge_http_get(url, key);
        };
    }

    std::vector<ResolvedPackFile> planned;
    std::set<std::string> seen_paths;
    std::map<std::uint64_t, std::string> project_directories;
    for (const ModpackFile& file : info.files) {
        ResolvedPackFile entry;
        if (info.format == "modrinth") {
            entry.relative = checked_modpack_path(file.path, "modpack file");
            entry.url = file.url;
            entry.sha1 = file.sha1;
        } else {
            const std::string file_url =
                curseforge_file_url(file.project_id, file.file_id);
            const CurseForgeFile cf_file =
                parse_curseforge_file(fetcher(file_url), file_url);
            if (cf_file.mod_id != 0 && cf_file.mod_id != file.project_id) {
                fail("CurseForge file " + std::to_string(cf_file.id) +
                     " does not belong to project " +
                     std::to_string(file.project_id));
            }
            if (trim(cf_file.download_url).empty()) {
                fail("CurseForge file " + std::to_string(cf_file.id) +
                     " has no download URL; the author disabled third-party "
                     "distribution");
            }
            if (!is_https_url(cf_file.download_url)) {
                fail("CurseForge file " + std::to_string(cf_file.id) +
                     " does not use HTTPS");
            }
            if (!is_sha1_hex(cf_file.sha1)) {
                fail("CurseForge file " + std::to_string(cf_file.id) +
                     " has no SHA-1 hash");
            }
            if (!is_safe_component(cf_file.filename)) {
                fail("Unsafe CurseForge file name: " + cf_file.filename);
            }
            auto directory = project_directories.find(file.project_id);
            if (directory == project_directories.end()) {
                const std::string mod_url = curseforge_mod_url(file.project_id);
                const CurseForgeMod mod =
                    parse_curseforge_mod(fetcher(mod_url), mod_url);
                directory = project_directories
                                .emplace(file.project_id,
                                         curseforge_class_directory(mod.class_id))
                                .first;
            }
            entry.relative =
                checked_modpack_path(directory->second + "/" + cf_file.filename,
                                     "modpack file");
            entry.url = cf_file.download_url;
            entry.sha1 = cf_file.sha1;
            entry.project_id = file.project_id;
            entry.file_id = file.file_id;
        }

        const std::string key = to_lower_ascii(portable_relative(entry.relative));
        if (!seen_paths.insert(key).second) {
            fail("Modpack contains duplicate file path: " +
                 path_to_utf8(entry.relative));
        }
        planned.push_back(std::move(entry));
    }

    // HMCL builds the game version and loader before unpacking the pack. The
    // vanilla JSON is required by inheritance and by downloading game files.
    std::string parent = info.minecraft_version;
    result.loader_version_id.clear();
    if (options.install_game) {
        resolve_or_fetch_version(game_dir, info.minecraft_version, true);
    }
    if (options.install_loader && !info.loader.empty()) {
        result.loader_version_id =
            install_pack_loader(info, options, game_dir);
        if (!result.loader_version_id.empty()) {
            parent = result.loader_version_id;
        }
    } else if (!options.install_loader && !info.loader.empty()) {
        log_warn("Skipping mod loader installation at the caller's request; "
                 "the instance will inherit from Minecraft " +
                 info.minecraft_version + " only");
    }
    if (options.install_game) {
        install_parent_files(game_dir, parent, options);
    }

    if (result.upgraded && options.backup) {
        result.backup_path = make_backup_path(game_dir, result.version_id);
        log_info("Backing up modpack instance to " +
                 path_to_utf8(result.backup_path));
        copy_tree(instance_dir, result.backup_path);
    }

    std::filesystem::path staging;
    try {
        ensure_directory(instance_dir);
        write_instance_json(instance_dir, result.version_id, parent);

        std::map<std::string, ManagedFile> old_files;
        for (const ManagedFile& file : old_marker.files) {
            old_files.emplace(to_lower_ascii(file.path), file);
        }

        std::vector<ManagedFile> new_files;
        for (const ResolvedPackFile& file : planned) {
            const std::filesystem::path destination =
                instance_dir / file.relative;
            const std::string key = to_lower_ascii(portable_relative(file.relative));
            const auto old = old_files.find(key);
            bool replace = false;
            std::error_code destination_error;
            if (std::filesystem::is_directory(destination, destination_error)) {
                fail("Modpack file path is a directory: " +
                     path_to_utf8(destination));
            }
            if (is_regular(destination)) {
                const std::string actual = safe_file_sha1(destination);
                if (checksum_matches(file.sha1, actual)) {
                    ++result.files_skipped;
                } else if (old != old_files.end() &&
                           checksum_matches(old->second.sha1, actual)) {
                    replace = true;
                } else if (options.force) {
                    replace = true;
                } else if (old != old_files.end()) {
                    log_warn("Keeping user-modified pack file: " +
                             path_to_utf8(destination));
                    ++result.files_skipped;
                } else {
                    fail("File already exists with different contents: " +
                         path_to_utf8(destination) +
                         " (use --force to replace)");
                }
            } else {
                replace = true;
            }
            if (replace) {
                ensure_directory(destination.parent_path());
                downloader(file.url, destination, file.sha1);
                if (!checksum_matches(file.sha1, safe_file_sha1(destination))) {
                    fail("Downloaded modpack file did not match its SHA-1: " +
                         path_to_utf8(destination));
                }
                ++result.files_installed;
            }

            ManagedFile managed;
            managed.path = portable_relative(file.relative);
            managed.sha1 = file.sha1;
            managed.project_id = file.project_id;
            managed.file_id = file.file_id;
            new_files.push_back(std::move(managed));
        }

        // CurseForge removes files that disappeared from the new manifest,
        // matching CurseInstallTask. Modrinth leaves unmanaged stale jars
        // alone, matching ModrinthCompletionTask.
        if (info.format == "curseforge") {
            std::set<std::pair<std::uint64_t, std::uint64_t>> new_ids;
            for (const ResolvedPackFile& file : planned) {
                new_ids.emplace(file.project_id, file.file_id);
            }
            for (const ManagedFile& file : old_marker.files) {
                if (new_ids.count({file.project_id, file.file_id}) != 0) {
                    continue;
                }
                const std::filesystem::path stale =
                    instance_dir / checked_modpack_path(file.path, "modpack file");
                std::error_code stale_error;
                if (std::filesystem::is_regular_file(stale, stale_error)) {
                    std::filesystem::remove(stale, stale_error);
                    if (!stale_error) {
                        ++result.files_removed;
                    }
                }
            }
        }

        std::vector<ManagedOverride> new_overrides;
        if (options.install_overrides) {
            staging = game_dir / ".aurora" /
                      ("modpack-staging-" +
                       std::to_string(std::chrono::steady_clock::now()
                                          .time_since_epoch()
                                          .count()));
            for (const std::string& prefix : info.override_prefixes) {
                extract_zip_subtree(options.archive, prefix, staging);
            }

            std::map<std::string, ManagedOverride> old_overrides;
            for (const ManagedOverride& entry : old_marker.overrides) {
                old_overrides.emplace(to_lower_ascii(entry.path), entry);
            }

            std::vector<StagedOverride> staged;
            std::set<std::string> new_keys;
            std::error_code iterate_error;
            if (std::filesystem::is_directory(staging, iterate_error)) {
                std::filesystem::recursive_directory_iterator iterator(
                    staging, iterate_error);
                const std::filesystem::recursive_directory_iterator end;
                while (!iterate_error && iterator != end) {
                    std::error_code status_error;
                    if (iterator->is_regular_file(status_error) && !status_error) {
                        const std::filesystem::path relative =
                            std::filesystem::relative(iterator->path(), staging,
                                                      iterate_error);
                        if (iterate_error) {
                            break;
                        }
                        StagedOverride entry;
                        entry.relative = relative;
                        entry.key = to_lower_ascii(portable_relative(relative));
                        entry.sha1 = sha1_file(iterator->path());
                        staged.push_back(std::move(entry));
                    }
                    iterator.increment(iterate_error);
                }
                if (iterate_error) {
                    fail("Unable to scan modpack overrides: " +
                         iterate_error.message());
                }
            }

            for (const StagedOverride& entry : staged) {
                const std::filesystem::path destination =
                    instance_dir / entry.relative;
                const auto old = old_overrides.find(entry.key);
                bool copy = false;
                std::error_code destination_error;
                if (std::filesystem::is_directory(destination,
                                                  destination_error)) {
                    fail("Override path is a directory: " +
                         path_to_utf8(destination));
                }
                if (!old_marker.present) {
                    copy = true;
                } else if (old == old_overrides.end()) {
                    copy = true;
                } else if (!is_regular(destination)) {
                    // The user deleted this override on purpose; keep it
                    // missing instead of restoring it during an upgrade.
                    copy = false;
                } else if (checksum_matches(old->second.sha1,
                                            safe_file_sha1(destination))) {
                    copy = true;
                } else {
                    copy = false;
                }
                if (options.force && old_marker.present) {
                    copy = true;
                }
                if (copy) {
                    ensure_directory(destination.parent_path());
                    std::filesystem::copy_file(
                        staging / entry.relative, destination,
                        std::filesystem::copy_options::overwrite_existing,
                        destination_error);
                    if (destination_error) {
                        fail("Unable to write override file " +
                             path_to_utf8(destination) + ": " +
                             destination_error.message());
                    }
                    ++result.overrides_extracted;
                } else {
                    ++result.overrides_skipped;
                }

                ManagedOverride managed;
                managed.path = portable_relative(entry.relative);
                managed.sha1 = entry.sha1;
                new_overrides.push_back(std::move(managed));
                new_keys.insert(entry.key);
            }

            for (const ManagedOverride& entry : old_marker.overrides) {
                if (new_keys.count(to_lower_ascii(entry.path)) != 0) {
                    continue;
                }
                const std::filesystem::path stale =
                    instance_dir / checked_modpack_path(entry.path,
                                                        "override file");
                std::error_code stale_error;
                if (std::filesystem::is_regular_file(stale, stale_error)) {
                    std::filesystem::remove(stale, stale_error);
                    if (!stale_error) {
                        ++result.overrides_removed;
                    }
                }
            }
        } else {
            // --no-overrides keeps the previous override bookkeeping so a
            // later upgrade can still distinguish user edits.
            new_overrides = old_marker.overrides;
        }

        result.instance_dir = instance_dir;
        result.version_json =
            instance_dir / path_from_utf8(result.version_id + ".json");
        result.manifest_path = instance_dir / kModpackMarkerName;

        Json marker = Json::object();
        marker["format"] = info.format;
        marker["name"] = info.name;
        marker["version"] = info.version;
        marker["author"] = info.author;
        marker["summary"] = info.summary;
        marker["minecraft"] = info.minecraft_version;
        marker["loader"] = info.loader;
        marker["loaderVersion"] = info.loader_version;
        marker["loaderVersionId"] = result.loader_version_id;
        marker["versionId"] = result.version_id;
        marker["parent"] = parent;
        marker["installedAt"] = now_iso8601();
        marker["archive"] = path_to_utf8(absolute_path(options.archive));
        Json files_json = Json::array();
        for (const ManagedFile& file : new_files) {
            Json item = Json::object();
            item["path"] = file.path;
            item["sha1"] = file.sha1;
            if (file.project_id != 0) {
                item["projectId"] = file.project_id;
                item["fileId"] = file.file_id;
            }
            files_json.push_back(std::move(item));
        }
        marker["files"] = std::move(files_json);
        Json overrides_json = Json::array();
        for (const ManagedOverride& entry : new_overrides) {
            Json item = Json::object();
            item["path"] = entry.path;
            item["sha1"] = entry.sha1;
            overrides_json.push_back(std::move(item));
        }
        marker["overrides"] = std::move(overrides_json);
        write_text_file(result.manifest_path, json_pretty(marker) + "\n");
    } catch (...) {
        remove_tree(staging);
        if (result.upgraded && !result.backup_path.empty()) {
            log_warn("Modpack install failed; restoring instance backup");
            remove_tree(instance_dir);
            try {
                copy_tree(result.backup_path, instance_dir);
            } catch (const std::exception& restore_error) {
                log_error("Unable to restore modpack backup " +
                          path_to_utf8(result.backup_path) + ": " +
                          restore_error.what());
            }
        } else if (!result.upgraded) {
            remove_tree(instance_dir);
        } else {
            log_warn("Modpack upgrade failed; existing files were left in place "
                     "(backups were disabled)");
        }
        throw;
    }
    remove_tree(staging);
    return result;
}

} // namespace aurora
