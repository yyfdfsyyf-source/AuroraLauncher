#include "core/pcl.hpp"

#include "core/error.hpp"
#include "core/json.hpp"
#include "core/paths.hpp"
#include "core/strings.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>
#include <system_error>

namespace aurora {

namespace {

bool is_space(unsigned char character) {
    return std::isspace(character) != 0;
}

std::string strip_bom(std::string text) {
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        text.erase(0, 3);
    }
    return text;
}

std::string strip_inline_comment(std::string_view value) {
    bool in_single = false;
    bool in_double = false;
    for (std::size_t index = 0; index < value.size(); ++index) {
        const char character = value[index];
        if (character == '\'' && !in_double) {
            in_single = !in_single;
        } else if (character == '"' && !in_single) {
            in_double = !in_double;
        } else if (character == '#' && !in_single && !in_double &&
                   (index == 0 || is_space(static_cast<unsigned char>(value[index - 1])))) {
            return std::string(value.substr(0, index));
        }
    }
    return std::string(value);
}

std::string unquote(std::string value) {
    value = trim(value);
    if (value.size() >= 2) {
        const char first = value.front();
        const char last = value.back();
        if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
            value = value.substr(1, value.size() - 2);
        }
    }
    return trim(value);
}

std::optional<std::pair<std::string, std::string>> parse_line(std::string_view raw) {
    const std::string line = trim(raw);
    if (line.empty() || line.front() == '#' || line.rfind("//", 0) == 0 ||
        line.front() == '[' || line.front() == '-') {
        return std::nullopt;
    }
    std::size_t separator = line.find(':');
    const std::size_t equals = line.find('=');
    if (separator == std::string_view::npos ||
        (equals != std::string_view::npos && equals < separator)) {
        separator = equals;
    }
    if (separator == std::string_view::npos) {
        return std::nullopt;
    }
    std::string key = trim(line.substr(0, separator));
    if (key.empty()) {
        return std::nullopt;
    }
    std::string value = trim(line.substr(separator + 1));
    value = trim(strip_inline_comment(value));
    return std::pair<std::string, std::string>{std::move(key), unquote(std::move(value))};
}

bool parse_bool(std::string_view value, bool& result) {
    const std::string lower = to_lower_ascii(trim(value));
    if (lower == "true" || lower == "1" || lower == "yes" || lower == "on") {
        result = true;
        return true;
    }
    if (lower == "false" || lower == "0" || lower == "no" || lower == "off") {
        result = false;
        return true;
    }
    return false;
}

bool parse_int(std::string_view value, int& result) {
    try {
        std::size_t consumed = 0;
        const int parsed = std::stoi(trim(value), &consumed);
        if (consumed != trim(value).size()) {
            return false;
        }
        result = parsed;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

int pcl_memory_tick_to_mb(int tick) {
    if (tick <= 0) {
        return 0;
    }
    double gigabytes = 0.0;
    if (tick <= 12) {
        gigabytes = tick * 0.1 + 0.3;
    } else if (tick <= 25) {
        gigabytes = (tick - 12) * 0.5 + 1.5;
    } else if (tick <= 33) {
        gigabytes = (tick - 25) + 8.0;
    } else {
        gigabytes = (tick - 33) * 2.0 + 16.0;
    }
    return static_cast<int>(std::lround(gigabytes * 1024.0));
}

bool directory_has_file(const std::filesystem::path& directory) {
    std::error_code error;
    const auto status = std::filesystem::status(directory, error);
    if (error || !std::filesystem::is_directory(status)) {
        return false;
    }
    std::filesystem::directory_iterator iterator(directory, error);
    if (error) {
        return false;
    }
    const std::filesystem::directory_iterator end;
    while (iterator != end) {
        std::error_code status_error;
        if (iterator->is_regular_file(status_error) && !status_error) {
            return true;
        }
        iterator.increment(error);
        if (error) {
            return false;
        }
    }
    return false;
}

bool directory_has_subdirectory(const std::filesystem::path& directory) {
    std::error_code error;
    const auto status = std::filesystem::status(directory, error);
    if (error || !std::filesystem::is_directory(status)) {
        return false;
    }
    std::filesystem::directory_iterator iterator(directory, error);
    if (error) {
        return false;
    }
    const std::filesystem::directory_iterator end;
    while (iterator != end) {
        std::error_code status_error;
        if (iterator->is_directory(status_error) && !status_error) {
            return true;
        }
        iterator.increment(error);
        if (error) {
            return false;
        }
    }
    return false;
}

std::string selected_java_path(std::string_view value,
                               const std::filesystem::path& version_dir) {
    const std::string selected = trim(value);
    if (selected.empty() || selected == "使用全局设置") {
        return {};
    }
    if (selected.front() != '{') {
        return selected;
    }
    const Json preference = Json::parse(selected, nullptr, false);
    if (preference.is_discarded() || !preference.is_object()) {
        return {};
    }
    const std::string kind = json_string(preference, "kind");
    if (kind == "exist") {
        return json_string(preference, "JavaExePath");
    }
    if (kind == "relative") {
        const std::string relative = json_string(preference, "RelativePath");
        if (relative.empty()) {
            return {};
        }
        const auto game_dir = version_dir.parent_path().parent_path();
        for (const auto& base : {game_dir, game_dir.parent_path(), version_dir}) {
            std::error_code error;
            const auto candidate = base / path_from_utf8(relative);
            if (std::filesystem::is_regular_file(candidate, error) && !error) {
                return path_to_utf8(candidate);
            }
        }
    }
    return {};
}

} // namespace

std::vector<std::string> split_pcl_arguments(std::string_view value) {
    std::vector<std::string> result;
    std::string token;
    bool in_single = false;
    bool in_double = false;
    bool escaped = false;
    for (const char character : value) {
        if (escaped) {
            token.push_back(character);
            escaped = false;
            continue;
        }
        if (character == '\\' && in_double) {
            escaped = true;
            continue;
        }
        if (character == '"' && !in_single) {
            in_double = !in_double;
            continue;
        }
        if (character == '\'' && !in_double) {
            in_single = !in_single;
            continue;
        }
        if (is_space(static_cast<unsigned char>(character)) && !in_single &&
            !in_double) {
            if (!token.empty()) {
                result.push_back(std::move(token));
                token.clear();
            }
            continue;
        }
        token.push_back(character);
    }
    if (!token.empty()) {
        result.push_back(std::move(token));
    }
    return result;
}

PclInstanceSettings read_pcl_instance_settings(
    const std::filesystem::path& version_dir) {
    PclInstanceSettings settings;
    if (version_dir.empty()) {
        return settings;
    }

    std::filesystem::path config_path;
    for (const char* candidate : {"config.v1.yml", "Setup.ini"}) {
        const std::filesystem::path path = version_dir / "PCL" / candidate;
        std::error_code error;
        if (std::filesystem::is_regular_file(path, error) && !error) {
            config_path = path;
            break;
        }
    }

    std::optional<bool> explicit_isolation;
    std::optional<int> memory_type;
    int memory_tick = 0;
    bool memory_tick_configured = false;
    if (!config_path.empty()) {
        settings.detected = true;
        settings.config_path = config_path;
        std::string text;
        try {
            text = strip_bom(read_text_file(config_path));
        } catch (const Error&) {
            text.clear();
        }
        std::size_t start = 0;
        while (start <= text.size()) {
            const std::size_t end = text.find('\n', start);
            const std::string_view line(
                text.data() + start,
                (end == std::string::npos ? text.size() : end) - start);
            if (const auto entry = parse_line(line)) {
                const std::string& key = entry->first;
                const std::string& value = entry->second;
                if (key == "VersionOriginal") {
                    if (settings.minecraft_version.empty()) {
                        settings.minecraft_version = trim(value);
                    }
                } else if (key == "VersionArgumentIndieV2") {
                    bool parsed = false;
                    if (parse_bool(value, parsed)) {
                        explicit_isolation = parsed;
                    }
                } else if (key == "VersionArgumentIndie") {
                    int legacy = 0;
                    if (parse_int(value, legacy)) {
                        if (!explicit_isolation.has_value()) {
                            if (legacy == 1) {
                                explicit_isolation = true;
                            } else if (legacy == 2) {
                                explicit_isolation = false;
                            }
                        }
                    }
                } else if (key == "VersionRamType") {
                    int parsed = 0;
                    if (parse_int(value, parsed)) {
                        memory_type = parsed;
                    }
                } else if (key == "VersionRamCustom") {
                    int parsed = 0;
                    if (parse_int(value, parsed)) {
                        memory_tick = parsed;
                        memory_tick_configured = true;
                    }
                } else if (key == "VersionAdvanceJvm") {
                    settings.jvm_args = split_pcl_arguments(value);
                } else if (key == "VersionAdvanceGame") {
                    settings.game_args = split_pcl_arguments(value);
                } else if (key == "VersionArgumentJavaSelect") {
                    const std::string selected = selected_java_path(value, version_dir);
                    if (!selected.empty()) {
                        settings.java_path = selected;
                    }
                }
            }
            if (end == std::string::npos) {
                break;
            }
            start = end + 1;
        }
    }

    const bool mods_present = directory_has_file(version_dir / "mods");
    const bool saves_present = directory_has_subdirectory(version_dir / "saves");
    settings.auto_isolated = mods_present || saves_present;
    settings.isolated =
        explicit_isolation.value_or(settings.auto_isolated);

    if (memory_type.has_value() && *memory_type == 1) {
        // PCL 未写入 VersionRamCustom 时按默认档位 15 计算
        const int tick = memory_tick_configured ? memory_tick : 15;
        if (tick > 0) {
            settings.memory_mb = pcl_memory_tick_to_mb(tick);
        }
    }
    return settings;
}

bool pcl_instance_is_isolated(const std::filesystem::path& version_dir) {
    return read_pcl_instance_settings(version_dir).isolated;
}

} // namespace aurora
