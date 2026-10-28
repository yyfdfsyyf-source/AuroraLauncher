#include "core/launch.hpp"

#include "core/branding.hpp"
#include "core/error.hpp"
#include "core/hash.hpp"
#include "core/install.hpp"
#include "core/log.hpp"
#include "core/paths.hpp"
#include "core/process.hpp"
#include "core/strings.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <string_view>

namespace aurora {

namespace {

std::string substitute(std::string_view value,
                       const std::map<std::string, std::string>& variables) {
    std::string result;
    result.reserve(value.size());
    std::size_t position = 0;
    while (position < value.size()) {
        const std::size_t open = value.find("${", position);
        if (open == std::string_view::npos) {
            result.append(value.substr(position));
            break;
        }
        result.append(value.substr(position, open - position));
        const std::size_t close = value.find('}', open + 2);
        if (close == std::string_view::npos) {
            result.append(value.substr(open));
            break;
        }
        const std::string name(value.substr(open + 2, close - open - 2));
        const auto it = variables.find(name);
        if (it != variables.end()) {
            result += it->second;
        } else {
            result.append(value.substr(open, close - open + 1));
        }
        position = close + 1;
    }
    return result;
}

bool has_option(const std::vector<std::string>& arguments, std::string_view option) {
    for (const std::string& argument : arguments) {
        if (argument == option) {
            return true;
        }
        if (argument.size() > option.size() && argument.compare(0, option.size(), option) == 0 &&
            argument[option.size()] == '=') {
            return true;
        }
    }
    return false;
}

bool has_jvm_option(const std::vector<std::string>& arguments, std::string_view prefix) {
    for (const std::string& argument : arguments) {
        if (argument == prefix ||
            (argument.size() > prefix.size() &&
             argument.compare(0, prefix.size(), prefix) == 0)) {
            return true;
        }
    }
    return false;
}

std::string jvm_property_name(std::string_view argument) {
    const std::size_t equals = argument.find('=');
    if (equals == std::string_view::npos) {
        return std::string(argument);
    }
    return std::string(argument.substr(0, equals));
}

bool has_jvm_property(const std::vector<std::string>& arguments, std::string_view property) {
    if (property.empty()) {
        return false;
    }
    for (const std::string& argument : arguments) {
        if (argument == property) {
            return true;
        }
        if (argument.size() > property.size() &&
            argument.compare(0, property.size(), property) == 0 &&
            argument[property.size()] == '=') {
            return true;
        }
    }
    return false;
}

void add_option(std::vector<std::string>& arguments,
                std::string_view option,
                std::string value) {
    if (!has_option(arguments, option)) {
        arguments.emplace_back(option);
        arguments.push_back(std::move(value));
    }
}

std::vector<std::string> split_legacy_arguments(const std::string& value) {
    std::vector<std::string> result;
    std::size_t start = 0;
    while (start < value.size()) {
        while (start < value.size() &&
               std::isspace(static_cast<unsigned char>(value[start])) != 0) {
            ++start;
        }
        if (start >= value.size()) {
            break;
        }
        std::size_t end = start;
        while (end < value.size() &&
               std::isspace(static_cast<unsigned char>(value[end])) == 0) {
            ++end;
        }
        result.push_back(value.substr(start, end - start));
        start = end;
    }
    return result;
}

std::string join_classpath(const std::vector<std::string>& entries) {
#ifdef _WIN32
    return join(entries, ";");
#else
    return join(entries, ":");
#endif
}

} // namespace

std::string LaunchPlan::command_line() const {
    return build_command_line(java, arguments);
}

LaunchPlan build_launch_plan(const ResolvedVersion& version,
                             const Environment& environment,
                             const LaunchOptions& options) {
    const auto& meta = version.meta;
    if (options.java_path.empty()) {
        fail("A Java runtime is required");
    }
    if (!std::filesystem::is_regular_file(options.java_path)) {
        fail("Java executable was not found: " + path_to_utf8(options.java_path));
    }
    if (!std::filesystem::is_regular_file(meta.client_jar(version.game_dir))) {
        fail("Client jar is missing: " + path_to_utf8(meta.client_jar(version.game_dir)) +
             ". Run 'aurora install " + meta.id + "' first.");
    }

    const auto libraries_dir = version.game_dir / "libraries";
    const auto natives_dir = meta.natives_directory(version.game_dir);
    ensure_directory(natives_dir);

    Environment launch_environment = environment;
    launch_environment.features["is_demo_user"] = options.demo;
    launch_environment.features["has_custom_resolution"] = options.width > 0 && options.height > 0;
    prepare_natives(version, launch_environment);

    std::vector<std::string> classpath_entries;
    std::set<std::string> classpath_seen;
    const auto client_jar = meta.client_jar(version.game_dir);
    classpath_entries.push_back(path_to_utf8(client_jar));
    classpath_seen.insert(path_to_utf8(absolute_path(client_jar)));

    for (const Library& library : meta.libraries) {
        if (!library.allowed(launch_environment)) {
            continue;
        }
        std::string relative;
        if (library.artifact) {
            relative = library.artifact->path.empty() ? library.relative_path() : library.artifact->path;
        } else {
            relative = library.relative_path();
        }
        if (relative.empty()) {
            fail("Unable to determine library path: " + library.name);
        }
        const auto path = libraries_dir /
            checked_relative_path(relative, "library " + library.name);
        if (!std::filesystem::is_regular_file(path)) {
            if (library.artifact) {
                fail("Library is missing: " + path_to_utf8(path) +
                     ". Run 'aurora install " + meta.id + "' first.");
            }
            if (!library.native_classifier(launch_environment)) {
                log_debug("Skipping missing non-artifact library: " + library.name);
            }
            continue;
        }
        const std::string absolute = path_to_utf8(absolute_path(path));
        if (classpath_seen.insert(absolute).second) {
            classpath_entries.push_back(absolute);
        }
    }

    const std::string classpath = join_classpath(classpath_entries);
    const std::string assets_dir = path_to_utf8(version.game_dir / "assets");
    const std::string asset_index_name =
        meta.asset_index && !meta.asset_index->id.empty()
            ? meta.asset_index->id
            : (meta.assets.empty() ? "legacy" : meta.assets);
    const auto asset_index_file = meta.asset_index_file(version.game_dir);
    if (!std::filesystem::is_regular_file(asset_index_file)) {
        fail("Asset index is missing: " + path_to_utf8(asset_index_file) +
             ". Run 'aurora install " + meta.id + "' first.");
    }
    const LegacyAssetLayout asset_layout = legacy_asset_layout(asset_index_file);
    std::string game_assets_dir = assets_dir;
    if (asset_layout.virtual_assets) {
        game_assets_dir = path_to_utf8(
            version.game_dir / "assets" / "virtual" /
            checked_relative_path(asset_index_name, "asset index"));
    } else if (asset_layout.map_to_resources) {
        game_assets_dir = path_to_utf8(version.run_dir / "resources");
    }

    if (options.username.empty()) {
        fail("A player name is required");
    }
    const bool online = options.online;
    if (online && (options.access_token.empty() || options.uuid.empty())) {
        fail("Authenticated launch requires an access token and Minecraft UUID");
    }
    const std::string auth_uuid = online ? options.uuid : offline_uuid(options.username);
    const std::string auth_access_token = online ? options.access_token : "0";
    const std::string auth_session = online
        ? "token:" + options.access_token + ":" + options.uuid
        : "-";

    std::map<std::string, std::string> variables;
    variables["auth_player_name"] = options.username;
    variables["version_name"] = meta.id;
    variables["game_directory"] = path_to_utf8(version.run_dir);
    variables["assets_root"] = assets_dir;
    variables["game_assets"] = game_assets_dir;
    variables["assets_index_name"] = asset_index_name;
    variables["auth_uuid"] = auth_uuid;
    variables["auth_access_token"] = auth_access_token;
    variables["auth_session"] = auth_session;
    variables["clientid"] = online ? options.client_id : "";
    variables["auth_xuid"] = online ? options.xuid : "";
    variables["user_type"] = online ? (options.user_type.empty() ? "msa" : options.user_type)
                                    : "legacy";
    variables["version_type"] = meta.type.empty() ? "release" : meta.type;
    variables["natives_directory"] = path_to_utf8(natives_dir);
    variables["launcher_name"] = std::string(kLauncherName);
    variables["launcher_version"] = std::string(kLauncherVersion);
    variables["classpath"] = classpath;
#ifdef _WIN32
    variables["classpath_separator"] = ";";
#else
    variables["classpath_separator"] = ":";
#endif
    variables["library_directory"] = path_to_utf8(libraries_dir);
    variables["resolution_width"] = std::to_string(options.width);
    variables["resolution_height"] = std::to_string(options.height);
    variables["user_properties"] = "{}";

    const bool logging_enabled =
        meta.logging_client && meta.logging_client->allowed(launch_environment);
    const bool logging_active =
        logging_enabled && !meta.logging_client->argument.empty();
    std::filesystem::path logging_file;
    if (logging_enabled) {
        logging_file = meta.logging_config_file(version.game_dir);
        if (!logging_file.empty()) {
            logging_file = absolute_path(logging_file);
            if (std::filesystem::is_regular_file(logging_file)) {
                variables["path"] = path_to_utf8(logging_file);
            }
        }
    }
    const auto logging_missing_message = [&meta](const std::filesystem::path& path) {
        std::string message = "Logging configuration is missing: " + path_to_utf8(path);
        if (meta.logging_client && meta.logging_client->file &&
            meta.logging_client->file->has_url()) {
            message += ". Run 'aurora install " + meta.id + "' first.";
        } else {
            message += " and the version provides no download URL for it.";
        }
        return message;
    };

    std::vector<std::string> jvm;
    jvm.push_back("-Xmx" + std::to_string(options.memory_mb) + "M");
    jvm.push_back("-Xms" + std::to_string(std::min(options.memory_mb, 512)) + "M");
    jvm.push_back("-Dfile.encoding=UTF-8");

    if (!meta.jvm_arguments.empty()) {
        for (const Argument& argument : meta.jvm_arguments) {
            if (!argument.allowed(launch_environment)) {
                continue;
            }
            for (const std::string& value : argument.values) {
                const std::string expanded = substitute(value, variables);
                jvm.push_back(expanded);
            }
        }
    }

    if (logging_active) {
        const std::string& raw_argument = meta.logging_client->argument;
        const std::string property = jvm_property_name(raw_argument);
        if (!has_jvm_property(jvm, property) &&
            !has_jvm_property(options.extra_jvm_args, property)) {
            const bool needs_path = raw_argument.find("${path}") != std::string::npos;
            if (logging_file.empty()) {
                if (needs_path) {
                    fail("Version '" + meta.id +
                         "' uses ${path} in logging.client.argument but has no logging file id");
                }
            } else if (!std::filesystem::is_regular_file(logging_file)) {
                fail(logging_missing_message(logging_file));
            }
            if (!logging_file.empty()) {
                variables["path"] = path_to_utf8(logging_file);
            }
            const std::string expanded = substitute(raw_argument, variables);
            if (!expanded.empty()) {
                jvm.push_back(expanded);
            }
        }
    }

    if (logging_enabled) {
        for (const std::string& argument : jvm) {
            if (argument.find("${path}") == std::string::npos) {
                continue;
            }
            if (logging_file.empty()) {
                fail("Version '" + meta.id +
                     "' uses ${path} but has no logging file id");
            }
            fail(logging_missing_message(logging_file));
        }
    }

    if (!has_jvm_option(jvm, "-Djava.library.path")) {
        jvm.push_back("-Djava.library.path=" + path_to_utf8(natives_dir));
    }
    if (!has_option(jvm, "-cp") && !has_option(jvm, "-classpath")) {
        jvm.push_back("-cp");
        jvm.push_back(classpath);
    }
    for (const std::string& argument : options.extra_jvm_args) {
        jvm.push_back(substitute(argument, variables));
    }

    std::vector<std::string> game;
    if (!meta.game_arguments.empty()) {
        for (const Argument& argument : meta.game_arguments) {
            if (!argument.allowed(launch_environment)) {
                continue;
            }
            for (const std::string& value : argument.values) {
                const std::string expanded = substitute(value, variables);
                game.push_back(expanded);
            }
        }
    } else if (meta.minecraft_arguments) {
        for (const std::string& value : split_legacy_arguments(*meta.minecraft_arguments)) {
            game.push_back(substitute(value, variables));
        }
    }

    add_option(game, "--username", options.username);
    add_option(game, "--version", meta.id);
    add_option(game, "--gameDir", path_to_utf8(version.run_dir));
    add_option(game, "--assetsDir", assets_dir);
    add_option(game, "--assetIndex", asset_index_name);
    add_option(game, "--uuid", variables["auth_uuid"]);
    add_option(game, "--accessToken", variables["auth_access_token"]);
    add_option(game, "--userType", variables["user_type"]);
    add_option(game, "--versionType", variables["version_type"]);
    if (online && !variables["auth_xuid"].empty()) {
        add_option(game, "--xuid", variables["auth_xuid"]);
    }
    if (online && !variables["clientid"].empty()) {
        add_option(game, "--clientId", variables["clientid"]);
    }
    if (options.width > 0 && options.height > 0) {
        add_option(game, "--width", std::to_string(options.width));
        add_option(game, "--height", std::to_string(options.height));
    }
    if (options.demo && !has_option(game, "--demo")) {
        game.push_back("--demo");
    }
    for (const std::string& argument : options.extra_game_args) {
        game.push_back(substitute(argument, variables));
    }

    LaunchPlan plan;
    plan.java = absolute_path(options.java_path);
    plan.working_directory = version.run_dir;
    plan.arguments = std::move(jvm);
    plan.arguments.push_back(meta.main_class);
    plan.arguments.insert(plan.arguments.end(), game.begin(), game.end());
    return plan;
}

int launch_game(const LaunchPlan& plan,
                bool wait,
                const std::filesystem::path& output_log) {
    // Never log the command line: it contains the Minecraft access token.
    log_info("Starting Minecraft process: " + path_to_utf8(plan.java));
    return run_process(plan.java, plan.arguments, plan.working_directory, wait, output_log);
}

} // namespace aurora
