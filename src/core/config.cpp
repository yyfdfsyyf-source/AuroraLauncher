#include "core/config.hpp"

#include "core/json.hpp"
#include "core/paths.hpp"
#include "core/strings.hpp"

#include <algorithm>

namespace aurora {

namespace {

std::vector<std::string> string_array(const Json& object, const char* key) {
    std::vector<std::string> result;
    if (const Json* array = json_array(object, key)) {
        for (const Json& item : *array) {
            if (item.is_string()) {
                result.push_back(item.get<std::string>());
            }
        }
    }
    return result;
}

Json to_json_array(const std::vector<std::string>& values) {
    Json array = Json::array();
    for (const std::string& value : values) {
        array.push_back(value);
    }
    return array;
}

} // namespace

Config Config::defaults() {
    Config config;
    config.game_dir = default_game_dir();
    config.username = environment_value("USERNAME");
    if (config.username.empty()) {
        config.username = "Player";
    }
    return config;
}

Config Config::load(const std::filesystem::path& path) {
    Config config = defaults();
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) {
        return config;
    }

    const Json value = read_json_file(path);
    if (const auto game_dir = json_optional_string(value, "gameDir"); game_dir && !game_dir->empty()) {
        config.game_dir = absolute_path(path_from_utf8(*game_dir));
    }
    if (const auto java_path = json_optional_string(value, "javaPath"); java_path && !java_path->empty()) {
        config.java_path = absolute_path(path_from_utf8(*java_path));
    }
    const auto memory = json_integer(value, "memoryMb");
    if (memory >= 512 && memory <= 65536) {
        config.memory_mb = static_cast<int>(memory);
    }
    const auto width = json_integer(value, "width");
    const auto height = json_integer(value, "height");
    if (width > 0 && width <= 16384 && height > 0 && height <= 16384) {
        config.width = static_cast<int>(width);
        config.height = static_cast<int>(height);
    }
    if (const auto username = json_optional_string(value, "username"); username && !username->empty()) {
        config.username = *username;
    }
    if (const auto client_id = json_optional_string(value, "microsoftClientId");
        client_id && !client_id->empty()) {
        config.microsoft_client_id = trim(*client_id);
    } else if (const auto legacy = json_optional_string(value, "authClientId");
               legacy && !legacy->empty()) {
        config.microsoft_client_id = trim(*legacy);
    }
    if (const auto key = json_optional_string(value, "curseForgeApiKey");
        key && !key->empty()) {
        config.curseforge_api_key = trim(*key);
    }
    config.jvm_args = string_array(value, "jvmArgs");
    config.game_args = string_array(value, "gameArgs");
    const auto download_retries = json_integer(value, "downloadRetries");
    if (download_retries >= 0 && download_retries <= 32) {
        config.download_retries = static_cast<int>(download_retries);
    }
    config.download_mirrors = string_array(value, "downloadMirrors");
    config.demo = json_boolean(value, "demo");
    return config;
}

void Config::save(const std::filesystem::path& path) const {
    Json value = Json::object();
    value["gameDir"] = path_to_utf8(game_dir);
    value["javaPath"] = path_to_utf8(java_path);
    value["memoryMb"] = memory_mb;
    value["width"] = width;
    value["height"] = height;
    value["username"] = username;
    value["microsoftClientId"] = microsoft_client_id;
    value["curseForgeApiKey"] = curseforge_api_key;
    value["jvmArgs"] = to_json_array(jvm_args);
    value["gameArgs"] = to_json_array(game_args);
    value["downloadRetries"] = download_retries;
    value["downloadMirrors"] = to_json_array(download_mirrors);
    value["demo"] = demo;
    write_text_file(path, json_pretty(value) + "\n");
}

} // namespace aurora
