#include "core/mods.hpp"

#include "core/error.hpp"
#include "core/hash.hpp"
#include "core/json.hpp"
#include "core/log.hpp"
#include "core/strings.hpp"
#include "core/zip.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <system_error>
#include <utility>

namespace aurora {

namespace {

constexpr std::string_view kDisabledSuffix = ".disabled";
constexpr std::string_view kJarSuffix = ".jar";

bool has_suffix_ci(std::string_view value, std::string_view suffix) {
    return value.size() >= suffix.size() &&
           iequals(value.substr(value.size() - suffix.size()), suffix);
}

std::string strip_disabled_suffix(std::string name) {
    if (has_suffix_ci(name, kDisabledSuffix)) {
        name.resize(name.size() - kDisabledSuffix.size());
    }
    return name;
}

std::string flatten_text(const Json& value) {
    if (value.is_string()) {
        return value.get<std::string>();
    }
    if (value.is_object()) {
        const std::string text = json_string(value, "text");
        if (!text.empty()) {
            return text;
        }
        if (const auto extra = value.find("extra"); extra != value.end() && extra->is_array()) {
            std::string result;
            for (const Json& part : *extra) {
                result += flatten_text(part);
            }
            return result;
        }
        return {};
    }
    if (value.is_array()) {
        std::string result;
        for (const Json& part : value) {
            const std::string text = flatten_text(part);
            if (text.empty()) {
                continue;
            }
            if (!result.empty()) {
                result += ", ";
            }
            result += text;
        }
        return result;
    }
    return {};
}

std::string json_text_field(const Json& object, const char* key) {
    if (!object.is_object()) {
        return {};
    }
    const auto it = object.find(key);
    return it == object.end() ? std::string() : flatten_text(*it);
}

std::string join_authors(const Json& value) {
    if (value.is_string()) {
        return value.get<std::string>();
    }
    if (!value.is_array()) {
        return {};
    }
    std::string result;
    for (const Json& item : value) {
        std::string name;
        if (item.is_string()) {
            name = item.get<std::string>();
        } else if (item.is_object()) {
            name = json_string(item, "name");
        }
        if (name.empty()) {
            continue;
        }
        if (!result.empty()) {
            result += ", ";
        }
        result += name;
    }
    return result;
}

std::optional<std::string> try_read_entry(const std::filesystem::path& jar,
                                          std::string_view name) {
    try {
        return read_zip_entry(jar, name, 1024 * 1024);
    } catch (const Error& error) {
        log_debug("Cannot read " + std::string(name) + " from " +
                  path_to_utf8(jar) + ": " + error.what());
        return std::nullopt;
    }
}

std::string strip_toml_comment(std::string_view line) {
    bool in_single = false;
    bool in_double = false;
    for (std::size_t index = 0; index < line.size(); ++index) {
        const char character = line[index];
        if (character == '\'' && !in_double) {
            in_single = !in_single;
        } else if (character == '"' && !in_single &&
                   (index == 0 || line[index - 1] != '\\')) {
            in_double = !in_double;
        } else if (character == '#' && !in_single && !in_double) {
            return std::string(line.substr(0, index));
        }
    }
    return std::string(line);
}

std::string unquote_toml(std::string value) {
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

std::vector<std::string> split_lines(std::string_view text) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find('\n', start);
        std::string line(text.substr(
            start, end == std::string_view::npos ? std::string_view::npos : end - start));
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(std::move(line));
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return lines;
}

ModMetadata parse_toml_metadata(std::string_view text, std::string loader) {
    ModMetadata metadata;
    metadata.loader = std::move(loader);
    const std::vector<std::string> lines = split_lines(text);
    std::string section;
    bool in_mods = false;
    bool captured_mod = false;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        std::string line = trim(strip_toml_comment(lines[index]));
        if (line.empty()) {
            continue;
        }
        if (line.rfind("[[", 0) == 0) {
            const std::size_t close = line.find("]]");
            section = close == std::string::npos
                          ? line.substr(2)
                          : line.substr(2, close - 2);
            section = trim(section);
            in_mods = section == "mods";
            if (in_mods && captured_mod) {
                in_mods = false;
            }
            continue;
        }
        if (line.front() == '[') {
            const std::size_t close = line.find(']');
            section = close == std::string::npos
                          ? line.substr(1)
                          : line.substr(1, close - 1);
            section = trim(section);
            in_mods = false;
            continue;
        }
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        const std::string key = trim(line.substr(0, equals));
        std::string value = trim(line.substr(equals + 1));
        if (value.rfind("'''", 0) == 0 || value.rfind("\"\"\"", 0) == 0) {
            const std::string delimiter = value.substr(0, 3);
            std::string content = value.substr(3);
            const std::size_t closing = content.find(delimiter);
            if (closing != std::string::npos) {
                content = content.substr(0, closing);
            } else {
                for (++index; index < lines.size(); ++index) {
                    const std::size_t end = lines[index].find(delimiter);
                    if (end != std::string::npos) {
                        content += "\n" + lines[index].substr(0, end);
                        break;
                    }
                    content += "\n" + lines[index];
                }
            }
            value = trim(content);
        } else {
            value = unquote_toml(value);
        }
        if (section.empty()) {
            // modLoader identifies the loader used by this JAR; the candidate
            // file already distinguishes Forge from NeoForge.
            (void)key;
        } else if (in_mods) {
            if (key == "modId" && metadata.mod_id.empty()) {
                metadata.mod_id = value;
            } else if (key == "version" && metadata.version.empty()) {
                metadata.version = value;
            } else if ((key == "displayName" || key == "name") &&
                       metadata.name.empty()) {
                metadata.name = value;
            } else if ((key == "authors" || key == "authorList") &&
                       metadata.authors.empty()) {
                if (!value.empty() && value.front() == '[') {
                    try {
                        metadata.authors = join_authors(parse_json(value, "mods.toml"));
                    } catch (const Error&) {
                        metadata.authors = value;
                    }
                } else {
                    metadata.authors = value;
                }
            } else if (key == "description" && metadata.description.empty()) {
                metadata.description = value;
            }
            if (!metadata.name.empty() && !metadata.version.empty()) {
                captured_mod = true;
            }
        }
    }
    return metadata;
}

ModMetadata parse_mcmod_info(std::string_view text) {
    ModMetadata metadata;
    metadata.loader = "forge";
    const Json root = parse_json(text, "mcmod.info");
    const Json* entry = nullptr;
    if (root.is_array() && !root.empty()) {
        entry = &root[0];
    } else if (root.is_object()) {
        if (const Json* list = json_array(root, "modList")) {
            if (!list->empty()) {
                entry = &(*list)[0];
            }
        } else if (const Json* lower_list = json_array(root, "modlist")) {
            if (!lower_list->empty()) {
                entry = &(*lower_list)[0];
            }
        } else {
            entry = &root;
        }
    }
    if (entry == nullptr || !entry->is_object()) {
        return metadata;
    }
    metadata.mod_id = json_string(*entry, "modid");
    metadata.name = json_text_field(*entry, "name");
    metadata.version = json_text_field(*entry, "version");
    Json authors = Json();
    if (const auto list = entry->find("authorList"); list != entry->end()) {
        authors = *list;
    } else if (const auto author = entry->find("author"); author != entry->end()) {
        authors = *author;
    }
    metadata.authors = join_authors(authors);
    metadata.description = json_text_field(*entry, "description");
    return metadata;
}

ModMetadata parse_fabric_metadata(std::string_view text, std::string loader) {
    ModMetadata metadata;
    metadata.loader = std::move(loader);
    const Json root = parse_json(text, "fabric.mod.json");
    if (!root.is_object()) {
        return metadata;
    }
    metadata.mod_id = json_string(root, "id");
    metadata.name = json_text_field(root, "name");
    metadata.version = json_string(root, "version");
    metadata.description = json_text_field(root, "description");
    if (const auto authors = root.find("authors"); authors != root.end()) {
        metadata.authors = join_authors(*authors);
    }
    return metadata;
}

ModMetadata parse_quilt_metadata(std::string_view text) {
    ModMetadata metadata;
    metadata.loader = "quilt";
    const Json root = parse_json(text, "quilt.mod.json");
    const Json* loader = json_object(root, "quilt_loader");
    if (loader == nullptr) {
        return metadata;
    }
    metadata.mod_id = json_string(*loader, "id");
    metadata.version = json_string(*loader, "version");
    if (const Json* meta = json_object(*loader, "metadata")) {
        metadata.name = json_text_field(*meta, "name");
        metadata.description = json_text_field(*meta, "description");
        if (const Json* contributors = json_object(*meta, "contributors")) {
            std::string authors;
            for (auto it = contributors->begin(); it != contributors->end(); ++it) {
                if (!authors.empty()) {
                    authors += ", ";
                }
                authors += it.key();
            }
            metadata.authors = std::move(authors);
        }
    }
    return metadata;
}

} // namespace

std::optional<ModMetadata> read_mod_metadata(const std::filesystem::path& jar) {
    if (const auto entry = try_read_entry(jar, "fabric.mod.json")) {
        try {
            ModMetadata metadata = parse_fabric_metadata(*entry, "fabric");
            return metadata;
        } catch (const Error& error) {
            log_debug("Invalid fabric.mod.json in " + path_to_utf8(jar) + ": " +
                      error.what());
        }
    }
    if (const auto entry = try_read_entry(jar, "quilt.mod.json")) {
        try {
            return parse_quilt_metadata(*entry);
        } catch (const Error& error) {
            log_debug("Invalid quilt.mod.json in " + path_to_utf8(jar) + ": " +
                      error.what());
        }
    }
    if (const auto entry = try_read_entry(jar, "META-INF/neoforge.mods.toml")) {
        return parse_toml_metadata(*entry, "neoforge");
    }
    if (const auto entry = try_read_entry(jar, "META-INF/mods.toml")) {
        return parse_toml_metadata(*entry, "forge");
    }
    if (const auto entry = try_read_entry(jar, "mcmod.info")) {
        try {
            return parse_mcmod_info(*entry);
        } catch (const Error& error) {
            log_debug("Invalid mcmod.info in " + path_to_utf8(jar) + ": " +
                      error.what());
        }
    }
    if (const auto entry = try_read_entry(jar, "litemod.json")) {
        try {
            const Json root = parse_json(*entry, "litemod.json");
            ModMetadata metadata;
            metadata.loader = "liteloader";
            metadata.mod_id = json_string(root, "name");
            metadata.name = json_string(root, "name");
            metadata.version = json_string(root, "version");
            metadata.authors = json_string(root, "author");
            metadata.description = json_text_field(root, "description");
            return metadata;
        } catch (const Error& error) {
            log_debug("Invalid litemod.json in " + path_to_utf8(jar) + ": " +
                      error.what());
        }
    }
    return std::nullopt;
}

namespace {

std::optional<std::filesystem::path> find_entry_ci(
    const std::filesystem::path& directory,
    std::string_view name) {
    const std::string lowered = to_lower_ascii(std::string(name));
    std::error_code error;
    std::filesystem::directory_iterator iterator(directory, error);
    if (error) {
        fail("Cannot read mods directory: " + error.message());
    }
    const std::filesystem::directory_iterator end;
    while (iterator != end) {
        std::error_code status_error;
        const auto status = iterator->symlink_status(status_error);
        if (!status_error && std::filesystem::is_regular_file(status) &&
            to_lower_ascii(path_to_utf8(iterator->path().filename())) == lowered) {
            return iterator->path();
        }
        iterator.increment(error);
        if (error) {
            fail("Cannot read mods directory: " + error.message());
        }
    }
    return std::nullopt;
}

} // namespace

bool is_safe_mod_filename(std::string_view filename) {
    if (filename.empty() || filename.size() > 200) {
        return false;
    }
    if (filename.front() == '.' || filename.front() == ' ') {
        return false;
    }
    if (filename.back() == '.' || filename.back() == ' ') {
        return false;
    }
    for (const unsigned char character : filename) {
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

    const std::string lower = to_lower_ascii(std::string(filename));
    if (lower.size() < 5 || lower.compare(lower.size() - 4, 4, kJarSuffix) != 0) {
        return false;
    }
    const std::string stem = lower.substr(0, lower.find('.'));
    if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul") {
        return false;
    }
    if (stem.size() == 4 &&
        (stem.compare(0, 3, "com") == 0 || stem.compare(0, 3, "lpt") == 0) &&
        stem[3] >= '1' && stem[3] <= '9') {
        return false;
    }
    return true;
}

std::filesystem::path mods_directory(const std::filesystem::path& game_dir) {
    return game_dir / "mods";
}

std::vector<InstalledMod> scan_mods(const std::filesystem::path& game_dir,
                                    bool compute_sha1,
                                    bool read_metadata) {
    const std::filesystem::path directory = mods_directory(game_dir);
    std::error_code error;
    const auto directory_status = std::filesystem::status(directory, error);
    if (error) {
        if (error == std::errc::no_such_file_or_directory) {
            return {};
        }
        fail("Cannot read mods directory " + path_to_utf8(directory) + ": " +
             error.message());
    }
    if (!std::filesystem::is_directory(directory_status)) {
        return {};
    }

    std::vector<InstalledMod> mods;
    std::filesystem::directory_iterator iterator(directory, error);
    if (error) {
        fail("Cannot read mods directory: " + error.message());
    }
    const std::filesystem::directory_iterator end;
    while (iterator != end) {
        std::error_code status_error;
        const auto status = iterator->symlink_status(status_error);
        if (!status_error && std::filesystem::is_regular_file(status)) {
            const std::string filename = path_to_utf8(iterator->path().filename());
            std::string name = strip_disabled_suffix(filename);
            if (has_suffix_ci(name, kJarSuffix)) {
                InstalledMod mod;
                mod.path = iterator->path();
                mod.filename = filename;
                mod.name = std::move(name);
                mod.enabled = !has_suffix_ci(filename, kDisabledSuffix);
                std::error_code size_error;
                const auto size = iterator->file_size(size_error);
                mod.size = size_error ? 0 : size;
                if (compute_sha1) {
                    try {
                        mod.sha1 = sha1_file(mod.path);
                    } catch (const Error& hash_error) {
                        log_warn("Cannot hash " + filename + ": " +
                                 std::string(hash_error.what()));
                    }
                }
                if (read_metadata) {
                    if (auto metadata = read_mod_metadata(mod.path)) {
                        mod.has_metadata = true;
                        mod.metadata = std::move(*metadata);
                    }
                }
                mods.push_back(std::move(mod));
            }
        }
        iterator.increment(error);
        if (error) {
            fail("Cannot read mods directory: " + error.message());
        }
    }

    std::sort(mods.begin(), mods.end(), [](const InstalledMod& left,
                                           const InstalledMod& right) {
        const std::string left_key = to_lower_ascii(left.filename);
        const std::string right_key = to_lower_ascii(right.filename);
        if (left_key != right_key) {
            return left_key < right_key;
        }
        return left.filename < right.filename;
    });
    return mods;
}

ModToggleResult set_mod_enabled(const std::filesystem::path& game_dir,
                                std::string_view filename,
                                bool enabled) {
    const std::string input = trim(filename);
    if (input.empty()) {
        fail("Mod file name is required");
    }
    const std::string base = strip_disabled_suffix(input);
    if (!is_safe_mod_filename(base)) {
        fail("Invalid mod file name: " + input);
    }

    const std::filesystem::path directory = mods_directory(game_dir);
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error)) {
        fail("Mods directory does not exist: " + path_to_utf8(directory));
    }

    const std::string desired_name =
        enabled ? base : base + std::string(kDisabledSuffix);
    const std::string other_name =
        enabled ? base + std::string(kDisabledSuffix) : base;
    const auto desired = find_entry_ci(directory, desired_name);
    const auto other = find_entry_ci(directory, other_name);
    if (desired && other) {
        fail("Both " + path_to_utf8(desired->filename()) + " and " +
             path_to_utf8(other->filename()) + " exist in " +
             path_to_utf8(directory) + "; resolve the duplicate first");
    }
    if (desired) {
        return ModToggleResult{*desired, path_to_utf8(desired->filename()), enabled, false};
    }
    if (!other) {
        fail("Mod file not found in " + path_to_utf8(directory) + ": " + base);
    }

    const std::filesystem::path target = directory / path_from_utf8(desired_name);
    std::error_code rename_error;
    std::filesystem::rename(*other, target, rename_error);
    if (rename_error) {
        fail("Cannot rename " + path_to_utf8(*other) + ": " + rename_error.message());
    }
    return ModToggleResult{target, desired_name, enabled, true};
}

} // namespace aurora
