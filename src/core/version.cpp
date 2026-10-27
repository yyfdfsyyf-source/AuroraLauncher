#include "core/version.hpp"

#include "core/error.hpp"
#include "core/maven.hpp"
#include "core/paths.hpp"
#include "core/strings.hpp"

#include <algorithm>
#include <map>

namespace aurora {

namespace {

Argument parse_argument(const Json& value) {
    Argument argument;
    if (value.is_string()) {
        argument.values.push_back(value.get<std::string>());
        return argument;
    }
    if (!value.is_object()) {
        fail("Version argument must be a string or an object");
    }
    if (const Json* rules = json_array(value, "rules")) {
        for (const Json& rule : *rules) {
            argument.rules.push_back(parse_rule(rule));
        }
    }
    const auto it = value.find("value");
    if (it != value.end()) {
        if (it->is_string()) {
            argument.values.push_back(it->get<std::string>());
        } else if (it->is_array()) {
            for (const Json& item : *it) {
                if (!item.is_string()) {
                    fail("Version argument value array must contain strings");
                }
                argument.values.push_back(item.get<std::string>());
            }
        } else {
            fail("Version argument value must be a string or an array");
        }
    }
    return argument;
}

std::vector<Argument> parse_arguments(const Json& value, const char* key) {
    std::vector<Argument> result;
    if (const Json* arguments = json_array(value, key)) {
        for (const Json& item : *arguments) {
            result.push_back(parse_argument(item));
        }
    }
    return result;
}

std::string classifier_path(const std::string& name, const std::string& classifier) {
    try {
        MavenCoordinate coordinate = MavenCoordinate::parse(name);
        coordinate.classifier = classifier;
        return coordinate.path();
    } catch (const Error&) {
        return {};
    }
}

} // namespace

bool Library::allowed(const Environment& environment) const {
    return rules_allow(rules, environment);
}

std::optional<std::string> Library::native_classifier(const Environment& environment) const {
    const auto it = natives.find(environment.os_name);
    if (it == natives.end() || it->second.empty()) {
        return std::nullopt;
    }
    std::string classifier = it->second;
    classifier = replace_all(classifier, "${arch}", architecture_bits(environment.arch));
    return classifier;
}

std::optional<DownloadInfo> Library::native_download(const Environment& environment) const {
    const auto classifier = native_classifier(environment);
    if (!classifier) {
        return std::nullopt;
    }
    const auto it = classifiers.find(*classifier);
    if (it != classifiers.end()) {
        return it->second;
    }
    DownloadInfo fallback;
    fallback.path = classifier_path(name, *classifier);
    if (fallback.path.empty()) {
        return std::nullopt;
    }
    return fallback;
}

std::string Library::relative_path() const {
    if (artifact && !artifact->path.empty()) {
        return artifact->path;
    }
    return maven_path(name);
}

std::string Library::native_relative_path(const Environment& environment) const {
    const auto classifier = native_classifier(environment);
    if (!classifier) {
        return {};
    }
    const auto it = classifiers.find(*classifier);
    if (it != classifiers.end() && !it->second.path.empty()) {
        return it->second.path;
    }
    return classifier_path(name, *classifier);
}

bool Argument::allowed(const Environment& environment) const {
    return rules_allow(rules, environment);
}

bool LoggingClient::allowed(const Environment& environment) const {
    return rules_allow(rules, environment);
}

DownloadInfo parse_download_info(const Json& value) {
    if (!value.is_object()) {
        fail("Download information must be an object");
    }
    DownloadInfo result;
    result.id = json_string(value, "id");
    result.path = json_string(value, "path");
    result.url = json_string(value, "url");
    result.sha1 = to_lower_ascii(json_string(value, "sha1"));
    const auto size = json_integer(value, "size");
    if (size > 0) {
        result.size = static_cast<std::uint64_t>(size);
    }
    return result;
}

AssetIndex parse_asset_index(const Json& value) {
    if (!value.is_object()) {
        fail("Asset index must be an object");
    }
    AssetIndex result;
    result.id = json_string(value, "id");
    result.sha1 = to_lower_ascii(json_string(value, "sha1"));
    const auto size = json_integer(value, "size");
    if (size > 0) {
        result.size = static_cast<std::uint64_t>(size);
    }
    const auto total_size = json_integer(value, "totalSize");
    if (total_size > 0) {
        result.total_size = static_cast<std::uint64_t>(total_size);
    }
    result.url = json_string(value, "url");
    return result;
}

VersionMeta VersionMeta::parse(const Json& value, std::string requested_id) {
    if (!value.is_object()) {
        fail("Version JSON must be an object");
    }

    VersionMeta result;
    result.id = requested_id.empty() ? json_string(value, "id") : std::move(requested_id);
    result.type = json_string(value, "type");
    result.main_class = json_string(value, "mainClass");
    result.assets = json_string(value, "assets");
    result.inherits_from = json_string(value, "inheritsFrom");
    result.jar = json_string(value, "jar");

    if (const Json* java_version = json_object(value, "javaVersion")) {
        const auto major = json_integer(*java_version, "majorVersion");
        if (major > 0) {
            result.java_major = static_cast<int>(major);
        }
    }

    if (const Json* downloads = json_object(value, "downloads")) {
        if (const auto it = downloads->find("client"); it != downloads->end() && it->is_object()) {
            result.client = parse_download_info(*it);
        }
    }

    if (const auto it = value.find("assetIndex"); it != value.end() && it->is_object()) {
        result.asset_index = parse_asset_index(*it);
    }

    if (const Json* logging = json_object(value, "logging")) {
        if (const Json* client = json_object(*logging, "client")) {
            LoggingClient logging_client;
            logging_client.argument = json_string(*client, "argument");
            if (const Json* file = json_object(*client, "file")) {
                LoggingFile logging_file;
                logging_file.id = json_string(*file, "id");
                logging_file.url = json_string(*file, "url");
                logging_file.sha1 = to_lower_ascii(json_string(*file, "sha1"));
                const auto size = json_integer(*file, "size");
                if (size > 0) {
                    logging_file.size = static_cast<std::uint64_t>(size);
                }
                logging_client.file = std::move(logging_file);
            }
            if (const Json* rules = json_array(*client, "rules")) {
                for (const Json& rule : *rules) {
                    logging_client.rules.push_back(parse_rule(rule));
                }
            }
            if (!logging_client.argument.empty() || logging_client.file ||
                !logging_client.rules.empty()) {
                result.logging_client = std::move(logging_client);
            }
        }
    }

    if (const Json* libraries = json_array(value, "libraries")) {
        for (const Json& item : *libraries) {
            if (!item.is_object()) {
                fail("Library entry must be an object");
            }
            Library library;
            library.name = json_string(item, "name");
            if (library.name.empty()) {
                fail("Library entry is missing a name");
            }
            if (const Json* downloads = json_object(item, "downloads")) {
                if (const auto it = downloads->find("artifact");
                    it != downloads->end() && it->is_object()) {
                    library.artifact = parse_download_info(*it);
                }
                if (const auto classifiers = json_object(*downloads, "classifiers")) {
                    for (auto it = classifiers->begin(); it != classifiers->end(); ++it) {
                        if (it.value().is_object()) {
                            library.classifiers[it.key()] = parse_download_info(it.value());
                        }
                    }
                }
            }
            if (const Json* natives = json_object(item, "natives")) {
                for (auto it = natives->begin(); it != natives->end(); ++it) {
                    if (it.value().is_string()) {
                        library.natives[it.key()] = it.value().get<std::string>();
                    }
                }
            }
            if (const Json* rules = json_array(item, "rules")) {
                for (const Json& rule : *rules) {
                    library.rules.push_back(parse_rule(rule));
                }
            }
            if (const Json* extract = json_object(item, "extract")) {
                if (const Json* exclude = json_array(*extract, "exclude")) {
                    for (const Json& entry : *exclude) {
                        if (entry.is_string()) {
                            library.extract_exclude.push_back(entry.get<std::string>());
                        }
                    }
                }
            }
            result.libraries.push_back(std::move(library));
        }
    }

    if (const Json* arguments = json_object(value, "arguments")) {
        result.game_arguments = parse_arguments(*arguments, "game");
        result.jvm_arguments = parse_arguments(*arguments, "jvm");
    }

    if (const auto arguments = json_optional_string(value, "minecraftArguments")) {
        result.minecraft_arguments = *arguments;
    }

    return result;
}

std::filesystem::path VersionMeta::version_directory(
    const std::filesystem::path& game_dir) const {
    return game_dir / "versions" / checked_relative_path(id, "version id");
}

std::string VersionMeta::client_jar_name() const {
    return (jar.empty() ? id : jar) + ".jar";
}

std::filesystem::path VersionMeta::client_jar(const std::filesystem::path& game_dir) const {
    return version_directory(game_dir) /
           checked_relative_path(client_jar_name(), "client jar");
}

std::filesystem::path VersionMeta::asset_index_file(
    const std::filesystem::path& game_dir) const {
    const std::string index_id =
        asset_index && !asset_index->id.empty() ? asset_index->id
                                                : (assets.empty() ? "legacy" : assets);
    return game_dir / "assets" / "indexes" /
           checked_relative_path(index_id + ".json", "asset index");
}

std::filesystem::path VersionMeta::logging_config_file(
    const std::filesystem::path& game_dir) const {
    if (!logging_client || !logging_client->file || logging_client->file->id.empty()) {
        return {};
    }
    return game_dir / "assets" / "log_configs" /
           checked_relative_path(logging_client->file->id, "logging configuration");
}

std::filesystem::path VersionMeta::natives_directory(
    const std::filesystem::path& game_dir) const {
    return version_directory(game_dir) / "natives";
}

VersionMeta merge_versions(const VersionMeta& parent, const VersionMeta& child) {
    VersionMeta result = parent;
    if (!child.id.empty()) {
        result.id = child.id;
    }
    if (!child.type.empty()) {
        result.type = child.type;
    }
    if (!child.main_class.empty()) {
        result.main_class = child.main_class;
    }
    if (!child.assets.empty()) {
        result.assets = child.assets;
    }
    if (!child.inherits_from.empty()) {
        result.inherits_from = child.inherits_from;
    }
    if (!child.jar.empty()) {
        result.jar = child.jar;
    }
    if (child.java_major) {
        result.java_major = child.java_major;
    }
    if (child.client) {
        result.client = child.client;
    }
    if (child.asset_index) {
        result.asset_index = child.asset_index;
    }
    if (child.logging_client) {
        if (!result.logging_client) {
            result.logging_client.emplace();
        }
        if (!child.logging_client->argument.empty()) {
            result.logging_client->argument = child.logging_client->argument;
        }
        if (child.logging_client->file) {
            result.logging_client->file = child.logging_client->file;
        }
        if (!child.logging_client->rules.empty()) {
            result.logging_client->rules = child.logging_client->rules;
        }
    }
    if (child.minecraft_arguments) {
        result.minecraft_arguments = child.minecraft_arguments;
    }

    std::map<std::string, std::size_t> positions;
    for (std::size_t i = 0; i < result.libraries.size(); ++i) {
        positions[result.libraries[i].name] = i;
    }
    for (const Library& library : child.libraries) {
        const auto it = positions.find(library.name);
        if (it == positions.end()) {
            positions[library.name] = result.libraries.size();
            result.libraries.push_back(library);
        } else {
            result.libraries[it->second] = library;
        }
    }

    result.game_arguments.insert(
        result.game_arguments.end(), child.game_arguments.begin(), child.game_arguments.end());
    result.jvm_arguments.insert(
        result.jvm_arguments.end(), child.jvm_arguments.begin(), child.jvm_arguments.end());
    return result;
}

} // namespace aurora
