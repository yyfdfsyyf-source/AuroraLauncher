#include "cli.hpp"

#include "core/auth.hpp"
#include "core/branding.hpp"
#include "core/config.hpp"
#include "core/curseforge.hpp"
#include "core/error.hpp"
#include "core/forge.hpp"
#include "core/hash.hpp"
#include "core/http.hpp"
#include "core/install.hpp"
#include "core/java.hpp"
#include "core/java_install.hpp"
#include "core/json.hpp"
#include "core/launch.hpp"
#include "core/loader.hpp"
#include "core/log.hpp"
#include "core/manifest.hpp"
#include "core/modpack.hpp"
#include "core/modrinth.hpp"
#include "core/mods.hpp"
#include "core/paths.hpp"
#include "core/pcl.hpp"
#include "core/repository.hpp"
#include "core/resourcepacks.hpp"
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
#include <utility>
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
        << "  aurora java list [--game-dir DIR] [--json]\n"
        << "  aurora java install [--feature-version N] [--game-dir DIR] [--force] [--json]\n"
        << "  aurora loader list <fabric|quilt> <game> [--json]\n"
        << "  aurora loader install <fabric|quilt> <game> [options]\n"
        << "  aurora forge list <forge|neoforge> <game> [--json]\n"
        << "  aurora forge install <forge|neoforge> <game> [options]\n"
        << "  aurora mod search <query> [--loader L] [--game-version V] [--limit N] [--json]\n"
        << "  aurora mod install <slug> --game-dir DIR --loader L --game-version V [options]\n"
        << "  aurora mod list [--game-dir DIR] [--instance ID] [--json]\n"
        << "  aurora mod enable|disable <file> [--game-dir DIR] [--instance ID] [--json]\n"
        << "  aurora mod update --game-dir DIR --loader L --game-version V [--prerelease] [--enabled-only] [--apply] [--json]\n"
        << "  aurora curseforge search <query> [--loader L] [--game-version V] [--limit N] [--api-key KEY] [--json]\n"
        << "  aurora curseforge install <mod-id> --game-dir DIR --loader L --game-version V [options]\n"
        << "  aurora curseforge update --game-dir DIR --loader L --game-version V [--apply] [--json]\n"
        << "  aurora resourcepack list [--game-dir DIR] [--instance ID] [--game-version V] [--json]\n"
        << "  aurora resourcepack enable|disable <name> [--game-dir DIR] [--instance ID] [--game-version V] [--json]\n"
        << "  aurora modpack info <archive> [--json]\n"
        << "  aurora modpack install <archive> --game-dir DIR [options]\n"
        << "  aurora auth login [--client-id ID] [--json]\n"
        << "  aurora auth status|refresh|logout [--json]\n"
        << "  aurora install <id> [--game-dir DIR] [--force] [--no-fetch] [--no-assets] [--no-libraries] [--no-natives]\n"
        << "  aurora launch <id> [options]\n"
        << "  aurora config show|set [--config FILE]\n\n"
        << "Launch options:\n"
        << "  --game-dir DIR       Minecraft directory (default: %USERPROFILE%\\.minecraft)\n"
        << "  --java PATH          Java executable (overrides PCL instance and config)\n"
        << "  --username NAME      Offline player name (also forces offline mode)\n"
        << "  --offline            Ignore the saved Microsoft session\n"
        << "  --memory MB          Maximum heap in MB (accepts 4G / 4096M)\n"
        << "  --width N --height N Window size\n"
        << "  --demo               Enable demo mode\n"
        << "  --dry-run            Print the command without starting the game\n"
        << "  --json               Print a machine-readable plan\n"
        << "  --install            Install missing files before launching\n"
        << "  --detach             Do not wait for the game process\n"
        << "  --jvm-arg VALUE      Append a JVM argument (repeatable)\n"
        << "  --game-arg VALUE     Append a game argument (repeatable)\n"
        << "  --log FILE           Capture the game's stdout/stderr into FILE (appended)\n"
        << "  --config FILE        Configuration file\n"
        << "  --verbose            Enable debug logging\n\n"
        << "PCL instances: versions/<id>/PCL/Setup.ini or PCL/config.v1.yml isolation,\n"
        << "memory, JVM/game arguments and Java selection are applied automatically.\n\n"
        << "Forge install options:\n"
        << "  --loader VERSION     Pin a loader version (default: recommended/newest)\n"
        << "  --channel NAME       recommended or latest (Forge only)\n"
        << "  --java-home DIR      Java runtime used to run the installer\n"
        << "  --sha1 HEX           Pin the installer SHA-1 instead of fetching its sidecar\n"
        << "  --allow-unverified   Run the installer without a verified SHA-1\n"
        << "  --no-fetch           Do not fetch the base game version JSON first\n\n"
        << "Modpack install options:\n"
        << "  --version-id ID      Instance/version id (default: pack name)\n"
        << "  --no-loader          Do not install the pack loader\n"
        << "  --no-game            Do not download client/libraries/assets\n"
        << "  --no-backup          Skip the pre-upgrade instance backup\n"
        << "  --java-home DIR      Java runtime for the Forge/NeoForge installer\n\n"
        << "Mod options:\n"
        << "  --loader NAME        fabric, quilt, forge or neoforge\n"
        << "  --game-version VER   Minecraft version for search or install\n"
        << "  --limit N            Search result count (1-50, default 10)\n"
        << "  --version ID         Pin a specific Modrinth version id\n"
        << "  --prerelease         Allow alpha/beta versions\n"
        << "  --no-deps            Do not install required Modrinth dependencies\n"
        << "  --mirror URL         Additional file mirror (repeatable)\n"
        << "  --retry N            Download retries per source\n"
        << "  --instance ID        Target a modpack instance under versions/<ID>\n\n"
        << "This is a no-UI core. A GUI is not implemented yet.\n";
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

// Modpack instances keep mods, resource packs and options inside
// versions/<id>; --instance selects that run directory.
std::filesystem::path content_dir_from(const ArgParser& parser, const Config& config) {
    const std::filesystem::path game_dir = game_dir_from(parser, config);
    const auto instance = parser.value("--instance");
    if (!instance) {
        return game_dir;
    }
    return VersionRepository(game_dir).run_dir_for(*instance);
}

std::string instance_minecraft_version(const ArgParser& parser, const Config& config) {
    if (const auto version = parser.value("--game-version")) {
        return trim(*version);
    }
    const auto instance = parser.value("--instance");
    if (!instance) {
        return {};
    }
    const std::filesystem::path marker =
        game_dir_from(parser, config) / "versions" / path_from_utf8(*instance) /
        "modpack.json";
    std::error_code error;
    if (!std::filesystem::is_regular_file(marker, error) || error) {
        return {};
    }
    try {
        return trim(json_string(read_json_file(marker), "minecraft"));
    } catch (const Error&) {
        return {};
    }
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
    return resolve_or_fetch_version(
        repository.game_dir(), id, allow_fetch, force_json, manifest_url);
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
        const auto game_dir = game_dir_from(parser, config);
        const auto runtimes = detect_java_runtimes(game_dir);
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
                entry["managed"] = runtime.source == "managed";
                output.push_back(std::move(entry));
            }
            std::cout << output.dump(2) << '\n';
        } else if (runtimes.empty()) {
            std::cout << "No Java runtime detected\n";
        } else {
            for (const JavaRuntime& runtime : runtimes) {
                std::cout << runtime.major << "  " << runtime.version << "  "
                          << (runtime.vendor.empty() ? "unknown" : runtime.vendor);
                if (runtime.source == "managed") {
                    std::cout << "  [managed]";
                }
                std::cout << '\n';
                std::cout << "    home: " << path_to_utf8(runtime.home) << '\n';
                std::cout << "    exe:  " << path_to_utf8(runtime.executable) << '\n';
                std::cout << "    from: " << runtime.source << '\n';
            }
        }
        return 0;
    }

    if (subcommand == "install") {
        ArgParser parser(
            rest, {"--json", "--force"}, {"--game-dir", "--feature-version", "--api-url"});
        parser.parse();
        if (parser.flag("--help")) {
            print_help();
            return 0;
        }
        if (parser.positional().size() > 1) {
            fail("Usage: aurora java install [--feature-version N] [--game-dir DIR] [--force]");
        }
        const Config config = Config::load(config_path_from(parser));
        const auto game_dir = game_dir_from(parser, config);
        JavaInstallOptions options;
        if (const auto feature = parser.value("--feature-version")) {
            options.feature_version = parse_int(*feature, "--feature-version");
        }
        if (!parser.positional().empty()) {
            if (parser.value("--feature-version")) {
                fail("Specify the Java feature version either positionally or with "
                     "--feature-version, not both");
            }
            options.feature_version = parse_int(parser.positional().front(), "java version");
        }
        if (options.feature_version <= 0) {
            fail("Java feature version must be positive");
        }
        options.force = parser.flag("--force");
        if (const auto api_url = parser.value("--api-url")) {
            options.api_url = *api_url;
        }
        const JavaRuntime runtime = install_java_runtime(game_dir, options);
        if (parser.flag("--json")) {
            Json output = Json::object();
            output["major"] = runtime.major;
            output["version"] = runtime.version;
            output["vendor"] = runtime.vendor;
            output["home"] = path_to_utf8(runtime.home);
            output["executable"] = path_to_utf8(runtime.executable);
            output["source"] = runtime.source;
            std::cout << output.dump(2) << '\n';
        } else {
            std::cout << "Java " << runtime.major << " is ready\n";
            std::cout << "version: " << runtime.version << '\n';
            std::cout << "vendor: " << (runtime.vendor.empty() ? "unknown" : runtime.vendor) << '\n';
            std::cout << "home: " << path_to_utf8(runtime.home) << '\n';
            std::cout << "exe:  " << path_to_utf8(runtime.executable) << '\n';
        }
        return 0;
    }

    fail("Unknown java subcommand: " + subcommand);
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

ForgeKind parse_forge_kind(const std::string& value) {
    if (value == "forge") {
        return ForgeKind::Forge;
    }
    if (value == "neoforge") {
        return ForgeKind::NeoForge;
    }
    fail("Unknown loader kind: " + value + " (expected forge or neoforge)");
}

std::string_view forge_kind_token(ForgeKind kind) {
    return kind == ForgeKind::Forge ? "forge" : "neoforge";
}

std::string_view forge_kind_display(ForgeKind kind) {
    return kind == ForgeKind::Forge ? "Forge" : "NeoForge";
}

std::string_view forge_channel_token(ForgeChannel channel) {
    return channel == ForgeChannel::Recommended ? "recommended" : "latest";
}

ForgeChannel parse_forge_channel(const std::string& value) {
    if (value == "recommended") {
        return ForgeChannel::Recommended;
    }
    if (value == "latest") {
        return ForgeChannel::Latest;
    }
    fail("Unknown Forge channel: " + value + " (expected recommended or latest)");
}

int command_forge(const std::vector<std::string>& arguments) {
    if (arguments.empty() || arguments.front() == "--help" || arguments.front() == "-h") {
        print_help();
        return 0;
    }
    const std::string subcommand = arguments.front();
    std::vector<std::string> rest(arguments.begin() + 1, arguments.end());

    if (subcommand == "list") {
        ArgParser parser(rest, {"--json"}, {"--promotions-url", "--metadata-url"});
        parser.parse();
        if (parser.flag("--help")) {
            print_help();
            return 0;
        }
        if (parser.positional().size() != 2) {
            fail("Usage: aurora forge list <forge|neoforge> <game> [--json]");
        }
        const ForgeKind kind = parse_forge_kind(parser.positional()[0]);
        const std::string minecraft_version = parser.positional()[1];
        if (!is_safe_minecraft_version(minecraft_version)) {
            fail("Invalid Minecraft version: " + minecraft_version);
        }
        ForgeEndpoints endpoints;
        if (const auto url = parser.value("--promotions-url")) {
            endpoints.forge_promotions_url = *url;
            endpoints.forge_promotions_mirror_url.clear();
        }
        if (const auto url = parser.value("--metadata-url")) {
            endpoints.neoforge_metadata_url = *url;
            endpoints.neoforge_versions_mirror_url.clear();
        }

        if (kind == ForgeKind::Forge) {
            const auto promotions =
                fetch_forge_promotions(minecraft_version, endpoints);
            if (parser.flag("--json")) {
                Json output = Json::array();
                for (const ForgePromotion& promotion : promotions) {
                    Json entry = Json::object();
                    entry["version"] = promotion.version;
                    entry["channel"] = forge_channel_token(promotion.channel);
                    output.push_back(std::move(entry));
                }
                std::cout << output.dump(2) << '\n';
            } else if (promotions.empty()) {
                std::cout << "No Forge promotion found for Minecraft " << minecraft_version
                          << '\n';
            } else {
                for (const ForgePromotion& promotion : promotions) {
                    std::cout << promotion.version << "  "
                              << forge_channel_token(promotion.channel) << '\n';
                }
            }
            return 0;
        }

        const auto versions =
            fetch_neoforge_versions(minecraft_version, endpoints);
        if (parser.flag("--json")) {
            Json output = Json::array();
            for (const std::string& version : versions) {
                output.push_back(version);
            }
            std::cout << output.dump(2) << '\n';
        } else if (versions.empty()) {
            std::cout << "No NeoForge version found for Minecraft " << minecraft_version << '\n';
        } else {
            for (const std::string& version : versions) {
                std::cout << version << '\n';
            }
        }
        return 0;
    }

    if (subcommand == "install") {
        ArgParser parser(rest,
                         {"--json", "--allow-unverified", "--no-fetch"},
                         {"--game-dir", "--loader", "--channel", "--java-home", "--sha1",
                          "--promotions-url", "--metadata-url", "--forge-maven-root",
                          "--neoforge-maven-root"});
        parser.parse();
        if (parser.flag("--help")) {
            print_help();
            return 0;
        }
        if (parser.positional().size() != 2) {
            fail("Usage: aurora forge install <forge|neoforge> <game> [options]");
        }

        const Config config = Config::load(config_path_from(parser));
        const auto game_dir = game_dir_from(parser, config);
        ForgeInstallRequest request;
        request.kind = parse_forge_kind(parser.positional()[0]);
        request.minecraft_version = parser.positional()[1];
        if (!is_safe_minecraft_version(request.minecraft_version)) {
            fail("Invalid Minecraft version: " + request.minecraft_version);
        }
        request.game_dir = game_dir;
        if (const auto loader = parser.value("--loader")) {
            request.loader_version = *loader;
        }
        if (const auto channel = parser.value("--channel")) {
            if (request.kind != ForgeKind::Forge) {
                fail("--channel is only valid for Forge; NeoForge selects the newest matching "
                     "version automatically");
            }
            request.channel = parse_forge_channel(*channel);
        }
        if (const auto java_home = parser.value("--java-home")) {
            request.java_home = absolute_path(path_from_utf8(*java_home));
        }
        if (const auto sha1 = parser.value("--sha1")) {
            request.expected_sha1 = *sha1;
        }
        request.allow_unverified_installer = parser.flag("--allow-unverified");
        if (const auto url = parser.value("--promotions-url")) {
            request.endpoints.forge_promotions_url = *url;
            request.endpoints.forge_promotions_mirror_url.clear();
        }
        if (const auto url = parser.value("--metadata-url")) {
            request.endpoints.neoforge_metadata_url = *url;
            request.endpoints.neoforge_versions_mirror_url.clear();
        }
        if (const auto root = parser.value("--forge-maven-root")) {
            request.endpoints.forge_maven_root = *root;
        }
        if (const auto root = parser.value("--neoforge-maven-root")) {
            request.endpoints.neoforge_maven_root = *root;
        }

        if (!parser.flag("--no-fetch")) {
            const VersionRepository repository(game_dir);
            (void)resolve_version(repository, request.minecraft_version, true);
        }

        const ForgeInstallResult result = install_forge(request);
        if (parser.flag("--json")) {
            Json output = Json::object();
            output["kind"] = std::string(forge_kind_token(result.kind));
            output["minecraftVersion"] = result.minecraft_version;
            output["loaderVersion"] = result.loader_version;
            output["versionId"] = result.version_id;
            output["jsonPath"] = path_to_utf8(result.version_json);
            output["installerJar"] = path_to_utf8(result.installer_jar);
            output["installerLog"] = path_to_utf8(result.installer_log);
            output["processLog"] = path_to_utf8(result.process_log);
            output["sha1Verified"] = result.sha1_verified;
            output["installerExitCode"] = result.installer_exit_code;
            std::cout << output.dump(2) << '\n';
        } else {
            std::cout << "loader: " << forge_kind_display(result.kind) << " "
                      << result.loader_version << '\n';
            std::cout << "minecraft: " << result.minecraft_version << '\n';
            std::cout << "version id: " << result.version_id << '\n';
            std::cout << "json: " << path_to_utf8(result.version_json) << '\n';
            std::cout << "installer: " << path_to_utf8(result.installer_jar) << '\n';
            std::cout << "installer log: " << path_to_utf8(result.installer_log) << '\n';
            std::cout << "sha1 verified: " << (result.sha1_verified ? "yes" : "no") << '\n';
        }
        return 0;
    }

    fail("Unknown forge subcommand: " + subcommand);
}

std::filesystem::path auth_store_path() {
    return app_data_dir() / "auth.dat";
}

Json public_session_json(const StoredSession& session) {
    Json output = Json::object();
    output["username"] = session.minecraft.username;
    output["uuid"] = session.minecraft.uuid;
    output["xuid"] = session.minecraft.xuid;
    output["userType"] = session.minecraft.user_type;
    output["expired"] = session.minecraft.expired();
    if (session.minecraft.expires_at != std::chrono::system_clock::time_point{}) {
        output["expiresAt"] = std::chrono::duration_cast<std::chrono::seconds>(
                                  session.minecraft.expires_at.time_since_epoch())
                                  .count();
    }
    return output;
}

void print_session(const StoredSession& session) {
    std::cout << "username: " << session.minecraft.username << '\n';
    std::cout << "uuid: " << session.minecraft.uuid << '\n';
    if (!session.minecraft.xuid.empty()) {
        std::cout << "xuid: " << session.minecraft.xuid << '\n';
    }
    std::cout << "type: " << session.minecraft.user_type << '\n';
    std::cout << "expired: " << (session.minecraft.expired() ? "yes" : "no") << '\n';
}

int command_auth(const std::vector<std::string>& arguments) {
    if (arguments.empty() || arguments.front() == "--help" || arguments.front() == "-h") {
        print_help();
        return 0;
    }
    const std::string subcommand = arguments.front();
    std::vector<std::string> rest(arguments.begin() + 1, arguments.end());
    ArgParser parser(rest, {"--json", "--refresh"}, {"--client-id"});
    parser.parse();
    if (parser.flag("--help")) {
        print_help();
        return 0;
    }

    const auto config_path = config_path_from(parser);
    Config config = Config::load(config_path);
    const std::string client_id =
        trim(parser.value("--client-id").value_or(config.microsoft_client_id));
    AuthStore store(auth_store_path());

    if (subcommand == "login") {
        if (!parser.positional().empty()) {
            fail("Unexpected argument: " + parser.positional().front());
        }
        if (client_id.empty()) {
            fail("No Microsoft client ID is configured. Register AuroraLauncher's own Azure "
                 "application and run 'aurora config set microsoftClientId <client-id>' or pass "
                 "--client-id. Do not use another launcher's client ID.");
        }
        MicrosoftAuth auth(client_id);
        const DeviceCode code = auth.request_device_code();
        std::cout << "Sign in to Microsoft/Xbox using your own account.\n";
        if (!code.message.empty()) {
            std::cout << code.message << '\n';
        } else {
            std::cout << "Open " << code.verification_uri << " and enter code "
                      << code.user_code << '\n';
        }
        std::cout.flush();
        const StoredSession session = auth.authenticate_device_code(code);
        store.save(session);
        if (parser.flag("--json")) {
            std::cout << public_session_json(session).dump(2) << '\n';
        } else {
            std::cout << "Signed in.\n";
            print_session(session);
            std::cout << "stored: " << path_to_utf8(store.path()) << '\n';
        }
        return 0;
    }

    if (subcommand == "status" || subcommand == "refresh") {
        if (!parser.positional().empty()) {
            fail("Unexpected argument: " + parser.positional().front());
        }
        auto session = store.load();
        if (!session) {
            fail("No Microsoft account is signed in");
        }
        if (parser.flag("--refresh") || subcommand == "refresh" || session->minecraft.expired()) {
            if (client_id.empty()) {
                fail("The saved Microsoft session needs a refresh, but no client ID is "
                     "configured. Set microsoftClientId or sign in again.");
            }
            MicrosoftAuth auth(client_id);
            session = auth.refresh_session(*session);
            store.save(*session);
            std::cout << "Session refreshed.\n";
        }
        if (parser.flag("--json")) {
            std::cout << public_session_json(*session).dump(2) << '\n';
        } else {
            print_session(*session);
        }
        return 0;
    }

    if (subcommand == "logout") {
        if (!parser.positional().empty()) {
            fail("Unexpected argument: " + parser.positional().front());
        }
        store.clear();
        if (parser.flag("--json")) {
            std::cout << R"({"loggedOut":true})" << '\n';
        } else {
            std::cout << "Signed out.\n";
        }
        return 0;
    }

    fail("Unknown auth subcommand: " + subcommand);
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

std::vector<std::string> redacted_launch_arguments(const std::vector<std::string>& arguments) {
    std::vector<std::string> result = arguments;
    bool redact_next = false;
    for (std::string& argument : result) {
        if (redact_next) {
            argument = "<redacted>";
            redact_next = false;
            continue;
        }
        if (argument == "--accessToken" || argument == "--session") {
            redact_next = true;
            continue;
        }
        if (argument.rfind("--accessToken=", 0) == 0) {
            argument = "--accessToken=<redacted>";
        } else if (argument.rfind("--session=", 0) == 0) {
            argument = "--session=<redacted>";
        }
    }
    return result;
}

LaunchPlan redacted_launch_plan(const LaunchPlan& plan) {
    LaunchPlan copy = plan;
    copy.arguments = redacted_launch_arguments(copy.arguments);
    return copy;
}

int command_launch(const std::vector<std::string>& arguments) {
    ArgParser parser(arguments,
                     {"--demo", "--dry-run", "--json", "--install", "--detach", "--offline"},
                     {"--game-dir", "--java", "--username", "--memory", "--width", "--height",
                      "--jvm-arg", "--game-arg", "--log"});
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
    const PclInstanceSettings pcl =
        read_pcl_instance_settings(version.json_path.parent_path());

    LaunchOptions options;
    const bool explicit_username = parser.value("--username").has_value();
    const bool offline = parser.flag("--offline") || explicit_username;
    if (!offline) {
        AuthStore store(auth_store_path());
        auto session = store.load();
        if (session) {
            if (session->minecraft.expired()) {
                if (config.microsoft_client_id.empty()) {
                    fail("The saved Microsoft session has expired and no client ID is "
                         "configured. Run 'aurora auth refresh' after configuring "
                         "microsoftClientId, or launch with --offline.");
                }
                MicrosoftAuth auth(config.microsoft_client_id);
                session = auth.refresh_session(*session);
                store.save(*session);
            }
            options.online = true;
            options.username = session->minecraft.username;
            options.access_token = session->minecraft.access_token;
            options.uuid = session->minecraft.uuid;
            options.user_type = session->minecraft.user_type;
            options.xuid = session->minecraft.xuid;
            options.client_id = config.microsoft_client_id;
        }
    }
    if (!options.online) {
        options.username = parser.value("--username").value_or(config.username);
    }
    options.memory_mb = parser.value("--memory")
        ? parse_memory_mb(*parser.value("--memory"))
        : (pcl.detected && pcl.memory_mb > 0 ? pcl.memory_mb : config.memory_mb);
    options.width = parser.value("--width")
        ? parse_int(*parser.value("--width"), "--width")
        : config.width;
    options.height = parser.value("--height")
        ? parse_int(*parser.value("--height"), "--height")
        : config.height;
    options.demo = parser.flag("--demo") || config.demo;
    options.extra_jvm_args =
        pcl.detected && !pcl.jvm_args.empty() ? pcl.jvm_args : config.jvm_args;
    const auto extra_jvm = parser.values("--jvm-arg");
    options.extra_jvm_args.insert(
        options.extra_jvm_args.end(), extra_jvm.begin(), extra_jvm.end());
    options.extra_game_args =
        pcl.detected && !pcl.game_args.empty() ? pcl.game_args : config.game_args;
    const auto extra_game = parser.values("--game-arg");
    options.extra_game_args.insert(
        options.extra_game_args.end(), extra_game.begin(), extra_game.end());

    std::optional<std::filesystem::path> java_path;
    const bool explicit_java = parser.value("--java").has_value();
    if (explicit_java) {
        java_path = absolute_path(path_from_utf8(*parser.value("--java")));
    } else if (pcl.detected && !pcl.java_path.empty() &&
               std::filesystem::is_regular_file(path_from_utf8(pcl.java_path))) {
        java_path = path_from_utf8(pcl.java_path);
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
        const auto runtime = find_java_runtime(required, game_dir);
        if (!runtime) {
            fail("No Java runtime >= " + std::to_string(required) +
                 " was found. Run 'aurora java install " + std::to_string(required) +
                 "', install a JDK, or pass --java PATH. 'aurora java list' shows detected "
                 "runtimes.");
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
        const LaunchPlan safe_plan = redacted_launch_plan(plan);
        if (parser.flag("--json")) {
            Json output = Json::object();
            output["java"] = path_to_utf8(safe_plan.java);
            output["workingDirectory"] = path_to_utf8(safe_plan.working_directory);
            output["arguments"] = safe_plan.arguments;
            output["commandLine"] = safe_plan.command_line();
            output["online"] = options.online;
            if (pcl.detected) {
                Json pcl_json = Json::object();
                pcl_json["configPath"] = path_to_utf8(pcl.config_path);
                pcl_json["isolated"] = pcl.isolated;
                pcl_json["autoIsolated"] = pcl.auto_isolated;
                pcl_json["memoryMb"] = pcl.memory_mb;
                pcl_json["java"] = pcl.java_path;
                pcl_json["jvmArgs"] = pcl.jvm_args;
                pcl_json["gameArgs"] = pcl.game_args;
                output["pcl"] = std::move(pcl_json);
            }
            std::cout << output.dump(2) << '\n';
        } else {
            std::cout << safe_plan.command_line() << '\n';
        }
        return 0;
    }

    return launch_game(plan, !parser.flag("--detach"),
                       parser.value("--log")
                           ? absolute_path(path_from_utf8(*parser.value("--log")))
                           : std::filesystem::path{});
}

std::string shorten_text(std::string text, std::size_t max_length) {
    for (char& character : text) {
        if (character == '\n' || character == '\r' || character == '\t') {
            character = ' ';
        }
    }
    if (text.size() <= max_length) {
        return text;
    }
    text.resize(max_length);
    while (!text.empty() && (static_cast<unsigned char>(text.back()) & 0xC0) == 0x80) {
        text.pop_back();
    }
    if (!text.empty() && (static_cast<unsigned char>(text.back()) & 0x80) != 0) {
        text.pop_back();
    }
    text += "...";
    return text;
}

Json modrinth_installed_json(const ModrinthInstalledMod& mod) {
    Json project = Json::object();
    project["id"] = mod.project.id;
    project["slug"] = mod.project.slug;
    project["title"] = mod.project.title;

    Json version = Json::object();
    version["id"] = mod.version.id;
    version["number"] = mod.version.version_number;
    version["type"] = mod.version.version_type;
    version["gameVersions"] = mod.version.game_versions;
    version["loaders"] = mod.version.loaders;

    Json file = Json::object();
    file["name"] = mod.file.filename;
    file["path"] = path_to_utf8(mod.file_path);
    file["sha1"] = mod.file.sha1;
    file["size"] = mod.file.size;

    Json value = Json::object();
    value["project"] = std::move(project);
    value["version"] = std::move(version);
    value["file"] = std::move(file);
    value["downloaded"] = mod.downloaded;
    value["dependency"] = mod.dependency;
    return value;
}

const char* modrinth_update_status_token(ModrinthUpdateStatus status) {
    switch (status) {
    case ModrinthUpdateStatus::UpToDate:
        return "up-to-date";
    case ModrinthUpdateStatus::UpdateAvailable:
        return "update-available";
    case ModrinthUpdateStatus::NotOnModrinth:
        return "not-on-modrinth";
    case ModrinthUpdateStatus::Unreadable:
        return "unreadable";
    case ModrinthUpdateStatus::LookupFailed:
        return "lookup-failed";
    }
    return "unknown";
}

int command_mod(const std::vector<std::string>& arguments) {
    ArgParser parser(arguments,
                     {"--json", "--prerelease", "--no-deps", "--force",
                      "--enabled-only", "--apply"},
                     {"--game-dir", "--instance", "--loader", "--game-version",
                      "--limit", "--version", "--mirror", "--retry"});
    parser.parse();
    if (parser.flag("--help")) {
        print_help();
        return 0;
    }
    const auto& positional = parser.positional();
    if (positional.empty()) {
        fail("Usage: aurora mod search|install ...");
    }
    const std::string& subcommand = positional.front();
    const Config config = Config::load(config_path_from(parser));

    if (subcommand == "list") {
        if (positional.size() != 1) {
            fail("Usage: aurora mod list [--game-dir DIR] [--json]");
        }
        const auto game_dir = content_dir_from(parser, config);
        const std::filesystem::path directory = mods_directory(game_dir);
        const std::vector<InstalledMod> mods = scan_mods(game_dir, false, true);
        if (parser.flag("--json")) {
            Json entries = Json::array();
            for (const InstalledMod& mod : mods) {
                Json item = Json::object();
                item["file"] = mod.filename;
                item["name"] = mod.name;
                item["enabled"] = mod.enabled;
                item["size"] = mod.size;
                if (mod.has_metadata) {
                    item["displayName"] = mod.metadata.name;
                    item["version"] = mod.metadata.version;
                    item["modId"] = mod.metadata.mod_id;
                    item["loader"] = mod.metadata.loader;
                    item["authors"] = mod.metadata.authors;
                    if (!mod.metadata.description.empty()) {
                        item["description"] = mod.metadata.description;
                    }
                }
                entries.push_back(std::move(item));
            }
            Json output = Json::object();
            output["modsDir"] = path_to_utf8(directory);
            output["mods"] = std::move(entries);
            std::cout << output.dump(2) << '\n';
            return 0;
        }
        if (mods.empty()) {
            std::cout << "No mods found in " << path_to_utf8(directory) << '\n';
            return 0;
        }
        for (const InstalledMod& mod : mods) {
            std::cout << (mod.enabled ? "enabled   " : "disabled  ");
            if (mod.has_metadata && !mod.metadata.name.empty()) {
                std::cout << mod.metadata.name;
                if (!mod.metadata.version.empty()) {
                    std::cout << " " << mod.metadata.version;
                }
                if (!mod.metadata.loader.empty()) {
                    std::cout << " [" << mod.metadata.loader << "]";
                }
                std::cout << "  (" << mod.filename << ")";
            } else {
                std::cout << mod.filename;
            }
            std::cout << "  " << format_bytes(mod.size) << '\n';
        }
        return 0;
    }

    if (subcommand == "enable" || subcommand == "disable") {
        if (positional.size() != 2) {
            fail("Usage: aurora mod enable|disable <file> [--game-dir DIR] [--json]");
        }
        const bool enabled = subcommand == "enable";
        const auto game_dir = content_dir_from(parser, config);
        const ModToggleResult result = set_mod_enabled(game_dir, positional[1], enabled);
        if (parser.flag("--json")) {
            Json output = Json::object();
            output["file"] = result.filename;
            output["enabled"] = result.enabled;
            output["changed"] = result.changed;
            output["path"] = path_to_utf8(result.path);
            std::cout << output.dump(2) << '\n';
            return 0;
        }
        std::cout << result.filename << " is "
                  << (result.enabled ? "enabled" : "disabled");
        if (!result.changed) {
            std::cout << " (unchanged)";
        }
        std::cout << '\n';
        return 0;
    }

    if (subcommand == "update" || subcommand == "updates") {
        const auto loader = parser.value("--loader");
        if (!loader) {
            fail("mod update requires --loader <fabric|quilt|forge|neoforge>");
        }
        const auto game_version = parser.value("--game-version");
        if (!game_version) {
            fail("mod update requires --game-version <version>");
        }

        ModrinthUpdateOptions options;
        options.game_dir = content_dir_from(parser, config);
        options.loader = *loader;
        options.game_version = *game_version;
        options.allow_prerelease = parser.flag("--prerelease");
        options.include_disabled = !parser.flag("--enabled-only");
        options.apply = parser.flag("--apply");
        options.download_retries = config.download_retries;
        options.download_mirrors = config.download_mirrors;
        if (const auto retry = parser.value("--retry")) {
            options.download_retries = parse_int(*retry, "--retry");
            if (options.download_retries < 0 || options.download_retries > 32) {
                fail("--retry must be between 0 and 32");
            }
        }
        const auto update_mirrors = parser.values("--mirror");
        if (!update_mirrors.empty()) {
            options.download_mirrors = update_mirrors;
        }

        const ModrinthUpdateCheckResult result = check_modrinth_updates(options);
        if (parser.flag("--json")) {
            Json entries = Json::array();
            for (const ModrinthUpdateEntry& entry : result.entries) {
                Json item = Json::object();
                item["file"] = entry.local_file;
                item["enabled"] = entry.local_enabled;
                item["sha1"] = entry.sha1;
                item["status"] = modrinth_update_status_token(entry.status);
                item["applied"] = entry.applied;
                if (!entry.installed_file.empty()) {
                    item["installedPath"] = path_to_utf8(entry.installed_file);
                }
                if (entry.current) {
                    item["currentVersion"] = entry.current->version_number;
                    item["currentVersionId"] = entry.current->id;
                }
                if (entry.latest) {
                    item["latestVersion"] = entry.latest->version_number;
                    item["latestVersionId"] = entry.latest->id;
                }
                if (entry.latest_file) {
                    item["latestFile"] = entry.latest_file->filename;
                    item["latestUrl"] = entry.latest_file->url;
                    item["latestSha1"] = entry.latest_file->sha1;
                }
                if (!entry.error.empty()) {
                    item["error"] = entry.error;
                }
                entries.push_back(std::move(item));
            }
            Json output = Json::object();
            output["updates"] = result.updates;
            output["upToDate"] = result.up_to_date;
            output["notOnModrinth"] = result.not_on_modrinth;
            output["applied"] = result.applied;
            output["failures"] = result.failures;
            output["entries"] = std::move(entries);
            std::cout << output.dump(2) << '\n';
            return 0;
        }

        if (result.entries.empty()) {
            std::cout << "No mods found in "
                      << path_to_utf8(mods_directory(options.game_dir)) << '\n';
            return 0;
        }
        for (const ModrinthUpdateEntry& entry : result.entries) {
            const std::string current =
                entry.current ? entry.current->version_number : "?";
            switch (entry.status) {
            case ModrinthUpdateStatus::UpdateAvailable: {
                const std::string latest =
                    entry.latest ? entry.latest->version_number : "?";
                std::cout << "update      " << entry.local_file << "  " << current
                          << " -> " << latest;
                if (entry.latest_file) {
                    std::cout << "  (" << entry.latest_file->filename << ")";
                }
                if (entry.applied) {
                    std::cout << "  [installed]";
                }
                if (!entry.error.empty()) {
                    std::cout << (entry.applied ? "  [warning: " : "  [apply failed: ")
                              << entry.error << ']';
                }
                std::cout << '\n';
                break;
            }
            case ModrinthUpdateStatus::UpToDate:
                std::cout << "up to date  " << entry.local_file << "  " << current
                          << '\n';
                break;
            case ModrinthUpdateStatus::NotOnModrinth:
                std::cout << "unknown     " << entry.local_file
                          << "  not found on Modrinth\n";
                break;
            case ModrinthUpdateStatus::Unreadable:
            case ModrinthUpdateStatus::LookupFailed:
                std::cout << "failed      " << entry.local_file << "  "
                          << entry.error << '\n';
                break;
            }
        }
        if (options.apply) {
            std::cout << result.applied << " installed, ";
        }
        std::cout << result.updates << " update(s), " << result.up_to_date
                  << " up to date, " << result.not_on_modrinth
                  << " not on Modrinth, " << result.failures << " failure(s)\n";
        return 0;
    }

    if (subcommand == "search" || subcommand == "find") {
        if (positional.size() != 2) {
            fail("Usage: aurora mod search <query> [--loader L] "
                 "[--game-version V] [--limit N] [--json]");
        }
        ModrinthSearchOptions options;
        options.query = positional[1];
        options.loader = parser.value("--loader").value_or("");
        options.game_version = parser.value("--game-version").value_or("");
        if (const auto limit = parser.value("--limit")) {
            options.limit = parse_int(*limit, "--limit");
            if (options.limit < 1 || options.limit > 50) {
                fail("--limit must be between 1 and 50");
            }
        }

        const ModrinthSearchResult result = search_modrinth(options);
        if (parser.flag("--json")) {
            Json hits = Json::array();
            for (const ModrinthSearchHit& hit : result.hits) {
                Json item = Json::object();
                item["projectId"] = hit.project_id;
                item["slug"] = hit.slug;
                item["title"] = hit.title;
                item["description"] = hit.description;
                item["author"] = hit.author;
                item["downloads"] = hit.downloads;
                item["categories"] = hit.categories;
                hits.push_back(std::move(item));
            }
            Json output = Json::object();
            output["totalHits"] = result.total_hits;
            output["hits"] = std::move(hits);
            std::cout << output.dump(2) << '\n';
            return 0;
        }
        if (result.hits.empty()) {
            std::cout << "No mods found.\n";
            return 0;
        }
        for (const ModrinthSearchHit& hit : result.hits) {
            std::cout << hit.slug << "  " << hit.title << "  by " << hit.author
                      << "  " << hit.downloads << " downloads\n";
            if (!hit.description.empty()) {
                std::cout << "  " << shorten_text(hit.description, 160) << '\n';
            }
        }
        return 0;
    }

    if (subcommand == "install") {
        if (positional.size() != 2) {
            fail("Usage: aurora mod install <slug> --game-dir DIR --loader L "
                 "--game-version V [options]");
        }
        const auto loader = parser.value("--loader");
        if (!loader) {
            fail("mod install requires --loader <fabric|quilt|forge|neoforge>");
        }
        const auto game_version = parser.value("--game-version");
        if (!game_version) {
            fail("mod install requires --game-version <version>");
        }

        ModrinthInstallOptions options;
        options.game_dir = content_dir_from(parser, config);
        options.project = positional[1];
        options.loader = *loader;
        options.game_version = *game_version;
        options.version_id = parser.value("--version").value_or("");
        options.allow_prerelease = parser.flag("--prerelease");
        options.install_dependencies = !parser.flag("--no-deps");
        options.force = parser.flag("--force");
        options.download_retries = config.download_retries;
        options.download_mirrors = config.download_mirrors;
        if (const auto retry = parser.value("--retry")) {
            options.download_retries = parse_int(*retry, "--retry");
            if (options.download_retries < 0 || options.download_retries > 32) {
                fail("--retry must be between 0 and 32");
            }
        }
        const auto mirrors = parser.values("--mirror");
        if (!mirrors.empty()) {
            options.download_mirrors = mirrors;
        }

        const ModrinthInstallResult result = install_modrinth_mod(options);
        if (parser.flag("--json")) {
            Json dependencies = Json::array();
            for (const ModrinthInstalledMod& dependency : result.dependencies) {
                dependencies.push_back(modrinth_installed_json(dependency));
            }
            Json root = modrinth_installed_json(result.root);
            Json output = Json::object();
            output["project"] = std::move(root["project"]);
            output["version"] = std::move(root["version"]);
            output["file"] = std::move(root["file"]);
            output["downloaded"] = root["downloaded"];
            output["dependencies"] = std::move(dependencies);
            std::cout << output.dump(2) << '\n';
            return 0;
        }

        std::cout << "project: " << result.root.project.title << " ("
                  << result.root.project.slug << ")\n";
        std::cout << "version: " << result.root.version.version_number << " ("
                  << result.root.version.version_type << ")\n";
        std::cout << "file: " << result.root.file.filename << '\n';
        std::cout << "path: " << path_to_utf8(result.root.file_path) << '\n';
        std::cout << (result.root.downloaded ? "installed\n" : "already present\n");
        if (!result.dependencies.empty()) {
            std::cout << "dependencies:\n";
            for (const ModrinthInstalledMod& dependency : result.dependencies) {
                std::cout << "  " << dependency.project.slug << " "
                          << dependency.version.version_number
                          << (dependency.downloaded ? " installed" : " already present")
                          << '\n';
            }
        }
        return 0;
    }

    fail("Unknown mod subcommand: " + subcommand);
}

std::string curseforge_api_key_from(const ArgParser& parser, const Config& config) {
    if (const auto key = parser.value("--api-key"); key && !trim(*key).empty()) {
        return trim(*key);
    }
    const std::string environment = trim(environment_value("CURSEFORGE_API_KEY"));
    if (!environment.empty()) {
        return environment;
    }
    return trim(config.curseforge_api_key);
}

Json curseforge_mod_json(const CurseForgeMod& mod) {
    Json value = Json::object();
    value["id"] = mod.id;
    value["name"] = mod.name;
    value["slug"] = mod.slug;
    value["summary"] = mod.summary;
    value["author"] = mod.author;
    value["downloads"] = mod.downloads;
    return value;
}

const char* curseforge_update_status_token(CurseForgeUpdateStatus status) {
    switch (status) {
    case CurseForgeUpdateStatus::UpToDate:
        return "up-to-date";
    case CurseForgeUpdateStatus::UpdateAvailable:
        return "update-available";
    case CurseForgeUpdateStatus::NotOnCurseForge:
        return "not-on-curseforge";
    case CurseForgeUpdateStatus::Unreadable:
        return "unreadable";
    case CurseForgeUpdateStatus::LookupFailed:
        return "lookup-failed";
    }
    return "unknown";
}

int command_curseforge(const std::vector<std::string>& arguments) {
    ArgParser parser(arguments,
                     {"--json", "--prerelease", "--no-deps", "--force",
                      "--apply", "--enabled-only"},
                     {"--game-dir", "--instance", "--loader", "--game-version",
                      "--limit", "--file", "--api-key", "--mirror", "--retry"});
    parser.parse();
    if (parser.flag("--help")) {
        print_help();
        return 0;
    }
    const auto& positional = parser.positional();
    if (positional.empty()) {
        fail("Usage: aurora curseforge search|install ...");
    }
    const std::string& subcommand = positional.front();
    const Config config = Config::load(config_path_from(parser));
    const std::string api_key = curseforge_api_key_from(parser, config);

    if (subcommand == "search" || subcommand == "find") {
        if (positional.size() != 2) {
            fail("Usage: aurora curseforge search <query> [--loader L] "
                 "[--game-version V] [--limit N] [--api-key KEY] [--json]");
        }
        if (api_key.empty()) {
            fail("CurseForge API key is required (--api-key, CURSEFORGE_API_KEY "
                 "or 'aurora config set curseForgeApiKey <key>')");
        }

        CurseForgeSearchOptions options;
        options.query = positional[1];
        options.loader = parser.value("--loader").value_or("");
        options.game_version = parser.value("--game-version").value_or("");
        options.api_key = api_key;
        if (const auto limit = parser.value("--limit")) {
            options.limit = parse_int(*limit, "--limit");
            if (options.limit < 1 || options.limit > 50) {
                fail("--limit must be between 1 and 50");
            }
        }

        const CurseForgeSearchResult result = search_curseforge(options);
        if (parser.flag("--json")) {
            Json mods = Json::array();
            for (const CurseForgeMod& mod : result.mods) {
                mods.push_back(curseforge_mod_json(mod));
            }
            Json output = Json::object();
            output["totalCount"] = result.total_count;
            output["mods"] = std::move(mods);
            std::cout << output.dump(2) << '\n';
            return 0;
        }
        if (result.mods.empty()) {
            std::cout << "No mods found.\n";
            return 0;
        }
        for (const CurseForgeMod& mod : result.mods) {
            std::cout << mod.id << "  " << mod.name;
            if (!mod.author.empty()) {
                std::cout << "  by " << mod.author;
            }
            std::cout << "  " << mod.downloads << " downloads\n";
            if (!mod.summary.empty()) {
                std::cout << "  " << shorten_text(mod.summary, 160) << '\n';
            }
        }
        return 0;
    }

    if (subcommand == "update" || subcommand == "updates") {
        const auto loader = parser.value("--loader");
        if (!loader) {
            fail("curseforge update requires --loader <fabric|quilt|forge|neoforge>");
        }
        const auto game_version = parser.value("--game-version");
        if (!game_version) {
            fail("curseforge update requires --game-version <version>");
        }
        if (api_key.empty()) {
            fail("CurseForge API key is required (--api-key, CURSEFORGE_API_KEY "
                 "or 'aurora config set curseForgeApiKey <key>')");
        }

        CurseForgeUpdateOptions options;
        options.game_dir = content_dir_from(parser, config);
        options.loader = *loader;
        options.game_version = *game_version;
        options.include_disabled = !parser.flag("--enabled-only");
        options.allow_prerelease = parser.flag("--prerelease");
        options.apply = parser.flag("--apply");
        options.api_key = api_key;
        options.download_retries = config.download_retries;
        options.download_mirrors = config.download_mirrors;
        if (const auto retry = parser.value("--retry")) {
            options.download_retries = parse_int(*retry, "--retry");
            if (options.download_retries < 0 || options.download_retries > 32) {
                fail("--retry must be between 0 and 32");
            }
        }
        const auto mirrors = parser.values("--mirror");
        if (!mirrors.empty()) {
            options.download_mirrors = mirrors;
        }

        const CurseForgeUpdateResult result = check_curseforge_updates(options);
        if (parser.flag("--json")) {
            Json entries = Json::array();
            for (const CurseForgeUpdateEntry& entry : result.entries) {
                Json item = Json::object();
                item["file"] = entry.local_file;
                item["enabled"] = entry.enabled;
                item["fingerprint"] = entry.fingerprint;
                item["status"] = curseforge_update_status_token(entry.status);
                item["applied"] = entry.applied;
                if (!entry.installed_file.empty()) {
                    item["installedPath"] = path_to_utf8(entry.installed_file);
                }
                if (entry.current) {
                    item["currentFileId"] = entry.current->id;
                    item["currentModId"] = entry.current->mod_id;
                    item["currentName"] = entry.current->display_name;
                }
                if (entry.latest) {
                    item["latestFileId"] = entry.latest->id;
                    item["latestFile"] = entry.latest->filename;
                    item["latestUrl"] = entry.latest->download_url;
                    item["latestSha1"] = entry.latest->sha1;
                }
                if (!entry.error.empty()) {
                    item["error"] = entry.error;
                }
                entries.push_back(std::move(item));
            }
            Json output = Json::object();
            output["updates"] = result.updates;
            output["upToDate"] = result.up_to_date;
            output["notOnCurseForge"] = result.not_on_curseforge;
            output["applied"] = result.applied;
            output["failures"] = result.failures;
            output["entries"] = std::move(entries);
            std::cout << output.dump(2) << '\n';
            return 0;
        }

        if (result.entries.empty()) {
            std::cout << "No mods found in "
                      << path_to_utf8(mods_directory(options.game_dir)) << '\n';
            return 0;
        }
        for (const CurseForgeUpdateEntry& entry : result.entries) {
            switch (entry.status) {
            case CurseForgeUpdateStatus::UpdateAvailable:
                std::cout << "update      " << entry.local_file;
                if (entry.latest) {
                    std::cout << "  -> " << entry.latest->filename;
                }
                if (entry.applied) {
                    std::cout << "  [installed]";
                }
                if (!entry.error.empty()) {
                    std::cout << (entry.applied ? "  [warning: " : "  [apply failed: ")
                              << entry.error << ']';
                }
                std::cout << '\n';
                break;
            case CurseForgeUpdateStatus::UpToDate:
                std::cout << "up to date  " << entry.local_file << '\n';
                break;
            case CurseForgeUpdateStatus::NotOnCurseForge:
                std::cout << "unknown     " << entry.local_file
                          << "  not matched by CurseForge fingerprint\n";
                break;
            case CurseForgeUpdateStatus::Unreadable:
            case CurseForgeUpdateStatus::LookupFailed:
                std::cout << "failed      " << entry.local_file << "  "
                          << entry.error << '\n';
                break;
            }
        }
        if (options.apply) {
            std::cout << result.applied << " installed, ";
        }
        std::cout << result.updates << " update(s), " << result.up_to_date
                  << " up to date, " << result.not_on_curseforge
                  << " not on CurseForge, " << result.failures << " failure(s)\n";
        return 0;
    }

    if (subcommand == "install") {
        if (positional.size() != 2) {
            fail("Usage: aurora curseforge install <mod-id> --game-dir DIR "
                 "--loader L --game-version V [options]");
        }
        const auto loader = parser.value("--loader");
        if (!loader) {
            fail("curseforge install requires --loader <fabric|quilt|forge|neoforge>");
        }
        const auto game_version = parser.value("--game-version");
        if (!game_version) {
            fail("curseforge install requires --game-version <version>");
        }
        const int mod_id = parse_int(positional[1], "mod id");
        if (mod_id <= 0) {
            fail("CurseForge mod id must be positive");
        }
        if (api_key.empty()) {
            fail("CurseForge API key is required (--api-key, CURSEFORGE_API_KEY "
                 "or 'aurora config set curseForgeApiKey <key>')");
        }

        CurseForgeInstallOptions options;
        options.game_dir = content_dir_from(parser, config);
        options.mod_id = static_cast<std::uint64_t>(mod_id);
        options.loader = *loader;
        options.game_version = *game_version;
        options.allow_prerelease = parser.flag("--prerelease");
        options.install_dependencies = !parser.flag("--no-deps");
        options.force = parser.flag("--force");
        options.api_key = api_key;
        options.download_retries = config.download_retries;
        options.download_mirrors = config.download_mirrors;
        if (const auto file_id = parser.value("--file")) {
            const int parsed = parse_int(*file_id, "--file");
            if (parsed <= 0) {
                fail("--file must be positive");
            }
            options.file_id = static_cast<std::uint64_t>(parsed);
        }
        if (const auto retry = parser.value("--retry")) {
            options.download_retries = parse_int(*retry, "--retry");
            if (options.download_retries < 0 || options.download_retries > 32) {
                fail("--retry must be between 0 and 32");
            }
        }
        const auto mirrors = parser.values("--mirror");
        if (!mirrors.empty()) {
            options.download_mirrors = mirrors;
        }

        const CurseForgeInstallResult result = install_curseforge_mod(options);
        if (parser.flag("--json")) {
            Json dependencies = Json::array();
            for (const CurseForgeInstalledMod& dependency : result.dependencies) {
                Json item = Json::object();
                item["mod"] = curseforge_mod_json(dependency.mod);
                item["file"] = dependency.file.filename;
                item["path"] = path_to_utf8(dependency.file_path);
                item["downloaded"] = dependency.downloaded;
                dependencies.push_back(std::move(item));
            }
            Json root = Json::object();
            root["mod"] = curseforge_mod_json(result.root.mod);
            root["file"] = result.root.file.filename;
            root["path"] = path_to_utf8(result.root.file_path);
            root["downloaded"] = result.root.downloaded;
            Json output = Json::object();
            output["mod"] = std::move(root["mod"]);
            output["file"] = std::move(root["file"]);
            output["path"] = std::move(root["path"]);
            output["downloaded"] = std::move(root["downloaded"]);
            output["dependencies"] = std::move(dependencies);
            std::cout << output.dump(2) << '\n';
            return 0;
        }

        std::cout << "mod: " << result.root.mod.name << " ("
                  << result.root.mod.id << ")\n";
        std::cout << "file: " << result.root.file.filename << '\n';
        std::cout << "path: " << path_to_utf8(result.root.file_path) << '\n';
        std::cout << (result.root.downloaded ? "installed\n" : "already present\n");
        if (!result.dependencies.empty()) {
            std::cout << "dependencies:\n";
            for (const CurseForgeInstalledMod& dependency : result.dependencies) {
                std::cout << "  " << dependency.mod.name << " "
                          << dependency.file.filename
                          << (dependency.downloaded ? " installed" : " already present")
                          << '\n';
            }
        }
        return 0;
    }

    fail("Unknown curseforge subcommand: " + subcommand);
}

const char* resource_pack_compatibility_token(ResourcePackCompatibility compatibility) {
    switch (compatibility) {
    case ResourcePackCompatibility::Compatible:
        return "compatible";
    case ResourcePackCompatibility::TooOld:
        return "too-old";
    case ResourcePackCompatibility::TooNew:
        return "too-new";
    case ResourcePackCompatibility::Invalid:
        return "invalid";
    case ResourcePackCompatibility::MissingMetadata:
        return "missing-pack-mcmeta";
    case ResourcePackCompatibility::MissingGameMetadata:
        return "missing-game-version";
    case ResourcePackCompatibility::Unknown:
        break;
    }
    return "unknown";
}

int command_resourcepack(const std::vector<std::string>& arguments) {
    ArgParser parser(arguments, {"--json"}, {"--game-dir", "--instance", "--game-version"});
    parser.parse();
    if (parser.flag("--help")) {
        print_help();
        return 0;
    }
    const auto& positional = parser.positional();
    if (positional.empty()) {
        fail("Usage: aurora resourcepack list|enable|disable ...");
    }
    const std::string& subcommand = positional.front();
    const Config config = Config::load(config_path_from(parser));

    if (subcommand == "list") {
        if (positional.size() != 1) {
            fail("Usage: aurora resourcepack list [--game-dir DIR] [--json]");
        }
        const auto game_dir = content_dir_from(parser, config);
        const std::filesystem::path directory = resourcepacks_directory(game_dir);
        const ResourcePackContext context = resource_pack_context(
            game_dir_from(parser, config), instance_minecraft_version(parser, config));
        const std::vector<InstalledResourcePack> packs =
            scan_resource_packs(game_dir, context);
        if (parser.flag("--json")) {
            Json entries = Json::array();
            for (const InstalledResourcePack& pack : packs) {
                Json item = Json::object();
                item["name"] = pack.name;
                item["path"] = path_to_utf8(pack.path);
                item["type"] = pack.directory ? "directory" : "zip";
                item["enabled"] = pack.enabled;
                item["packFormat"] = pack.pack_format;
                item["hasMetadata"] = pack.has_metadata;
                item["hasIcon"] = pack.has_icon;
                item["compatibility"] =
                    resource_pack_compatibility_token(pack.compatibility);
                if (!pack.description.empty()) {
                    item["description"] = pack.description;
                }
                entries.push_back(std::move(item));
            }
            Json output = Json::object();
            output["resourcepacksDir"] = path_to_utf8(directory);
            output["packs"] = std::move(entries);
            std::cout << output.dump(2) << '\n';
            return 0;
        }
        if (packs.empty()) {
            std::cout << "No resource packs found in " << path_to_utf8(directory)
                      << '\n';
            return 0;
        }
        for (const InstalledResourcePack& pack : packs) {
            std::cout << (pack.enabled ? "enabled   " : "disabled  ") << pack.name
                      << (pack.directory ? "/" : "") << '\n';
        }
        return 0;
    }

    if (subcommand == "enable" || subcommand == "disable") {
        if (positional.size() != 2) {
            fail("Usage: aurora resourcepack enable|disable <name> "
                 "[--game-dir DIR] [--json]");
        }
        const bool enabled = subcommand == "enable";
        const auto game_dir = content_dir_from(parser, config);
        const ResourcePackContext context = resource_pack_context(
            game_dir_from(parser, config), instance_minecraft_version(parser, config));
        const ResourcePackToggleResult result =
            set_resource_pack_enabled(game_dir, positional[1], enabled, context);
        if (parser.flag("--json")) {
            Json output = Json::object();
            output["name"] = result.name;
            output["enabled"] = result.enabled;
            output["changed"] = result.changed;
            output["optionsPath"] = path_to_utf8(result.options_path);
            std::cout << output.dump(2) << '\n';
            return 0;
        }
        std::cout << result.name << " is "
                  << (result.enabled ? "enabled" : "disabled");
        if (!result.changed) {
            std::cout << " (unchanged)";
        }
        std::cout << '\n';
        return 0;
    }

    fail("Unknown resourcepack subcommand: " + subcommand);
}

Json modpack_info_json(const ModpackInfo& info) {
    Json files = Json::array();
    for (const ModpackFile& file : info.files) {
        Json item = Json::object();
        if (!file.path.empty()) {
            item["path"] = file.path;
        }
        if (file.project_id != 0) {
            item["projectId"] = file.project_id;
            item["fileId"] = file.file_id;
        }
        item["sha1"] = file.sha1;
        item["size"] = file.size;
        files.push_back(std::move(item));
    }
    Json value = Json::object();
    value["format"] = info.format;
    value["name"] = info.name;
    value["version"] = info.version;
    value["author"] = info.author;
    value["summary"] = info.summary;
    value["minecraft"] = info.minecraft_version;
    value["loader"] = info.loader;
    value["loaderVersion"] = info.loader_version;
    value["files"] = std::move(files);
    value["overridePrefixes"] = info.override_prefixes;
    return value;
}

int command_modpack(const std::vector<std::string>& arguments) {
    ArgParser parser(arguments,
                     {"--json", "--force", "--no-overrides", "--no-loader",
                      "--no-game", "--no-backup"},
                     {"--game-dir", "--api-key", "--mirror", "--retry",
                      "--version-id", "--java-home"});
    parser.parse();
    if (parser.flag("--help")) {
        print_help();
        return 0;
    }
    const auto& positional = parser.positional();
    if (positional.empty()) {
        fail("Usage: aurora modpack info|install ...");
    }
    const std::string& subcommand = positional.front();
    const Config config = Config::load(config_path_from(parser));

    if (subcommand == "info") {
        if (positional.size() != 2) {
            fail("Usage: aurora modpack info <archive> [--json]");
        }
        const ModpackInfo info =
            read_modpack_info(path_from_utf8(positional[1]));
        if (parser.flag("--json")) {
            std::cout << modpack_info_json(info).dump(2) << '\n';
            return 0;
        }
        std::cout << "name: " << info.name;
        if (!info.version.empty()) {
            std::cout << " " << info.version;
        }
        std::cout << " (" << info.format << ")\n";
        if (!info.author.empty()) {
            std::cout << "author: " << info.author << '\n';
        }
        std::cout << "minecraft: " << info.minecraft_version << '\n';
        if (!info.loader.empty()) {
            std::cout << "loader: " << info.loader;
            if (!info.loader_version.empty()) {
                std::cout << " " << info.loader_version;
            }
            std::cout << '\n';
        }
        std::cout << "files: " << info.files.size() << '\n';
        for (const std::string& prefix : info.override_prefixes) {
            std::cout << "overrides: " << prefix << '\n';
        }
        return 0;
    }

    if (subcommand == "install") {
        if (positional.size() != 2) {
            fail("Usage: aurora modpack install <archive> --game-dir DIR [options]");
        }
        const auto game_dir_option = parser.value("--game-dir");
        if (!game_dir_option) {
            fail("modpack install requires an explicit --game-dir for the new "
                 "instance; refusing to install into the default Minecraft "
                 "directory");
        }

        ModpackInstallOptions options;
        options.archive = path_from_utf8(positional[1]);
        options.game_dir = absolute_path(path_from_utf8(*game_dir_option));
        options.force = parser.flag("--force");
        options.install_overrides = !parser.flag("--no-overrides");
        options.install_loader = !parser.flag("--no-loader");
        options.install_game = !parser.flag("--no-game");
        options.backup = !parser.flag("--no-backup");
        if (const auto version_id = parser.value("--version-id")) {
            options.version_id = *version_id;
        }
        if (const auto java_home = parser.value("--java-home")) {
            options.java_home = absolute_path(path_from_utf8(*java_home));
        }
        options.curseforge_api_key = curseforge_api_key_from(parser, config);
        options.download_retries = config.download_retries;
        options.download_mirrors = config.download_mirrors;
        if (const auto retry = parser.value("--retry")) {
            options.download_retries = parse_int(*retry, "--retry");
            if (options.download_retries < 0 || options.download_retries > 32) {
                fail("--retry must be between 0 and 32");
            }
        }
        const auto mirrors = parser.values("--mirror");
        if (!mirrors.empty()) {
            options.download_mirrors = mirrors;
        }

        const ModpackInstallResult result = install_modpack(options);
        const std::string launch_command =
            "aurora launch " + quote_windows_arg(result.version_id) +
            " --game-dir " + quote_windows_arg(path_to_utf8(options.game_dir));
        const std::string loader_command = options.install_loader
                                               ? std::string()
                                               : modpack_loader_command(
                                                     result.info, options.game_dir);
        if (parser.flag("--json")) {
            Json output = modpack_info_json(result.info);
            output["versionId"] = result.version_id;
            output["versionJson"] = path_to_utf8(result.version_json);
            output["instanceDir"] = path_to_utf8(result.instance_dir);
            output["loaderVersionId"] = result.loader_version_id;
            output["upgraded"] = result.upgraded;
            output["backupPath"] = result.backup_path.empty()
                                       ? ""
                                       : path_to_utf8(result.backup_path);
            output["filesInstalled"] = result.files_installed;
            output["filesSkipped"] = result.files_skipped;
            output["filesRemoved"] = result.files_removed;
            output["overridesExtracted"] = result.overrides_extracted;
            output["overridesSkipped"] = result.overrides_skipped;
            output["overridesRemoved"] = result.overrides_removed;
            output["manifestPath"] = path_to_utf8(result.manifest_path);
            output["launchCommand"] = launch_command;
            if (!loader_command.empty()) {
                output["loaderCommand"] = loader_command;
            }
            std::cout << output.dump(2) << '\n';
            return 0;
        }

        std::cout << "pack: " << result.info.name;
        if (!result.info.version.empty()) {
            std::cout << " " << result.info.version;
        }
        std::cout << " (" << result.info.format << ")\n";
        std::cout << "minecraft: " << result.info.minecraft_version << '\n';
        if (!result.info.loader.empty()) {
            std::cout << "loader: " << result.info.loader << " "
                      << result.info.loader_version;
            if (!result.loader_version_id.empty()) {
                std::cout << " (" << result.loader_version_id << ")";
            }
            std::cout << '\n';
        }
        std::cout << "version: " << result.version_id
                  << (result.upgraded ? " (upgraded)" : "") << '\n';
        std::cout << "instance: " << path_to_utf8(result.instance_dir) << '\n';
        std::cout << "files: " << result.files_installed << " installed, "
                  << result.files_skipped << " skipped";
        if (result.files_removed != 0) {
            std::cout << ", " << result.files_removed << " removed";
        }
        std::cout << '\n';
        std::cout << "overrides: " << result.overrides_extracted
                  << " extracted, " << result.overrides_skipped << " skipped";
        if (result.overrides_removed != 0) {
            std::cout << ", " << result.overrides_removed << " removed";
        }
        std::cout << '\n';
        std::cout << "manifest: " << path_to_utf8(result.manifest_path) << '\n';
        if (!result.backup_path.empty()) {
            std::cout << "backup: " << path_to_utf8(result.backup_path) << '\n';
        }
        std::cout << "launch: " << launch_command << '\n';
        if (!loader_command.empty()) {
            std::cout << "loader not installed; run: " << loader_command << '\n';
        }
        return 0;
    }

    fail("Unknown modpack subcommand: " + subcommand);
}

Json config_to_json(const Config& config) {
    Json value = Json::object();
    value["gameDir"] = path_to_utf8(config.game_dir);
    value["javaPath"] = path_to_utf8(config.java_path);
    value["memoryMb"] = config.memory_mb;
    value["width"] = config.width;
    value["height"] = config.height;
    value["username"] = config.username;
    value["microsoftClientId"] = config.microsoft_client_id;
    value["curseForgeApiKey"] =
        config.curseforge_api_key.empty() ? "" : "<set>";
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
            std::cout << "microsoftClientId: " << config.microsoft_client_id << '\n';
            std::cout << "curseForgeApiKey: "
                      << (config.curseforge_api_key.empty() ? "" : "<set>") << '\n';
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
        } else if (key == "microsoftClientId" || key == "authClientId") {
            config.microsoft_client_id = trim(value);
        } else if (key == "curseForgeApiKey") {
            config.curseforge_api_key = trim(value);
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
    if (command == "forge") {
        return command_forge(rest);
    }
    if (command == "mod" || command == "mods") {
        return command_mod(rest);
    }
    if (command == "curseforge" || command == "cf") {
        return command_curseforge(rest);
    }
    if (command == "resourcepack" || command == "resourcepacks" || command == "rp") {
        return command_resourcepack(rest);
    }
    if (command == "modpack" || command == "modpacks" || command == "mrpack") {
        return command_modpack(rest);
    }
    if (command == "auth") {
        return command_auth(rest);
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
