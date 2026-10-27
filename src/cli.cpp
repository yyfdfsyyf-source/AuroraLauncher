#include "cli.hpp"

#include "core/branding.hpp"
#include "core/config.hpp"
#include "core/error.hpp"
#include "core/hash.hpp"
#include "core/install.hpp"
#include "core/java.hpp"
#include "core/json.hpp"
#include "core/launch.hpp"
#include "core/loader.hpp"
#include "core/log.hpp"
#include "core/manifest.hpp"
#include "core/paths.hpp"
#include "core/repository.hpp"
#include "core/rules.hpp"
#include "core/strings.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace aurora {

namespace {

void print_help() {
    std::cout
        << kLauncherName << " " << kLauncherVersion << " - C++ core\n\n"
        << "Usage:\n"
        << "  aurora version list [--game-dir DIR] [--json]\n"
        << "  aurora version show <id> [--game-dir DIR] [--json]\n"
        << "  aurora version latest [--snapshot] [--json]\n"
        << "  aurora version fetch <id> [--game-dir DIR] [--force] [--json]\n"
        << "  aurora java list [--json]\n"
        << "  aurora loader list <fabric|quilt> <game> [--json]\n"
        << "  aurora loader install <fabric|quilt> <game> [options]\n"
        << "  aurora install <id> [--game-dir DIR] [--force] [--no-assets] [--no-libraries] [--no-natives]\n"
        << "  aurora launch <id> [options]\n"
        << "  aurora config show|set [--config FILE]\n\n"
        << "Launch options:\n"
        << "  --game-dir DIR       Minecraft directory (default: %USERPROFILE%\\.minecraft)\n"
        << "  --java PATH          Java executable (auto-detected by default)\n"
        << "  --username NAME      Offline player name\n"
        << "  --memory MB          Maximum heap in MB (accepts 4G / 4096M)\n"
        << "  --width N --height N Window size\n"
        << "  --demo               Enable demo mode\n"
        << "  --dry-run            Print the command without starting the game\n"
        << "  --json               Print a machine-readable plan\n"
        << "  --install            Install missing files before launching\n"
        << "  --detach             Do not wait for the game process\n"
        << "  --jvm-arg VALUE      Append a JVM argument (repeatable)\n"
        << "  --game-arg VALUE     Append a game argument (repeatable)\n"
        << "  --config FILE        Configuration file\n"
        << "  --verbose            Enable debug logging\n\n"
        << "This is a no-UI core. Microsoft authentication and a GUI are not implemented yet.\n";
}

class ArgParser {
public:
    ArgParser(std::vector<std::string> arguments,
              std::set<std::string> known_flags,
              std::set<std::string> known_values)
        : arguments_(std::move(arguments)),
          known_flags_(std::move(known_flags)),
          known_values_(std::move(known_values)) {
        known_flags_.insert("--help");
        known_flags_.insert("--verbose");
        known_values_.insert("--config");
    }

    void parse() {
        for (std::size_t i = 0; i < arguments_.size(); ++i) {
            const std::string& token = arguments_[i];
            if (token == "--") {
                for (++i; i < arguments_.size(); ++i) {
                    positional_.push_back(arguments_[i]);
                }
                break;
            }
            if (token.rfind("--", 0) != 0) {
                positional_.push_back(token);
                continue;
            }

            std::string name = token;
            std::optional<std::string> value;
            const auto equals = token.find('=');
            if (equals != std::string::npos) {
                name = token.substr(0, equals);
                value = token.substr(equals + 1);
            }

            if (known_flags_.contains(name)) {
                if (value) {
                    fail("Option does not take a value: " + name);
                }
                flags_.insert(name);
                continue;
            }
            if (known_values_.contains(name)) {
                if (!value) {
                    if (i + 1 >= arguments_.size()) {
                        fail("Option requires a value: " + name);
                    }
                    value = arguments_[++i];
                }
                options_[name].push_back(*value);
                continue;
            }
            fail("Unknown option: " + name);
        }
    }

    bool flag(const std::string& name) const {
        return flags_.contains(name);
    }

    std::optional<std::string> value(const std::string& name) const {
        const auto it = options_.find(name);
        if (it == options_.end() || it->second.empty()) {
            return std::nullopt;
        }
        return it->second.back();
    }

    std::vector<std::string> values(const std::string& name) const {
        const auto it = options_.find(name);
        return it == options_.end() ? std::vector<std::string>{} : it->second;
    }

    const std::vector<std::string>& positional() const {
        return positional_;
    }

private:
    std::vector<std::string> arguments_;
    std::set<std::string> known_flags_;
    std::set<std::string> known_values_;
    std::set<std::string> flags_;
    std::map<std::string, std::vector<std::string>> options_;
    std::vector<std::string> positional_;
};

std::filesystem::path option_path(const ArgParser& parser,
                                  const std::string& name,
                                  const std::filesystem::path& fallback) {
    const auto value = parser.value(name);
    return value ? absolute_path(path_from_utf8(*value)) : fallback;
}

int parse_int(const std::string& value, const std::string& option) {
    try {
        std::size_t consumed = 0;
        const int result = std::stoi(value, &consumed);
        if (consumed != value.size()) {
            fail("Invalid value for " + option + ": " + value);
        }
        return result;
    } catch (const Error&) {
        throw;
    } catch (const std::exception&) {
        fail("Invalid value for " + option + ": " + value);
    }
}

int parse_memory_mb(const std::string& value) {
    if (value.empty()) {
        fail("Memory value is empty");
    }
    std::string number = value;
    int multiplier = 1;
    const char suffix = static_cast<char>(std::tolower(static_cast<unsigned char>(value.back())));
    if (suffix == 'g') {
        multiplier = 1024;
        number.pop_back();
    } else if (suffix == 'm') {
        number.pop_back();
    }
    const int amount = parse_int(number, "--memory");
    if (amount <= 0 || amount > 65536 / multiplier) {
        fail("Memory value is out of range: " + value);
    }
    return amount * multiplier;
}

std::filesystem::path config_path_from(const ArgParser& parser) {
    const auto value = parser.value("--config");
    return value ? absolute_path(path_from_utf8(*value)) : default_config_path();
}

std::filesystem::path game_dir_from(const ArgParser& parser, const Config& config) {
    return option_path(parser, "--game-dir", config.game_dir);
}

InstallOptions install_options_from(const Config& config) {
    InstallOptions options;
    options.download_retries = config.download_retries;
    options.download_mirrors = config.download_mirrors;
    return options;
}

ResolvedVersion resolve_version(const VersionRepository& repository,
                                const std::string& id,
                                bool allow_fetch,
                                bool force_json = false,
                                std::string_view manifest_url = kOfficialVersionManifestUrl) {
    if (repository.contains(id)) {
        return repository.resolve(id);
    }
    if (!allow_fetch) {
        fail("Version '" + id +
             "' is not installed locally. Use 'aurora version fetch " + id +
             "' or add --install to fetch it.");
    }
    log_info("Version '" + id + "' is not local; fetching version JSON");
    const VersionManifest manifest = fetch_version_manifest(manifest_url);
    VersionJsonInstallOptions options;
    options.force = force_json;
    const VersionJsonInstallResult result =
        install_version_json(manifest, id, repository.game_dir(), options);
    log_info("Version JSON is ready: " + path_to_utf8(result.path));
    return repository.resolve(id);
}

void print_library_summary(const ResolvedVersion& version, const Environment& environment) {
    std::size_t allowed = 0;
    std::size_t natives = 0;
    for (const Library& library : version.meta.libraries) {
        if (library.allowed(environment)) {
            ++allowed;
            if (library.native_classifier(environment)) {
                ++natives;
            }
        }
    }
    std::cout << "  libraries: " << version.meta.libraries.size() << " total, " << allowed
              << " for this platform, " << natives << " native\n";
}

Json version_json(const ResolvedVersion& version, const Environment& environment) {
    Json result = Json::object();
    result["id"] = version.meta.id;
    result["type"] = version.meta.type;
    result["mainClass"] = version.meta.main_class;
    result["assets"] = version.meta.assets.empty() ? "legacy" : version.meta.assets;
    result["inheritsFrom"] = version.meta.inherits_from;
    result["javaMajor"] = version.meta.java_major.value_or(0);
    result["json"] = path_to_utf8(version.json_path);
    result["clientJar"] = path_to_utf8(version.meta.client_jar(version.game_dir));
    result["assetIndex"] = path_to_utf8(version.meta.asset_index_file(version.game_dir));
    result["libraryCount"] = version.meta.libraries.size();
    std::size_t allowed = 0;
    Json libraries = Json::array();
    for (const Library& library : version.meta.libraries) {
        if (!library.allowed(environment)) {
            continue;
        }
        ++allowed;
        Json entry = Json::object();
        entry["name"] = library.name;
        entry["native"] = library.native_classifier(environment).value_or("");
        entry["path"] = library.relative_path();
        libraries.push_back(std::move(entry));
    }
    result["allowedLibraryCount"] = allowed;
    result["libraries"] = std::move(libraries);
    return result;
}

int command_version(const std::vector<std::string>& arguments) {
    if (!arguments.empty() && (arguments.front() == "--help" || arguments.front() == "-h")) {
        print_help();
        return 0;
    }
    const bool has_subcommand = !arguments.empty() && arguments.front().rfind("-", 0) != 0;
    const std::string subcommand = has_subcommand ? arguments.front() : "list";
    std::vector<std::string> rest =
        has_subcommand ? std::vector<std::string>(arguments.begin() + 1, arguments.end())
                       : arguments;
    if (subcommand == "list" || subcommand == "ls") {
        ArgParser parser(rest, {"--json"}, {"--game-dir"});
        parser.parse();
    if (parser.flag("--help")) {
        print_help();
        return 0;
    }
        if (!parser.positional().empty()) {
            fail("Unexpected argument: " + parser.positional().front());
        }
        const Config config = Config::load(config_path_from(parser));
        const VersionRepository repository(game_dir_from(parser, config));
        const auto ids = repository.list_ids();
        if (parser.flag("--json")) {
            Json output = Json::array();
            for (const std::string& id : ids) {
                output.push_back(id);
            }
            std::cout << output.dump(2) << '\n';
        } else if (ids.empty()) {
            std::cout << "No versions found in " << path_to_utf8(repository.versions_dir()) << '\n';
        } else {
            for (const std::string& id : ids) {
                std::cout << id << '\n';
            }
        }
        return 0;
    }
    if (subcommand == "show") {
        ArgParser parser(rest, {"--json"}, {"--game-dir"});
        parser.parse();
    if (parser.flag("--help")) {
        print_help();
        return 0;
    }
        if (parser.positional().size() != 1) {
            fail("Usage: aurora version show <id>");
        }
        const Config config = Config::load(config_path_from(parser));
        const VersionRepository repository(game_dir_from(parser, config));
        const ResolvedVersion version = repository.resolve(parser.positional().front());
        const Environment environment = detect_environment();
        if (parser.flag("--json")) {
            std::cout << version_json(version, environment).dump(2) << '\n';
        } else {
            std::cout << "id: " << version.meta.id << '\n';
            std::cout << "type: " << version.meta.type << '\n';
            std::cout << "main class: " << version.meta.main_class << '\n';
            std::cout << "assets: "
                      << (version.meta.assets.empty() ? "legacy" : version.meta.assets) << '\n';
            std::cout << "java: " << (version.meta.java_major ? std::to_string(*version.meta.java_major)
                                                             : "unspecified")
                      << '\n';
            std::cout << "json: " << path_to_utf8(version.json_path) << '\n';
            std::cout << "client jar: " << path_to_utf8(version.meta.client_jar(version.game_dir))
                      << '\n';
            print_library_summary(version, environment);
        }
        return 0;
    }
    if (subcommand == "latest") {
        ArgParser parser(rest, {"--json", "--snapshot"}, {"--manifest"});
        parser.parse();
        if (parser.flag("--help")) {
            print_help();
            return 0;
        }
        if (!parser.positional().empty()) {
            fail("Unexpected argument: " + parser.positional().front());
        }
        const std::string url =
            parser.value("--manifest").value_or(std::string(kOfficialVersionManifestUrl));
        const VersionManifest manifest = fetch_version_manifest(url);
        const VersionManifestEntry& entry =
            parser.flag("--snapshot") ? manifest.latest_snapshot() : manifest.latest_release();
        if (parser.flag("--json")) {
            Json output = Json::object();
            output["id"] = entry.id;
            output["type"] = entry.type;
            output["url"] = entry.url;
            output["sha1"] = entry.sha1;
            if (entry.release_time) {
                output["releaseTime"] = *entry.release_time;
            }
            std::cout << output.dump(2) << '\n';
        } else {
            std::cout << entry.id << '\n';
        }
        return 0;
    }
    if (subcommand == "fetch") {
        ArgParser parser(rest, {"--json", "--force"}, {"--game-dir", "--manifest"});
        parser.parse();
        if (parser.flag("--help")) {
            print_help();
            return 0;
        }
        if (parser.positional().size() != 1) {
            fail("Usage: aurora version fetch <id> [options]");
        }
        const Config config = Config::load(config_path_from(parser));
        const auto game_dir = game_dir_from(parser, config);
        const std::string url =
            parser.value("--manifest").value_or(std::string(kOfficialVersionManifestUrl));
        const VersionManifest manifest = fetch_version_manifest(url);
        const VersionManifestEntry& entry = manifest.require(parser.positional().front());
        VersionJsonInstallOptions options;
        options.force = parser.flag("--force");
        const VersionJsonInstallResult result =
            install_version_json(entry, game_dir, options);
        if (parser.flag("--json")) {
            Json output = Json::object();
            output["id"] = entry.id;
            output["path"] = path_to_utf8(result.path);
            output["downloaded"] = result.downloaded;
            std::cout << output.dump(2) << '\n';
        } else {
            std::cout << entry.id << '\n';
            if (result.path.empty()) {
                std::cout << "not installed\n";
            } else {
                std::cout << "json: " << path_to_utf8(result.path) << '\n';
            }
        }
        return 0;
    }
    fail("Unknown version subcommand: " + subcommand);
}

int command_java(const std::vector<std::string>& arguments) {
    if (!arguments.empty() && (arguments.front() == "--help" || arguments.front() == "-h")) {
        print_help();
        return 0;
    }
    if (arguments.empty() || arguments.front() != "list") {
        fail("Usage: aurora java list [--json]");
    }
    std::vector<std::string> rest(arguments.begin() + 1, arguments.end());
    ArgParser parser(rest, {"--json"}, {});
    parser.parse();
    if (parser.flag("--help")) {
        print_help();
        return 0;
    }
    if (!parser.positional().empty()) {
        fail("Unexpected argument: " + parser.positional().front());
    }
    const auto runtimes = detect_java_runtimes();
    if (parser.flag("--json")) {
        Json output = Json::array();
        for (const JavaRuntime& runtime : runtimes) {
            Json entry = Json::object();
            entry["major"] = runtime.major;
            entry["version"] = runtime.version;
            entry["vendor"] = runtime.vendor;
            entry["home"] = path_to_utf8(runtime.home);
            entry["executable"] = path_to_utf8(runtime.executable);
            entry["source"] = runtime.source;
            output.push_back(std::move(entry));
        }
        std::cout << output.dump(2) << '\n';
    } else if (runtimes.empty()) {
        std::cout << "No Java runtime detected\n";
    } else {
        for (const JavaRuntime& runtime : runtimes) {
            std::cout << runtime.major << "  " << runtime.version << "  "
                      << (runtime.vendor.empty() ? "unknown" : runtime.vendor) << '\n';
            std::cout << "    home: " << path_to_utf8(runtime.home) << '\n';
            std::cout << "    exe:  " << path_to_utf8(runtime.executable) << '\n';
            std::cout << "    from: " << runtime.source << '\n';
        }
    }
    return 0;
}

LoaderKind parse_loader_kind(const std::string& value) {
    if (value == "fabric") {
        return LoaderKind::Fabric;
    }
    if (value == "quilt") {
        return LoaderKind::Quilt;
    }
    fail("Unknown loader: " + value + " (expected fabric or quilt)");
}

int command_loader(const std::vector<std::string>& arguments) {
    if (arguments.empty() || arguments.front() == "--help" || arguments.front() == "-h") {
        print_help();
        return 0;
    }
    const std::string subcommand = arguments.front();
    std::vector<std::string> rest(arguments.begin() + 1, arguments.end());
    if (subcommand == "list") {
        ArgParser parser(rest, {"--json", "--latest"}, {"--game-dir"});
        parser.parse();
        if (parser.flag("--help")) {
            print_help();
            return 0;
        }
        if (parser.positional().size() != 2) {
            fail("Usage: aurora loader list <fabric|quilt> <game> [--json]");
        }
        const LoaderKind kind = parse_loader_kind(parser.positional()[0]);
        const LoaderEndpoints endpoints;
        const auto versions = fetch_loader_versions(kind, parser.positional()[1], endpoints);
        if (parser.flag("--json")) {
            Json output = Json::array();
            for (const LoaderVersion& version : versions) {
                Json entry = Json::object();
                entry["version"] = version.version;
                entry["stable"] = version.stable;
                output.push_back(std::move(entry));
            }
            std::cout << output.dump(2) << '\n';
        } else if (versions.empty()) {
            std::cout << "No loader versions found\n";
        } else {
            for (const LoaderVersion& version : versions) {
                std::cout << version.version << (version.stable ? "  stable" : "  unstable") << '\n';
            }
        }
        return 0;
    }

    if (subcommand == "install") {
        ArgParser parser(rest,
                         {"--json", "--force", "--latest", "--no-fetch"},
                         {"--game-dir", "--loader", "--channel"});
        parser.parse();
        if (parser.flag("--help")) {
            print_help();
            return 0;
        }
        if (parser.positional().size() != 2) {
            fail("Usage: aurora loader install <fabric|quilt> <game> [options]");
        }
        const Config config = Config::load(config_path_from(parser));
        const auto game_dir = game_dir_from(parser, config);
        const std::string game_version = parser.positional()[1];
        const VersionRepository repository(game_dir);
        (void)resolve_version(repository, game_version, !parser.flag("--no-fetch"));
        LoaderInstallOptions options;
        options.kind = parse_loader_kind(parser.positional()[0]);
        options.game_version = game_version;
        if (const auto loader = parser.value("--loader")) {
            options.loader_version = *loader;
        }
        const std::string channel = parser.value("--channel").value_or(
            parser.flag("--latest") ? "latest" : "stable");
        if (channel == "latest") {
            options.channel = LoaderChannel::Latest;
        } else if (channel == "stable") {
            options.channel = LoaderChannel::Stable;
        } else {
            fail("Unknown loader channel: " + channel);
        }
        options.force = parser.flag("--force");
        const LoaderInstallResult result = install_loader(game_dir, options);
        if (parser.flag("--json")) {
            Json output = Json::object();
            output["kind"] = std::string(loader_name(result.kind));
            output["id"] = result.id;
            output["gameVersion"] = result.game_version;
            output["loaderVersion"] = result.loader_version;
            output["jsonPath"] = path_to_utf8(result.json_path);
            output["written"] = result.written;
            std::cout << output.dump(2) << '\n';
        } else {
            std::cout << "loader: " << loader_display_name(result.kind) << '\n';
            std::cout << "version: " << result.id << '\n';
            std::cout << "json: " << path_to_utf8(result.json_path) << '\n';
            std::cout << (result.written ? "installed\n" : "already installed\n");
        }
        return 0;
    }

    fail("Unknown loader subcommand: " + subcommand);
}

int command_install(const std::vector<std::string>& arguments) {
    ArgParser parser(arguments,
                     {"--force", "--no-fetch", "--no-assets", "--no-libraries", "--no-natives"},
                     {"--game-dir", "--retry", "--mirror"});
    parser.parse();
    if (parser.flag("--help")) {
        print_help();
        return 0;
    }
    if (parser.positional().size() != 1) {
        fail("Usage: aurora install <id> [options]");
    }
    const Config config = Config::load(config_path_from(parser));
    const VersionRepository repository(game_dir_from(parser, config));
    const ResolvedVersion version = resolve_version(
        repository, parser.positional().front(), !parser.flag("--no-fetch"), parser.flag("--force"));
    InstallOptions options = install_options_from(config);
    options.force = parser.flag("--force");
    options.assets = !parser.flag("--no-assets");
    options.libraries = !parser.flag("--no-libraries");
    options.natives = !parser.flag("--no-natives");
    if (const auto retries = parser.value("--retry")) {
        options.download_retries = parse_int(*retries, "--retry");
        if (options.download_retries < 0 || options.download_retries > 32) {
            fail("--retry must be between 0 and 32");
        }
    }
    const auto mirrors = parser.values("--mirror");
    if (!mirrors.empty()) {
        options.download_mirrors = mirrors;
    }
    install_version(version, detect_environment(), options);
    return 0;
}

int command_launch(const std::vector<std::string>& arguments) {
    ArgParser parser(arguments,
                     {"--demo", "--dry-run", "--json", "--install", "--detach"},
                     {"--game-dir", "--java", "--username", "--memory", "--width", "--height",
                      "--jvm-arg", "--game-arg"});
    parser.parse();
    if (parser.flag("--help")) {
        print_help();
        return 0;
    }
    if (parser.positional().size() != 1) {
        fail("Usage: aurora launch <id> [options]");
    }

    const auto config_path = config_path_from(parser);
    Config config = Config::load(config_path);
    const auto game_dir = game_dir_from(parser, config);
    const std::string id = parser.positional().front();
    const VersionRepository repository(game_dir);
    const ResolvedVersion version =
        resolve_version(repository, id, parser.flag("--install"));

    LaunchOptions options;
    options.username = parser.value("--username").value_or(config.username);
    options.memory_mb = parser.value("--memory")
        ? parse_memory_mb(*parser.value("--memory"))
        : config.memory_mb;
    options.width = parser.value("--width")
        ? parse_int(*parser.value("--width"), "--width")
        : config.width;
    options.height = parser.value("--height")
        ? parse_int(*parser.value("--height"), "--height")
        : config.height;
    options.demo = parser.flag("--demo") || config.demo;
    options.extra_jvm_args = config.jvm_args;
    const auto extra_jvm = parser.values("--jvm-arg");
    options.extra_jvm_args.insert(
        options.extra_jvm_args.end(), extra_jvm.begin(), extra_jvm.end());
    options.extra_game_args = config.game_args;
    const auto extra_game = parser.values("--game-arg");
    options.extra_game_args.insert(
        options.extra_game_args.end(), extra_game.begin(), extra_game.end());

    std::optional<std::filesystem::path> java_path;
    const bool explicit_java = parser.value("--java").has_value();
    if (explicit_java) {
        java_path = absolute_path(path_from_utf8(*parser.value("--java")));
    } else if (!config.java_path.empty()) {
        java_path = config.java_path;
    }
    if (java_path && !std::filesystem::is_regular_file(*java_path)) {
        if (explicit_java) {
            fail("Java executable not found: " + path_to_utf8(*java_path));
        }
        java_path.reset();
    }
    if (!java_path) {
        const int required = version.meta.java_major.value_or(8);
        const auto runtime = find_java_runtime(required);
        if (!runtime) {
            fail("No Java runtime >= " + std::to_string(required) +
                 " was found. Install a JDK or pass --java PATH. Run 'aurora java list' to inspect "
                 "detected runtimes.");
        }
        java_path = runtime->executable;
    }
    options.java_path = *java_path;

    if (parser.flag("--install")) {
        install_version(version, detect_environment(), install_options_from(config));
    }

    const Environment environment = detect_environment();
    const LaunchPlan plan = build_launch_plan(version, environment, options);
    if (parser.flag("--dry-run") || parser.flag("--json")) {
        if (parser.flag("--json")) {
            Json output = Json::object();
            output["java"] = path_to_utf8(plan.java);
            output["workingDirectory"] = path_to_utf8(plan.working_directory);
            output["arguments"] = plan.arguments;
            output["commandLine"] = plan.command_line();
            std::cout << output.dump(2) << '\n';
        } else {
            std::cout << plan.command_line() << '\n';
        }
        return 0;
    }

    return launch_game(plan, !parser.flag("--detach"));
}

Json config_to_json(const Config& config) {
    Json value = Json::object();
    value["gameDir"] = path_to_utf8(config.game_dir);
    value["javaPath"] = path_to_utf8(config.java_path);
    value["memoryMb"] = config.memory_mb;
    value["width"] = config.width;
    value["height"] = config.height;
    value["username"] = config.username;
    value["jvmArgs"] = config.jvm_args;
    value["gameArgs"] = config.game_args;
    value["downloadRetries"] = config.download_retries;
    value["downloadMirrors"] = config.download_mirrors;
    value["demo"] = config.demo;
    return value;
}

int command_config(const std::vector<std::string>& arguments) {
    if (!arguments.empty() && (arguments.front() == "--help" || arguments.front() == "-h")) {
        print_help();
        return 0;
    }
    if (arguments.empty()) {
        fail("Usage: aurora config show|set");
    }
    const std::string subcommand = arguments.front();
    std::vector<std::string> rest(arguments.begin() + 1, arguments.end());
    ArgParser parser(rest, {"--json"}, {});
    parser.parse();
    if (parser.flag("--help")) {
        print_help();
        return 0;
    }
    const auto path = config_path_from(parser);
    Config config = Config::load(path);

    if (subcommand == "show") {
        if (!parser.positional().empty()) {
            fail("Unexpected argument: " + parser.positional().front());
        }
        if (parser.flag("--json")) {
            std::cout << config_to_json(config).dump(2) << '\n';
        } else {
            std::cout << "config: " << path_to_utf8(path) << '\n';
            std::cout << "gameDir: " << path_to_utf8(config.game_dir) << '\n';
            std::cout << "javaPath: " << path_to_utf8(config.java_path) << '\n';
            std::cout << "memoryMb: " << config.memory_mb << '\n';
            std::cout << "width: " << config.width << '\n';
            std::cout << "height: " << config.height << '\n';
            std::cout << "username: " << config.username << '\n';
            std::cout << "jvmArgs:";
            for (const std::string& argument : config.jvm_args) {
                std::cout << ' ' << argument;
            }
            std::cout << '\n';
            std::cout << "gameArgs:";
            for (const std::string& argument : config.game_args) {
                std::cout << ' ' << argument;
            }
            std::cout << '\n';
            std::cout << "downloadRetries: " << config.download_retries << '\n';
            std::cout << "downloadMirrors:";
            for (const std::string& mirror : config.download_mirrors) {
                std::cout << ' ' << mirror;
            }
            std::cout << '\n';
            std::cout << "demo: " << (config.demo ? "true" : "false") << '\n';
        }
        return 0;
    }
    if (subcommand == "set") {
        if (parser.positional().size() != 2) {
            fail("Usage: aurora config set <key> <value>");
        }
        const std::string& key = parser.positional()[0];
        const std::string& value = parser.positional()[1];
        if (key == "gameDir") {
            config.game_dir = absolute_path(path_from_utf8(value));
        } else if (key == "javaPath") {
            config.java_path = absolute_path(path_from_utf8(value));
        } else if (key == "memoryMb") {
            config.memory_mb = parse_memory_mb(value);
        } else if (key == "width") {
            config.width = parse_int(value, key);
        } else if (key == "height") {
            config.height = parse_int(value, key);
        } else if (key == "username") {
            config.username = value;
        } else if (key == "downloadRetries") {
            config.download_retries = parse_int(value, key);
            if (config.download_retries < 0 || config.download_retries > 32) {
                fail("downloadRetries must be between 0 and 32");
            }
        } else if (key == "downloadMirrors") {
            config.download_mirrors.clear();
            std::size_t start = 0;
            while (start <= value.size()) {
                const std::size_t end = value.find_first_of(",;", start);
                const std::string item = value.substr(
                    start, end == std::string::npos ? std::string::npos : end - start);
                if (!item.empty()) {
                    config.download_mirrors.push_back(item);
                }
                if (end == std::string::npos) {
                    break;
                }
                start = end + 1;
            }
        } else if (key == "demo") {
            config.demo = value == "1" || value == "true" || value == "yes";
        } else {
            fail("Unknown config key: " + key);
        }
        config.save(path);
        std::cout << "Saved " << key << " to " << path_to_utf8(path) << '\n';
        return 0;
    }
    fail("Unknown config subcommand: " + subcommand);
}

} // namespace

int run_cli(const std::vector<std::string>& arguments) {
    if (arguments.empty() || arguments.front() == "help" || arguments.front() == "--help" ||
        arguments.front() == "-h") {
        print_help();
        return 0;
    }
    if (arguments.front() == "--version") {
        std::cout << kLauncherName << " " << kLauncherVersion << "\n";
        return 0;
    }
    if (std::find(arguments.begin(), arguments.end(), "--verbose") != arguments.end()) {
        Logger::instance().set_level(LogLevel::Debug);
    }

    const std::string command = arguments.front();
    std::vector<std::string> rest(arguments.begin() + 1, arguments.end());
    if (command == "version" || command == "versions") {
        if (command == "versions") {
            rest.insert(rest.begin(), "list");
        }
        return command_version(rest);
    }
    if (command == "java") {
        return command_java(rest);
    }
    if (command == "loader") {
        return command_loader(rest);
    }
    if (command == "install") {
        return command_install(rest);
    }
    if (command == "launch") {
        return command_launch(rest);
    }
    if (command == "config") {
        return command_config(rest);
    }
    fail("Unknown command: " + command + ". Use 'aurora help'.");
}

int run_cli(int argc, char** argv) {
    std::vector<std::string> arguments;
    for (int i = 1; i < argc; ++i) {
        arguments.emplace_back(argv[i]);
    }
    return run_cli(arguments);
}

} // namespace aurora
