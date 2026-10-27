#pragma once

#include "core/error.hpp"
#include "core/paths.hpp"
#include "core/strings.hpp"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace aurora {

using Json = nlohmann::json;

inline Json parse_json(std::string_view text, std::string_view source) {
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        text.remove_prefix(3);
    }
    try {
        return Json::parse(text.begin(), text.end());
    } catch (const std::exception& e) {
        fail("Invalid JSON in " + std::string(source) + ": " + e.what());
    }
}

inline Json read_json_file(const std::filesystem::path& path) {
    return parse_json(read_text_file(path), path_to_utf8(path));
}

inline std::string json_pretty(const Json& value) {
    return value.dump(2);
}

inline std::optional<std::string> json_optional_string(const Json& object, const char* key) {
    if (!object.is_object()) {
        return std::nullopt;
    }
    const auto it = object.find(key);
    if (it == object.end() || !it->is_string()) {
        return std::nullopt;
    }
    return it->get<std::string>();
}

inline std::string json_string(const Json& object, const char* key, std::string fallback = {}) {
    if (auto value = json_optional_string(object, key)) {
        return *value;
    }
    return fallback;
}

inline std::int64_t json_integer(const Json& object, const char* key, std::int64_t fallback = 0) {
    if (!object.is_object()) {
        return fallback;
    }
    const auto it = object.find(key);
    if (it == object.end() || !it->is_number_integer()) {
        return fallback;
    }
    return it->get<std::int64_t>();
}

inline bool json_boolean(const Json& object, const char* key, bool fallback = false) {
    if (!object.is_object()) {
        return fallback;
    }
    const auto it = object.find(key);
    if (it == object.end() || !it->is_boolean()) {
        return fallback;
    }
    return it->get<bool>();
}

inline const Json* json_object(const Json& object, const char* key) {
    if (!object.is_object()) {
        return nullptr;
    }
    const auto it = object.find(key);
    if (it == object.end() || !it->is_object()) {
        return nullptr;
    }
    return &*it;
}

inline const Json* json_array(const Json& object, const char* key) {
    if (!object.is_object()) {
        return nullptr;
    }
    const auto it = object.find(key);
    if (it == object.end() || !it->is_array()) {
        return nullptr;
    }
    return &*it;
}

} // namespace aurora
