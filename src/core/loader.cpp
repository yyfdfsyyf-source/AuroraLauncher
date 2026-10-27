#include "core/loader.hpp"

#include "core/error.hpp"
#include "core/http.hpp"
#include "core/json.hpp"
#include "core/log.hpp"
#include "core/paths.hpp"
#include "core/strings.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <stdexcept>

namespace aurora {

namespace {

constexpr std::size_t max_version_component_length = 128;

bool is_ascii_digit(char value) {
    return value >= '0' && value <= '9';
}

bool is_safe_version_component(std::string_view value) {
    if (value.empty() || value.size() > max_version_component_length) {
        return false;
    }
    if (value == "." || value == ".." || value.front() == '.' || value.back() == '.') {
        return false;
    }
    for (const char character : value) {
        const bool alphanumeric =
            (character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z') ||
            is_ascii_digit(character);
        if (!alphanumeric && character != '.' && character != '_' && character != '-' &&
            character != '+') {
            return false;
        }
    }
    return true;
}

void require_safe_version_component(std::string_view value, std::string_view label) {
    if (value.empty()) {
        fail(std::string(label) + " is empty");
    }
    if (!is_safe_version_component(value)) {
        fail("Unsafe " + std::string(label) + " '" + std::string(value) + "'");
    }
}

bool looks_prerelease(std::string_view version) {
    return version.find('-') != std::string_view::npos;
}

std::optional<bool> optional_boolean(const Json& object, const char* key) {
    if (!object.is_object()) {
        return std::nullopt;
    }
    const auto it = object.find(key);
    if (it == object.end() || !it->is_boolean()) {
        return std::nullopt;
    }
    return it->get<bool>();
}

std::string loader_version_source(LoaderKind kind,
                                  std::string_view game_version,
                                  std::string_view source) {
    if (!source.empty()) {
        return std::string(source);
    }
    return std::string(loader_display_name(kind)) + " loader versions for Minecraft " +
           std::string(game_version);
}

std::string loader_profile_source(LoaderKind kind, std::string_view source) {
    if (!source.empty()) {
        return std::string(source);
    }
    return std::string(loader_display_name(kind)) + " loader profile";
}

std::string join_url(std::string_view base, std::string path, std::string_view label) {
    if (base.empty()) {
        fail(std::string(label) + " is empty");
    }
    std::string result(base);
    while (result.size() > 1 && result.back() == '/') {
        result.pop_back();
    }
    if (result.back() != '/') {
        result.push_back('/');
    }
    result += path;
    return result;
}

std::string fetch_url(const LoaderFetcher& fetch, const std::string& url) {
    try {
        return fetch ? fetch(url) : http_get(url);
    } catch (const Error&) {
        throw;
    } catch (const std::exception& e) {
        fail("Failed to fetch " + url + ": " + e.what());
    }
}

bool all_digits(std::string_view value) {
    return !value.empty() &&
           std::all_of(value.begin(), value.end(), [](char character) {
               return is_ascii_digit(character);
           });
}

std::string_view strip_leading_zeros(std::string_view value) {
    while (value.size() > 1 && value.front() == '0') {
        value.remove_prefix(1);
    }
    return value;
}

int compare_identifier(std::string_view left, std::string_view right) {
    const bool left_numeric = all_digits(left);
    const bool right_numeric = all_digits(right);
    if (left_numeric && right_numeric) {
        left = strip_leading_zeros(left);
        right = strip_leading_zeros(right);
        if (left.size() != right.size()) {
            return left.size() < right.size() ? -1 : 1;
        }
        if (left < right) {
            return -1;
        }
        return left > right ? 1 : 0;
    }
    if (left_numeric != right_numeric) {
        return left_numeric ? -1 : 1;
    }
    if (left < right) {
        return -1;
    }
    return left > right ? 1 : 0;
}

struct ComparableVersion {
    std::vector<std::string> core;
    std::vector<std::string> prerelease;
    bool has_prerelease = false;
};

ComparableVersion parse_comparable_version(std::string_view version) {
    std::string text(version);
    const auto build = text.find('+');
    if (build != std::string::npos) {
        text.resize(build);
    }

    ComparableVersion result;
    std::string core = text;
    const auto prerelease = text.find('-');
    if (prerelease != std::string::npos) {
        core = text.substr(0, prerelease);
        result.has_prerelease = true;
        result.prerelease = split(text.substr(prerelease + 1), '.');
    }
    result.core = split(core, '.');
    if (result.core.empty()) {
        result.core.push_back("0");
    }
    return result;
}

} // namespace

std::string_view loader_name(LoaderKind kind) {
    switch (kind) {
    case LoaderKind::Fabric:
        return "fabric";
    case LoaderKind::Quilt:
        return "quilt";
    }
    fail("Unsupported loader kind");
}

std::string_view loader_display_name(LoaderKind kind) {
    switch (kind) {
    case LoaderKind::Fabric:
        return "Fabric";
    case LoaderKind::Quilt:
        return "Quilt";
    }
    fail("Unsupported loader kind");
}

std::string fabric_game_versions_url(const LoaderEndpoints& endpoints) {
    return join_url(endpoints.fabric_meta, "v2/versions/game", "Fabric meta base URL");
}

std::string loader_versions_url(LoaderKind kind,
                                std::string_view game_version,
                                const LoaderEndpoints& endpoints) {
    require_safe_version_component(game_version, "Minecraft version");
    const std::string suffix =
        "/versions/loader/" + std::string(game_version);
    switch (kind) {
    case LoaderKind::Fabric:
        return join_url(
            endpoints.fabric_meta, "v2" + suffix, "Fabric meta base URL");
    case LoaderKind::Quilt:
        return join_url(
            endpoints.quilt_meta, "v3" + suffix, "Quilt meta base URL");
    }
    fail("Unsupported loader kind");
}

std::string loader_profile_url(LoaderKind kind,
                               std::string_view game_version,
                               std::string_view loader_version,
                               const LoaderEndpoints& endpoints) {
    require_safe_version_component(game_version, "Minecraft version");
    require_safe_version_component(loader_version, "loader version");
    const std::string suffix = "/versions/loader/" + std::string(game_version) + "/" +
                               std::string(loader_version) + "/profile/json";
    switch (kind) {
    case LoaderKind::Fabric:
        return join_url(
            endpoints.fabric_meta, "v2" + suffix, "Fabric meta base URL");
    case LoaderKind::Quilt:
        return join_url(
            endpoints.quilt_meta, "v3" + suffix, "Quilt meta base URL");
    }
    fail("Unsupported loader kind");
}

std::vector<GameVersion> parse_game_versions(std::string_view json,
                                             std::string_view source) {
    const std::string label =
        source.empty() ? "Fabric game versions" : std::string(source);
    const Json root = parse_json(json, label);
    if (!root.is_array()) {
        fail("Invalid " + label + ": expected a JSON array");
    }

    std::vector<GameVersion> versions;
    versions.reserve(root.size());
    for (std::size_t index = 0; index < root.size(); ++index) {
        const Json& entry = root[index];
        if (!entry.is_object()) {
            fail("Invalid " + label + ": entry " + std::to_string(index) +
                 " is not an object");
        }
        const std::string id = json_string(entry, "version");
        if (id.empty()) {
            fail("Invalid " + label + ": entry " + std::to_string(index) +
                 " has no \"version\"");
        }
        GameVersion version;
        version.id = id;
        version.stable =
            optional_boolean(entry, "stable").value_or(!looks_prerelease(id));
        versions.push_back(std::move(version));
    }
    return versions;
}

std::vector<LoaderVersion> parse_loader_versions(LoaderKind kind,
                                                 std::string_view json,
                                                 std::string_view game_version,
                                                 std::string_view source) {
    const std::string label = loader_version_source(kind, game_version, source);
    const Json root = parse_json(json, label);
    if (!root.is_array()) {
        fail("Invalid " + label + ": expected a JSON array");
    }

    std::vector<LoaderVersion> versions;
    versions.reserve(root.size());
    for (std::size_t index = 0; index < root.size(); ++index) {
        const Json& entry = root[index];
        if (!entry.is_object()) {
            fail("Invalid " + label + ": entry " + std::to_string(index) +
                 " is not an object");
        }

        // Fabric v2 and Quilt v3 both wrap the details in a "loader" object.
        // Accepting flat entries as well keeps fixtures and future revisions
        // simple without changing the model.
        const Json* details = json_object(entry, "loader");
        if (details == nullptr) {
            details = &entry;
        }

        const std::string version = json_string(*details, "version");
        if (version.empty()) {
            fail("Invalid " + label + ": entry " + std::to_string(index) +
                 " has no loader version");
        }
        if (!is_safe_version_component(version)) {
            fail("Invalid " + label + ": unsafe loader version '" + version + "'");
        }

        LoaderVersion parsed;
        parsed.game_version = std::string(game_version);
        parsed.version = version;
        parsed.maven = json_string(*details, "maven");
        parsed.separator = json_string(*details, "separator");
        if (const auto stable = optional_boolean(*details, "stable")) {
            parsed.stable = *stable;
        } else if (const auto wrapper_stable = optional_boolean(entry, "stable")) {
            parsed.stable = *wrapper_stable;
        } else {
            parsed.stable = !looks_prerelease(version);
        }
        versions.push_back(std::move(parsed));
    }
    return versions;
}

Json parse_loader_profile(LoaderKind kind,
                          std::string_view json,
                          std::string_view generated_id,
                          std::string_view source) {
    const std::string label = loader_profile_source(kind, source);
    require_safe_version_component(generated_id, "generated loader version id");

    Json profile = parse_json(json, label);
    if (!profile.is_object()) {
        fail("Invalid " + label + ": expected a JSON object");
    }

    const auto require_string = [&profile, &label](const char* key) {
        const auto value = json_optional_string(profile, key);
        if (!value || value->empty()) {
            fail("Invalid " + label + ": missing or empty \"" + std::string(key) + "\"");
        }
        return *value;
    };
    require_string("id");
    require_string("inheritsFrom");
    require_string("mainClass");

    const Json* libraries = json_array(profile, "libraries");
    if (libraries == nullptr) {
        fail("Invalid " + label + ": missing \"libraries\" array");
    }
    for (std::size_t index = 0; index < libraries->size(); ++index) {
        const Json& library = (*libraries)[index];
        if (!library.is_object()) {
            fail("Invalid " + label + ": library " + std::to_string(index) +
                 " is not an object");
        }
        const auto name = json_optional_string(library, "name");
        if (!name || name->empty()) {
            fail("Invalid " + label + ": library " + std::to_string(index) +
                 " has no \"name\"");
        }
    }

    profile["id"] = std::string(generated_id);
    return profile;
}

std::optional<LoaderVersion> select_loader_version(
    const std::vector<LoaderVersion>& versions,
    LoaderChannel channel) {
    bool stable_only = false;
    switch (channel) {
    case LoaderChannel::Stable:
        stable_only = true;
        break;
    case LoaderChannel::Latest:
        break;
    default:
        fail("Unsupported loader channel");
    }

    const LoaderVersion* best = nullptr;
    for (const LoaderVersion& version : versions) {
        if (version.version.empty() || (stable_only && !version.stable)) {
            continue;
        }
        if (best == nullptr) {
            best = &version;
            continue;
        }
        const int comparison = compare_loader_versions(version.version, best->version);
        if (comparison > 0 || (comparison == 0 && version.stable && !best->stable)) {
            best = &version;
        }
    }
    if (best == nullptr) {
        return std::nullopt;
    }
    return *best;
}

int compare_loader_versions(std::string_view left, std::string_view right) {
    const ComparableVersion left_version = parse_comparable_version(left);
    const ComparableVersion right_version = parse_comparable_version(right);

    const std::size_t core_size = std::max(left_version.core.size(), right_version.core.size());
    for (std::size_t index = 0; index < core_size; ++index) {
        const std::string_view left_part =
            index < left_version.core.size() ? std::string_view(left_version.core[index]) : "0";
        const std::string_view right_part =
            index < right_version.core.size() ? std::string_view(right_version.core[index]) : "0";
        const int comparison = compare_identifier(left_part, right_part);
        if (comparison != 0) {
            return comparison;
        }
    }

    if (left_version.has_prerelease != right_version.has_prerelease) {
        return left_version.has_prerelease ? -1 : 1;
    }
    if (!left_version.has_prerelease) {
        return 0;
    }

    const std::size_t prerelease_size =
        std::max(left_version.prerelease.size(), right_version.prerelease.size());
    for (std::size_t index = 0; index < prerelease_size; ++index) {
        if (index >= left_version.prerelease.size()) {
            return -1;
        }
        if (index >= right_version.prerelease.size()) {
            return 1;
        }
        const int comparison = compare_identifier(
            left_version.prerelease[index], right_version.prerelease[index]);
        if (comparison != 0) {
            return comparison;
        }
    }
    return 0;
}

std::string make_loader_version_id(LoaderKind kind,
                                   std::string_view loader_version,
                                   std::string_view game_version) {
    require_safe_version_component(loader_version, "loader version");
    require_safe_version_component(game_version, "Minecraft version");
    const std::string id = std::string(loader_name(kind)) + "-loader-" +
                           std::string(loader_version) + "-" + std::string(game_version);
    if (!is_safe_version_component(id)) {
        fail("Generated loader version id is not a safe path segment: " + id);
    }
    return id;
}

std::vector<GameVersion> fetch_fabric_game_versions(
    const LoaderEndpoints& endpoints,
    const LoaderFetcher& fetch) {
    const std::string url = fabric_game_versions_url(endpoints);
    return parse_game_versions(fetch_url(fetch, url), url);
}

std::vector<LoaderVersion> fetch_loader_versions(
    LoaderKind kind,
    std::string_view game_version,
    const LoaderEndpoints& endpoints,
    const LoaderFetcher& fetch) {
    const std::string url = loader_versions_url(kind, game_version, endpoints);
    return parse_loader_versions(kind, fetch_url(fetch, url), game_version, url);
}

Json fetch_loader_profile(LoaderKind kind,
                          std::string_view game_version,
                          std::string_view loader_version,
                          const LoaderEndpoints& endpoints,
                          const LoaderFetcher& fetch) {
    const std::string url =
        loader_profile_url(kind, game_version, loader_version, endpoints);
    const std::string id =
        make_loader_version_id(kind, loader_version, game_version);
    return parse_loader_profile(kind, fetch_url(fetch, url), id, url);
}

LoaderInstallResult install_loader(const std::filesystem::path& game_dir,
                                   const LoaderInstallOptions& options) {
    if (game_dir.empty()) {
        fail("Game directory is empty");
    }
    require_safe_version_component(options.game_version, "Minecraft version");

    std::string loader_version = options.loader_version;
    if (loader_version.empty()) {
        const std::vector<LoaderVersion> versions = fetch_loader_versions(
            options.kind, options.game_version, options.endpoints, options.fetch);
        const auto selected = select_loader_version(versions, options.channel);
        if (!selected) {
            std::string message = "No ";
            if (options.channel == LoaderChannel::Stable) {
                message += "stable ";
            }
            message += std::string(loader_display_name(options.kind)) +
                       " loader version is available for Minecraft " +
                       options.game_version;
            fail(message);
        }
        loader_version = selected->version;
    } else {
        require_safe_version_component(loader_version, "loader version");
    }

    const std::string id =
        make_loader_version_id(options.kind, loader_version, options.game_version);
    const std::filesystem::path root = absolute_path(game_dir);
    const std::filesystem::path json_path = root / "versions" / path_from_utf8(id) /
                                            path_from_utf8(id + ".json");

    LoaderInstallResult result;
    result.kind = options.kind;
    result.id = id;
    result.game_version = options.game_version;
    result.loader_version = loader_version;
    result.json_path = json_path;

    std::error_code error;
    if (!options.force && std::filesystem::is_regular_file(json_path, error)) {
        log_debug("Loader version already installed: " + path_to_utf8(json_path));
        return result;
    }

    log_info("Installing " + std::string(loader_display_name(options.kind)) +
             " loader " + loader_version + " for Minecraft " + options.game_version);
    const Json profile = fetch_loader_profile(
        options.kind, options.game_version, loader_version, options.endpoints, options.fetch);
    write_text_file(json_path, json_pretty(profile) + "\n");
    result.written = true;
    log_info("Installed loader profile: " + path_to_utf8(json_path));
    return result;
}

} // namespace aurora
