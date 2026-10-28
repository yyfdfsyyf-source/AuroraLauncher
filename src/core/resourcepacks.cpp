#include "core/resourcepacks.hpp"

#include "core/error.hpp"
#include "core/json.hpp"
#include "core/log.hpp"
#include "core/paths.hpp"
#include "core/strings.hpp"
#include "core/zip.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <optional>
#include <system_error>
#include <utility>

namespace aurora {

namespace {

constexpr std::string_view kPacksKey = "resourcePacks:";
constexpr std::string_view kIncompatibleKey = "incompatibleResourcePacks:";

using PackFormat = std::pair<int, int>;

bool has_suffix_ci(std::string_view value, std::string_view suffix) {
    return value.size() >= suffix.size() &&
           iequals(value.substr(value.size() - suffix.size()), suffix);
}

bool is_safe_resource_pack_name(std::string_view name) {
    if (name.empty() || name.size() > 200 || name == "." || name == "..") {
        return false;
    }
    if (name.front() == ' ' || name.back() == ' ' || name.back() == '.') {
        return false;
    }
    for (const unsigned char character : name) {
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
    return true;
}

bool parse_positive_int(std::string_view raw, int& value) {
    const std::string text = trim(raw);
    if (text.empty() || !std::all_of(text.begin(), text.end(), [](unsigned char c) {
            return std::isdigit(c) != 0;
        })) {
        return false;
    }
    try {
        value = std::stoi(std::string(text));
    } catch (const std::exception&) {
        return false;
    }
    return value >= 0;
}

// Parses 1.21.4-style releases. Returns false for snapshots and unknown ids.
bool parse_release_version(std::string_view version, int& minor, int& patch) {
    if (version.size() < 3 || version[0] != '1' || version[1] != '.') {
        return false;
    }
    std::size_t index = 2;
    int value = 0;
    bool any = false;
    while (index < version.size() && std::isdigit(static_cast<unsigned char>(version[index]))) {
        value = value * 10 + (version[index] - '0');
        ++index;
        any = true;
    }
    if (!any) {
        return false;
    }
    minor = value;
    patch = 0;
    if (index < version.size() && version[index] == '.') {
        ++index;
        int patch_value = 0;
        bool patch_any = false;
        while (index < version.size() &&
               std::isdigit(static_cast<unsigned char>(version[index]))) {
            patch_value = patch_value * 10 + (version[index] - '0');
            ++index;
            patch_any = true;
        }
        if (patch_any) {
            patch = patch_value;
        }
    }
    if (index < version.size()) {
        const char next = version[index];
        if (next != '-' && next != '+' && next != ' ') {
            return false;
        }
    }
    return true;
}

// Parses snapshots such as 17w43a or 25w31a.
bool parse_snapshot_version(std::string_view version, int& year, int& week) {
    std::size_t index = 0;
    int year_value = 0;
    while (index < version.size() &&
           std::isdigit(static_cast<unsigned char>(version[index]))) {
        year_value = year_value * 10 + (version[index] - '0');
        ++index;
    }
    if (index == 0 || index >= version.size() || version[index] != 'w') {
        return false;
    }
    ++index;
    int week_value = 0;
    bool any = false;
    while (index < version.size() &&
           std::isdigit(static_cast<unsigned char>(version[index]))) {
        week_value = week_value * 10 + (version[index] - '0');
        ++index;
        any = true;
    }
    if (!any) {
        return false;
    }
    year = year_value;
    week = week_value;
    return true;
}

std::optional<PackFormat> parse_pack_version_value(const Json& value, bool any_minor) {
    if (value.is_number_integer()) {
        return PackFormat{value.get<int>(),
                          any_minor ? std::numeric_limits<int>::max() : 0};
    }
    if (!value.is_array()) {
        return std::nullopt;
    }
    if (value.size() == 1 && value[0].is_number_integer()) {
        return PackFormat{value[0].get<int>(),
                          any_minor ? std::numeric_limits<int>::max() : 0};
    }
    if (value.size() == 2 && value[0].is_number_integer() &&
        value[1].is_number_integer()) {
        return PackFormat{value[0].get<int>(), value[1].get<int>()};
    }
    return std::nullopt;
}

std::optional<std::pair<int, int>> parse_supported_formats(const Json& pack) {
    const auto it = pack.find("supported_formats");
    if (it == pack.end()) {
        return std::nullopt;
    }
    if (it->is_array() && it->size() == 2 && (*it)[0].is_number_integer() &&
        (*it)[1].is_number_integer()) {
        return std::pair<int, int>{(*it)[0].get<int>(), (*it)[1].get<int>()};
    }
    if (it->is_object()) {
        const auto min_it = it->find("min_inclusive");
        const auto max_it = it->find("max_inclusive");
        if (min_it != it->end() && max_it != it->end() && min_it->is_number_integer() &&
            max_it->is_number_integer()) {
            return std::pair<int, int>{min_it->get<int>(), max_it->get<int>()};
        }
    }
    return std::nullopt;
}

std::string flatten_description(const Json& value) {
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
                result += flatten_description(part);
            }
            return result;
        }
    }
    if (value.is_array()) {
        std::string result;
        for (const Json& part : value) {
            result += flatten_description(part);
        }
        return result;
    }
    return {};
}

struct PackMeta {
    bool valid = false;
    int pack_format = 0;
    std::optional<std::pair<int, int>> supported;
    std::optional<PackFormat> min_format;
    std::optional<PackFormat> max_format;
    std::string description;
};

PackMeta parse_pack_meta(std::string_view text, std::string_view source) {
    PackMeta meta;
    const Json root = parse_json(text, source);
    if (!root.is_object()) {
        return meta;
    }
    const Json* pack = json_object(root, "pack");
    if (pack == nullptr) {
        return meta;
    }
    meta.pack_format = static_cast<int>(json_integer(*pack, "pack_format"));
    meta.supported = parse_supported_formats(*pack);
    if (const auto it = pack->find("min_format"); it != pack->end()) {
        meta.min_format = parse_pack_version_value(*it, false);
    }
    if (const auto it = pack->find("max_format"); it != pack->end()) {
        meta.max_format = parse_pack_version_value(*it, true);
    }
    if (const auto it = pack->find("description"); it != pack->end()) {
        meta.description = flatten_description(*it);
    }
    meta.valid = meta.pack_format > 0;
    return meta;
}

PackMeta read_pack_meta(const std::filesystem::path& path, bool directory) {
    std::string text;
    try {
        if (directory) {
            const std::filesystem::path file = path / "pack.mcmeta";
            std::error_code error;
            if (!std::filesystem::is_regular_file(file, error)) {
                return {};
            }
            text = read_text_file(file);
        } else {
            const auto entry = read_zip_entry(path, "pack.mcmeta", 1024 * 1024);
            if (!entry) {
                return {};
            }
            text = *entry;
        }
    } catch (const Error& error) {
        log_warn("Unable to read pack.mcmeta from " + path_to_utf8(path) + ": " +
                 error.what());
        return {};
    }
    try {
        return parse_pack_meta(text, path_to_utf8(path) + "!pack.mcmeta");
    } catch (const Error& error) {
        log_warn(std::string(error.what()));
        return {};
    }
}

bool pack_has_icon(const std::filesystem::path& path, bool directory) {
    std::error_code error;
    try {
        if (directory) {
            return std::filesystem::is_regular_file(path / "pack.png", error) && !error;
        }
        return zip_entry_exists(path, "pack.png");
    } catch (const Error&) {
        return false;
    }
}

ResourcePackCompatibility compatibility_for(const PackMeta& meta,
                                            const ResourcePackContext& context) {
    const bool has_context =
        context.pack_format > 0 || !trim(context.minecraft_version).empty();
    if (!meta.valid) {
        return has_context ? ResourcePackCompatibility::MissingMetadata
                           : ResourcePackCompatibility::Unknown;
    }
    if (context.pack_format <= 0) {
        return has_context ? ResourcePackCompatibility::MissingGameMetadata
                           : ResourcePackCompatibility::Unknown;
    }
    const PackFormat required{context.pack_format, context.pack_format_minor};
    if (required.first <= 15) {
        if (required.first < 1) {
            return ResourcePackCompatibility::Invalid;
        }
        if (meta.pack_format < required.first) {
            return ResourcePackCompatibility::TooOld;
        }
        if (meta.pack_format > required.first) {
            return ResourcePackCompatibility::TooNew;
        }
        return ResourcePackCompatibility::Compatible;
    }
    if (required.first <= 64) {
        int minimum = meta.pack_format;
        int maximum = meta.pack_format;
        if (meta.supported) {
            minimum = meta.supported->first;
            maximum = meta.supported->second;
        }
        if (minimum > maximum || minimum <= 0) {
            return ResourcePackCompatibility::Invalid;
        }
        if (maximum < required.first) {
            return ResourcePackCompatibility::TooOld;
        }
        if (minimum > required.first) {
            return ResourcePackCompatibility::TooNew;
        }
        return ResourcePackCompatibility::Compatible;
    }

    PackFormat minimum = meta.min_format.value_or(
        PackFormat{meta.supported ? meta.supported->first : meta.pack_format, 0});
    PackFormat maximum = meta.max_format.value_or(
        PackFormat{meta.supported ? meta.supported->second : meta.pack_format, 0});
    if (meta.supported && meta.supported->second == 64 && !meta.max_format) {
        maximum = required;
    }
    if (minimum.first <= 0 || minimum > maximum) {
        return ResourcePackCompatibility::Invalid;
    }
    if (maximum < required) {
        return ResourcePackCompatibility::TooOld;
    }
    if (minimum > required) {
        return ResourcePackCompatibility::TooNew;
    }
    return ResourcePackCompatibility::Compatible;
}

bool compatibility_known(ResourcePackCompatibility compatibility) {
    return compatibility != ResourcePackCompatibility::Unknown;
}

bool compatibility_incompatible(ResourcePackCompatibility compatibility) {
    return compatibility_known(compatibility) &&
           compatibility != ResourcePackCompatibility::Compatible;
}

struct PackEntry {
    std::filesystem::path path;
    std::string name;
    bool directory = false;
};

std::optional<PackEntry> find_pack(const std::filesystem::path& directory,
                                   std::string_view name) {
    std::error_code error;
    std::filesystem::directory_iterator iterator(directory, error);
    if (error) {
        return std::nullopt;
    }
    const std::filesystem::directory_iterator end;
    while (iterator != end) {
        std::error_code status_error;
        const auto status = iterator->symlink_status(status_error);
        if (!status_error) {
            const std::string filename = path_to_utf8(iterator->path().filename());
            if (iequals(filename, name)) {
                if (std::filesystem::is_regular_file(status)) {
                    if (has_suffix_ci(filename, ".zip")) {
                        return PackEntry{iterator->path(), filename, false};
                    }
                } else if (std::filesystem::is_directory(status)) {
                    return PackEntry{iterator->path(), filename, true};
                }
            }
        }
        iterator.increment(error);
        if (error) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

struct OptionsFile {
    std::vector<std::string> lines;
    bool exists = false;
    bool crlf = true;
    bool trailing_newline = true;
};

std::vector<std::string> split_lines(std::string_view text) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start < text.size()) {
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

OptionsFile read_options(const std::filesystem::path& path) {
    OptionsFile options;
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) {
        return options;
    }
    const std::string text = read_text_file(path);
    options.exists = true;
    options.crlf = text.find("\r\n") != std::string::npos;
    options.trailing_newline = !text.empty() && text.back() == '\n';
    options.lines = split_lines(text);
    return options;
}

std::optional<std::size_t> find_key(const std::vector<std::string>& lines,
                                    std::string_view key) {
    for (std::size_t index = 0; index < lines.size(); ++index) {
        if (lines[index].rfind(key, 0) == 0) {
            return index;
        }
    }
    return std::nullopt;
}

std::vector<std::string> parse_pack_list(std::string_view line,
                                         std::string_view key,
                                         std::string_view context) {
    std::string value = trim(line.substr(key.size()));
    if (value.empty()) {
        return {};
    }
    const Json root = parse_json(value, context);
    if (!root.is_array()) {
        fail(std::string(context) + " must be a JSON array");
    }
    std::vector<std::string> ids;
    ids.reserve(root.size());
    for (const Json& item : root) {
        if (!item.is_string()) {
            fail(std::string(context) + " contains a non-string entry");
        }
        ids.push_back(item.get<std::string>());
    }
    return ids;
}

bool contains_id(const std::vector<std::string>& ids, std::string_view id) {
    return std::any_of(ids.begin(), ids.end(), [id](const std::string& value) {
        return iequals(value, id);
    });
}

bool contains_pack(const std::vector<std::string>& ids, std::string_view name) {
    return contains_id(ids, name) || contains_id(ids, "file/" + std::string(name));
}

void remove_pack(std::vector<std::string>& ids, std::string_view name) {
    ids.erase(std::remove_if(ids.begin(), ids.end(),
                             [name](const std::string& value) {
                                 return iequals(value, name) ||
                                        iequals(value, "file/" + std::string(name));
                             }),
              ids.end());
}

void set_pack_list_line(std::vector<std::string>& lines,
                        std::string_view key,
                        const std::vector<std::string>& ids) {
    Json array = Json::array();
    for (const std::string& id : ids) {
        array.push_back(id);
    }
    const std::string line = std::string(key) + array.dump();
    const auto index = find_key(lines, key);
    if (index) {
        lines[*index] = line;
    } else {
        lines.push_back(line);
    }
}

void write_options(const std::filesystem::path& path, const OptionsFile& options) {
    const std::string eol = options.crlf ? "\r\n" : "\n";
    std::string text;
    for (const std::string& line : options.lines) {
        text += line;
        text += eol;
    }
    if (!options.trailing_newline && !options.lines.empty()) {
        text.resize(text.size() - eol.size());
    }
    write_text_file(path, text);
}

bool supports_new_options(const ResourcePackContext& context) {
    if (trim(context.minecraft_version).empty()) {
        return true;
    }
    return minecraft_uses_new_resource_pack_options(context.minecraft_version);
}

} // namespace

int minecraft_resource_pack_format(std::string_view version) {
    int minor = 0;
    int patch = 0;
    if (!parse_release_version(trim(version), minor, patch)) {
        int year = 0;
        int week = 0;
        if (!parse_snapshot_version(trim(version), year, week)) {
            return 0;
        }
        const auto before = [year, week](int target_year, int target_week) {
            return year < target_year ||
                   (year == target_year && week < target_week);
        };
        if (before(13, 24)) {
            return 0;
        }
        if (before(15, 31)) {
            return 1;
        }
        if (before(16, 32)) {
            return 2;
        }
        if (before(17, 48)) {
            return 3;
        }
        if (before(18, 47)) {
            return 4;
        }
        return 0;
    }
    if (minor <= 8) {
        return 1;
    }
    if (minor <= 10) {
        return 2;
    }
    if (minor <= 12) {
        return 3;
    }
    if (minor == 13 || (minor == 14 && patch <= 4)) {
        return 4;
    }
    if (minor == 15 || (minor == 16 && patch <= 1)) {
        return 5;
    }
    if (minor == 16) {
        return 6;
    }
    if (minor == 17) {
        return 7;
    }
    if (minor == 18) {
        return 8;
    }
    if (minor == 19 && patch <= 2) {
        return 9;
    }
    if (minor == 19 && patch == 3) {
        return 12;
    }
    if (minor == 19 && patch == 4) {
        return 13;
    }
    if (minor == 20 && patch <= 1) {
        return 15;
    }
    if (minor == 20 && patch == 2) {
        return 18;
    }
    if (minor == 20 && (patch == 3 || patch == 4)) {
        return 22;
    }
    if (minor == 20 && (patch == 5 || patch == 6)) {
        return 32;
    }
    if (minor == 21 && patch <= 1) {
        return 34;
    }
    if (minor == 21 && (patch == 2 || patch == 3)) {
        return 42;
    }
    if (minor == 21 && patch == 4) {
        return 46;
    }
    if (minor == 21 && patch == 5) {
        return 55;
    }
    if (minor == 21 && patch == 6) {
        return 63;
    }
    if (minor == 21 && (patch == 7 || patch == 8)) {
        return 64;
    }
    return 0;
}

bool minecraft_uses_new_resource_pack_options(std::string_view version) {
    const std::string version_text = trim(version);
    int minor = 0;
    int patch = 0;
    if (parse_release_version(version_text, minor, patch)) {
        return minor >= 13;
    }
    int year = 0;
    int week = 0;
    if (parse_snapshot_version(version_text, year, week)) {
        if (year != 17) {
            return year > 17;
        }
        return week >= 43;
    }
    // Newer year-based versions (for example 25w31a) are always new format.
    return true;
}

ResourcePackContext resource_pack_context(const std::filesystem::path& game_dir,
                                          std::string_view minecraft_version) {
    ResourcePackContext context;
    context.minecraft_version = trim(minecraft_version);
    if (context.minecraft_version.empty()) {
        return context;
    }
    const std::filesystem::path version_dir =
        game_dir / "versions" / path_from_utf8(context.minecraft_version);
    const std::filesystem::path jar =
        version_dir / path_from_utf8(context.minecraft_version + ".jar");
    std::error_code error;
    if (std::filesystem::is_regular_file(jar, error) && !error) {
        try {
            const auto entry = read_zip_entry(jar, "version.json", 1024 * 1024);
            if (entry) {
                const Json root = parse_json(*entry, path_to_utf8(jar) + "!version.json");
                if (const Json* pack_version = json_object(root, "pack_version")) {
                    const auto it = pack_version->find("resource");
                    if (it != pack_version->end() && it->is_number_integer()) {
                        context.pack_format = it->get<int>();
                    } else {
                        context.pack_format = static_cast<int>(
                            json_integer(*pack_version, "resource_major"));
                        context.pack_format_minor = static_cast<int>(
                            json_integer(*pack_version, "resource_minor"));
                    }
                }
            }
        } catch (const Error& warning) {
            log_warn("Unable to read the resource pack format from " +
                     path_to_utf8(jar) + ": " + warning.what());
        }
    }
    if (context.pack_format <= 0) {
        context.pack_format =
            minecraft_resource_pack_format(context.minecraft_version);
    }
    return context;
}

std::filesystem::path resourcepacks_directory(const std::filesystem::path& game_dir) {
    return game_dir / "resourcepacks";
}

std::vector<InstalledResourcePack> scan_resource_packs(
    const std::filesystem::path& game_dir,
    const ResourcePackContext& context) {
    const std::filesystem::path directory = resourcepacks_directory(game_dir);
    std::error_code error;
    const auto status = std::filesystem::status(directory, error);
    if (error) {
        if (error == std::errc::no_such_file_or_directory) {
            return {};
        }
        fail("Cannot read resourcepacks directory: " + error.message());
    }
    if (!std::filesystem::is_directory(status)) {
        return {};
    }

    std::vector<InstalledResourcePack> packs;
    std::filesystem::directory_iterator iterator(directory, error);
    if (error) {
        fail("Cannot read resourcepacks directory: " + error.message());
    }
    const std::filesystem::directory_iterator end;
    while (iterator != end) {
        std::error_code status_error;
        const auto entry_status = iterator->symlink_status(status_error);
        if (!status_error) {
            const std::string filename = path_to_utf8(iterator->path().filename());
            const bool is_directory = std::filesystem::is_directory(entry_status);
            if ((!is_directory && std::filesystem::is_regular_file(entry_status) &&
                 has_suffix_ci(filename, ".zip")) ||
                is_directory) {
                InstalledResourcePack pack;
                pack.path = iterator->path();
                pack.name = filename;
                pack.directory = is_directory;
                const PackMeta meta = read_pack_meta(pack.path, pack.directory);
                pack.has_metadata = meta.valid;
                pack.pack_format = meta.pack_format;
                pack.supported_formats = meta.supported;
                pack.description = meta.description;
                pack.compatibility = compatibility_for(meta, context);
                pack.has_icon = pack_has_icon(pack.path, pack.directory);
                packs.push_back(std::move(pack));
            }
        }
        iterator.increment(error);
        if (error) {
            fail("Cannot read resourcepacks directory: " + error.message());
        }
    }

    std::vector<std::string> enabled_ids;
    std::vector<std::string> incompatible_ids;
    const OptionsFile options = read_options(game_dir / "options.txt");
    if (const auto index = find_key(options.lines, kPacksKey)) {
        try {
            enabled_ids = parse_pack_list(
                options.lines[*index], kPacksKey, "options.txt resourcePacks");
        } catch (const Error& parse_error) {
            log_warn("Cannot read enabled resource packs: " +
                     std::string(parse_error.what()));
        }
    }
    if (const auto index = find_key(options.lines, kIncompatibleKey)) {
        try {
            incompatible_ids = parse_pack_list(options.lines[*index], kIncompatibleKey,
                                               "options.txt incompatibleResourcePacks");
        } catch (const Error& parse_error) {
            log_warn("Cannot read incompatible resource packs: " +
                     std::string(parse_error.what()));
        }
    }
    for (InstalledResourcePack& pack : packs) {
        const bool listed = contains_pack(enabled_ids, pack.name);
        if (!compatibility_known(pack.compatibility)) {
            pack.enabled = listed;
        } else {
            pack.enabled =
                listed && (compatibility_incompatible(pack.compatibility) ==
                           contains_pack(incompatible_ids, pack.name));
        }
    }

    std::sort(packs.begin(), packs.end(),
              [](const InstalledResourcePack& left,
                 const InstalledResourcePack& right) {
                  const std::string left_key = to_lower_ascii(left.name);
                  const std::string right_key = to_lower_ascii(right.name);
                  if (left_key != right_key) {
                      return left_key < right_key;
                  }
                  return left.name < right.name;
              });
    return packs;
}

ResourcePackToggleResult set_resource_pack_enabled(
    const std::filesystem::path& game_dir,
    std::string_view name,
    bool enabled,
    const ResourcePackContext& context) {
    const std::string input = trim(name);
    if (!is_safe_resource_pack_name(input)) {
        fail("Invalid resource pack name: " + input);
    }

    const std::filesystem::path directory = resourcepacks_directory(game_dir);
    const auto pack = find_pack(directory, input);
    if (!pack) {
        fail("Resource pack not found in " + path_to_utf8(directory) + ": " + input);
    }

    const PackMeta meta = read_pack_meta(pack->path, pack->directory);
    const ResourcePackCompatibility compatibility = compatibility_for(meta, context);
    const bool incompatible = compatibility_incompatible(compatibility);
    const std::string pack_id =
        supports_new_options(context) ? "file/" + pack->name : pack->name;

    const std::filesystem::path options_path = game_dir / "options.txt";
    OptionsFile options = read_options(options_path);
    std::vector<std::string> ids;
    if (const auto index = find_key(options.lines, kPacksKey)) {
        ids = parse_pack_list(
            options.lines[*index], kPacksKey, "options.txt resourcePacks");
    }
    std::vector<std::string> incompatible_ids;
    if (const auto index = find_key(options.lines, kIncompatibleKey)) {
        incompatible_ids = parse_pack_list(options.lines[*index], kIncompatibleKey,
                                           "options.txt incompatibleResourcePacks");
    }

    const bool currently_enabled = contains_pack(ids, pack->name);
    bool changed = false;
    if (enabled) {
        if (!currently_enabled) {
            ids.push_back(pack_id);
            changed = true;
        }
        if (compatibility_known(compatibility)) {
            const bool listed_incompatible =
                contains_pack(incompatible_ids, pack->name);
            if (incompatible && !listed_incompatible) {
                incompatible_ids.push_back(pack_id);
                changed = true;
            } else if (!incompatible && listed_incompatible) {
                remove_pack(incompatible_ids, pack->name);
                changed = true;
            }
        }
    } else {
        if (currently_enabled) {
            remove_pack(ids, pack->name);
            changed = true;
        }
        if (contains_pack(incompatible_ids, pack->name)) {
            remove_pack(incompatible_ids, pack->name);
            changed = true;
        }
    }
    if (!changed) {
        return ResourcePackToggleResult{pack->name, enabled, false, options_path};
    }

    set_pack_list_line(options.lines, kPacksKey, ids);
    if (contains_pack(incompatible_ids, pack->name) ||
        !incompatible_ids.empty() ||
        find_key(options.lines, kIncompatibleKey)) {
        set_pack_list_line(options.lines, kIncompatibleKey, incompatible_ids);
    }
    write_options(options_path, options);
    return ResourcePackToggleResult{pack->name, enabled, true, options_path};
}

} // namespace aurora
