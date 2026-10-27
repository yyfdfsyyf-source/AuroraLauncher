#pragma once

#include "core/json.hpp"
#include "core/rules.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace aurora {

struct DownloadInfo {
    std::string id;
    std::string path;
    std::string url;
    std::string sha1;
    std::uint64_t size = 0;

    bool has_url() const { return !url.empty(); }
};

struct AssetIndex {
    std::string id;
    std::string sha1;
    std::uint64_t size = 0;
    std::uint64_t total_size = 0;
    std::string url;

    bool has_url() const { return !url.empty(); }
};

struct Library {
    std::string name;
    std::optional<DownloadInfo> artifact;
    std::map<std::string, DownloadInfo> classifiers;
    std::map<std::string, std::string> natives;
    std::vector<Rule> rules;
    std::vector<std::string> extract_exclude;

    bool allowed(const Environment& environment) const;
    std::optional<std::string> native_classifier(const Environment& environment) const;
    std::optional<DownloadInfo> native_download(const Environment& environment) const;
    std::string relative_path() const;
    std::string native_relative_path(const Environment& environment) const;
};

struct Argument {
    std::vector<Rule> rules;
    std::vector<std::string> values;

    bool allowed(const Environment& environment) const;
};

struct LoggingFile {
    std::string id;
    std::string url;
    std::string sha1;
    std::uint64_t size = 0;

    bool has_url() const { return !url.empty(); }
};

struct LoggingClient {
    std::string argument;
    std::optional<LoggingFile> file;
    std::vector<Rule> rules;

    bool allowed(const Environment& environment) const;
};

struct VersionMeta {
    std::string id;
    std::string type;
    std::string main_class;
    std::string assets;
    std::string inherits_from;
    std::string jar;
    std::optional<int> java_major;
    std::optional<DownloadInfo> client;
    std::optional<AssetIndex> asset_index;
    std::optional<LoggingClient> logging_client;
    std::vector<Library> libraries;
    std::vector<Argument> game_arguments;
    std::vector<Argument> jvm_arguments;
    std::optional<std::string> minecraft_arguments;

    static VersionMeta parse(const Json& value, std::string id = {});

    std::filesystem::path version_directory(const std::filesystem::path& game_dir) const;
    std::filesystem::path client_jar(const std::filesystem::path& game_dir) const;
    std::filesystem::path asset_index_file(const std::filesystem::path& game_dir) const;
    std::filesystem::path logging_config_file(const std::filesystem::path& game_dir) const;
    std::filesystem::path natives_directory(const std::filesystem::path& game_dir) const;
    std::string client_jar_name() const;
};

VersionMeta merge_versions(const VersionMeta& parent, const VersionMeta& child);

DownloadInfo parse_download_info(const Json& value);
AssetIndex parse_asset_index(const Json& value);

} // namespace aurora
