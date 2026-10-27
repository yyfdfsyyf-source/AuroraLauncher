#include "core/forge.hpp"

#include "core/error.hpp"
#include "core/http.hpp"
#include "core/java.hpp"
#include "core/json.hpp"
#include "core/log.hpp"
#include "core/paths.hpp"
#include "core/process.hpp"
#include "core/strings.hpp"

#include <miniz.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <set>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace aurora {

namespace {

constexpr std::uint64_t kMaxInstallerBytes = 512ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMaxInstallerMetadataBytes = 64ULL * 1024ULL * 1024ULL;

std::string kind_token(ForgeKind kind) {
    return kind == ForgeKind::Forge ? "forge" : "neoforge";
}

std::string kind_display(ForgeKind kind) {
    return kind == ForgeKind::Forge ? "Forge" : "NeoForge";
}

bool is_hex_digit(char value) {
    return std::isxdigit(static_cast<unsigned char>(value)) != 0;
}

bool is_safe_version_text(std::string_view value) {
    if (value.empty() || value.size() > 96 || value == "." || value == "..") {
        return false;
    }
    if (value.front() == '.' || value.front() == '-' || value.front() == '_' ||
        value.front() == '+') {
        return false;
    }
    if (value.back() == '.' || value.back() == '-' || value.back() == '_' ||
        value.back() == '+') {
        return false;
    }
    if (value.find("..") != std::string_view::npos) {
        return false;
    }
    for (const char character : value) {
        if (!std::isalnum(static_cast<unsigned char>(character)) && character != '.' &&
            character != '-' && character != '_' && character != '+') {
            return false;
        }
    }
    return true;
}

// Matches a complete version token so that loader 21.1.7 does not match the
// unrelated version 21.1.72. Identifiers place the token after a separator
// (usually '-') and may append a suffix such as "-beta".
bool contains_version_token(std::string_view text, std::string_view token) {
    if (token.empty()) {
        return false;
    }
    std::size_t position = text.find(token);
    while (position != std::string_view::npos) {
        const bool before_is_boundary =
            position == 0 ||
            (std::isalnum(static_cast<unsigned char>(text[position - 1])) == 0 &&
             text[position - 1] != '.');
        const std::size_t end = position + token.size();
        const bool after_is_boundary =
            end == text.size() ||
            (std::isalnum(static_cast<unsigned char>(text[end])) == 0 &&
             text[end] != '.');
        if (before_is_boundary && after_is_boundary) {
            return true;
        }
        position = text.find(token, position + 1);
    }
    return false;
}

void require_safe_minecraft_version(std::string_view version) {
    if (!is_safe_minecraft_version(version)) {
        fail("Unsafe Minecraft version: " + std::string(version));
    }
}

void require_safe_loader_version(std::string_view version, std::string_view context) {
    if (!is_safe_loader_version(version)) {
        fail("Unsafe " + std::string(context) + " version: " + std::string(version));
    }
}

void require_https_url(std::string_view url, std::string_view context) {
    if (url.rfind("https://", 0) != 0) {
        fail(std::string(context) + " URL must use HTTPS: " + std::string(url));
    }
}

std::string join_url(std::string_view base, std::string_view relative) {
    if (base.empty() || relative.empty()) {
        fail("Cannot build an empty URL");
    }
    std::string result(base);
    while (!result.empty() && result.back() == '/') {
        result.pop_back();
    }
    std::string path(relative);
    while (!path.empty() && path.front() == '/') {
        path.erase(path.begin());
    }
    if (result.empty() || path.empty()) {
        fail("Cannot build an empty URL");
    }
    return result + "/" + path;
}

struct VersionKey {
    std::vector<long long> numbers;
    bool prerelease = false;
};

VersionKey make_version_key(std::string_view value) {
    VersionKey key;
    std::size_t position = 0;
    while (position < value.size()) {
        const char character = value[position];
        if (std::isdigit(static_cast<unsigned char>(character)) != 0) {
            long long number = 0;
            while (position < value.size() &&
                   std::isdigit(static_cast<unsigned char>(value[position])) != 0) {
                if (number < 1000000000LL) {
                    number = number * 10 + (value[position] - '0');
                }
                ++position;
            }
            key.numbers.push_back(number);
        } else if (character == '.') {
            ++position;
        } else {
            key.prerelease = true;
            break;
        }
    }
    return key;
}

int compare_version_keys(const VersionKey& left, const VersionKey& right) {
    const std::size_t count = std::min(left.numbers.size(), right.numbers.size());
    for (std::size_t i = 0; i < count; ++i) {
        if (left.numbers[i] != right.numbers[i]) {
            return left.numbers[i] < right.numbers[i] ? -1 : 1;
        }
    }
    if (left.numbers.size() != right.numbers.size()) {
        return left.numbers.size() < right.numbers.size() ? -1 : 1;
    }
    if (left.prerelease != right.prerelease) {
        return left.prerelease ? -1 : 1;
    }
    return 0;
}

bool loader_version_greater(std::string_view left, std::string_view right) {
    const int comparison = compare_version_keys(make_version_key(left), make_version_key(right));
    if (comparison != 0) {
        return comparison > 0;
    }
    return left > right;
}

std::string path_key(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::path normalized = absolute_path(path);
    const auto canonical = std::filesystem::weakly_canonical(normalized, error);
    if (!error) {
        normalized = canonical;
    }
    std::string key = path_to_utf8(normalized.lexically_normal());
    for (char& character : key) {
        if (character == '/') {
            character = '\\';
        }
    }
#ifdef _WIN32
    key = to_lower_ascii(std::move(key));
#endif
    return key;
}

bool path_within(const std::filesystem::path& base, const std::filesystem::path& candidate) {
    const std::string base_key = path_key(base);
    const std::string candidate_key = path_key(candidate);
    if (candidate_key.size() < base_key.size() ||
        candidate_key.compare(0, base_key.size(), base_key) != 0) {
        return false;
    }
    if (candidate_key.size() == base_key.size()) {
        return true;
    }
    return candidate_key[base_key.size()] == '\\' || candidate_key[base_key.size()] == '/';
}

void require_path_within(const std::filesystem::path& base,
                         const std::filesystem::path& candidate,
                         std::string_view context) {
    if (!path_within(base, candidate)) {
        fail("Unsafe " + std::string(context) + " path outside the game directory: " +
             path_to_utf8(candidate));
    }
}

void require_regular_file(const std::filesystem::path& path, std::string_view context) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error) {
        fail("Unable to inspect " + std::string(context) + ": " + path_to_utf8(path));
    }
    if (std::filesystem::is_symlink(status)) {
        fail(std::string(context) + " is a symbolic link: " + path_to_utf8(path));
    }
    if (!std::filesystem::is_regular_file(status)) {
        fail(std::string(context) + " is not a regular file: " + path_to_utf8(path));
    }
}

std::vector<std::uint8_t> read_binary_file(const std::filesystem::path& path,
                                           std::uint64_t max_bytes,
                                           std::string_view context) {
    std::error_code error;
    const auto file_size = std::filesystem::file_size(path, error);
    if (error) {
        fail("Unable to read " + std::string(context) + " size: " + path_to_utf8(path));
    }
    if (file_size == 0) {
        fail(std::string(context) + " is empty: " + path_to_utf8(path));
    }
    if (file_size > max_bytes) {
        fail(std::string(context) + " is too large: " + path_to_utf8(path));
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        fail("Unable to open " + std::string(context) + ": " + path_to_utf8(path));
    }
    std::vector<std::uint8_t> data(static_cast<std::size_t>(file_size));
    input.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!input || input.gcount() != static_cast<std::streamsize>(data.size())) {
        fail("Unable to read " + std::string(context) + ": " + path_to_utf8(path));
    }
    return data;
}

struct ZipReader {
    mz_zip_archive archive{};
    bool initialized = false;

    ~ZipReader() {
        if (initialized) {
            mz_zip_reader_end(&archive);
        }
    }
};

std::optional<std::string> read_zip_entry(mz_zip_archive& archive,
                                          std::string_view name,
                                          std::uint64_t max_bytes) {
    const mz_uint count = mz_zip_reader_get_num_files(&archive);
    for (mz_uint index = 0; index < count; ++index) {
        mz_zip_archive_file_stat info{};
        if (!mz_zip_reader_file_stat(&archive, index, &info)) {
            continue;
        }
        if (info.m_filename == nullptr || name != std::string_view(info.m_filename)) {
            continue;
        }
        if (info.m_is_directory) {
            return std::nullopt;
        }
        if (info.m_uncomp_size > max_bytes) {
            fail("Installer entry is too large: " + std::string(name));
        }
        std::string content(static_cast<std::size_t>(info.m_uncomp_size), '\0');
        if (!content.empty() &&
            !mz_zip_reader_extract_to_mem(
                &archive, index, content.data(), content.size(), 0)) {
            fail("Unable to read installer entry: " + std::string(name));
        }
        return content;
    }
    return std::nullopt;
}

std::string manifest_main_class(std::string_view manifest) {
    std::size_t position = 0;
    while (position < manifest.size()) {
        const std::size_t end = manifest.find('\n', position);
        const std::size_t length =
            (end == std::string_view::npos ? manifest.size() : end) - position;
        const std::string line = trim(manifest.substr(position, length));
        if (to_lower_ascii(line).rfind("main-class:", 0) == 0) {
            return trim(line.substr(11));
        }
        if (end == std::string_view::npos) {
            break;
        }
        position = end + 1;
    }
    return {};
}

void ensure_launcher_profiles(const std::filesystem::path& game_dir) {
    const std::filesystem::path legacy = game_dir / "launcher_profiles.json";
    const std::filesystem::path store = game_dir / "launcher_profiles_microsoft_store.json";
    bool found = false;
    for (const auto& path : {legacy, store}) {
        std::error_code error;
        if (std::filesystem::is_symlink(path, error)) {
            fail("Launcher profile is a symbolic link: " + path_to_utf8(path));
        }
        if (!std::filesystem::exists(path, error)) {
            continue;
        }
        if (!std::filesystem::is_regular_file(path, error)) {
            fail("Launcher profile is not a regular file: " + path_to_utf8(path));
        }
        found = true;
    }
    if (found) {
        return;
    }
    log_info("Creating a minimal launcher_profiles.json for the Forge installer");
    write_text_file(legacy, R"({"profiles":{},"settings":{},"version":3})");
}

} // namespace

bool is_safe_loader_version(std::string_view version) {
    return is_safe_version_text(version);
}

bool is_safe_minecraft_version(std::string_view version) {
    return is_safe_version_text(version);
}

std::vector<ForgePromotion> parse_forge_promotions(std::string_view promotions_json,
                                                   std::string_view minecraft_version) {
    require_safe_minecraft_version(minecraft_version);
    const Json root = parse_json(promotions_json, "Forge promotions");
    const Json* promos = json_object(root, "promos");
    if (promos == nullptr) {
        fail("Forge promotions JSON has no 'promos' object");
    }

    std::vector<ForgePromotion> result;
    for (auto it = promos->begin(); it != promos->end(); ++it) {
        const std::string& key = it.key();
        const std::size_t separator = key.rfind('-');
        if (separator == std::string::npos || key.substr(0, separator) != minecraft_version) {
            continue;
        }
        const std::string channel = to_lower_ascii(key.substr(separator + 1));
        ForgeChannel parsed_channel = ForgeChannel::Latest;
        if (channel == "recommended") {
            parsed_channel = ForgeChannel::Recommended;
        } else if (channel == "latest") {
            parsed_channel = ForgeChannel::Latest;
        } else {
            continue;
        }
        if (!it.value().is_string()) {
            fail("Forge promotion '" + key + "' must contain a version string");
        }
        const std::string version = trim(it.value().get<std::string>());
        require_safe_loader_version(version, "Forge");
        result.push_back(ForgePromotion{
            std::string(minecraft_version), version, parsed_channel, false});
    }
    return result;
}

std::optional<ForgePromotion> select_forge_version(
    std::string_view promotions_json,
    std::string_view minecraft_version,
    std::optional<std::string> explicit_version,
    ForgeChannel preferred_channel) {
    require_safe_minecraft_version(minecraft_version);
    if (explicit_version) {
        ForgePromotion promotion;
        promotion.minecraft = std::string(minecraft_version);
        promotion.version = normalize_forge_version(minecraft_version, *explicit_version);
        promotion.channel = preferred_channel;
        promotion.explicit_version = true;
        return promotion;
    }

    const auto promotions = parse_forge_promotions(promotions_json, minecraft_version);
    const auto find_channel = [&promotions](ForgeChannel channel)
        -> std::optional<ForgePromotion> {
        for (const ForgePromotion& promotion : promotions) {
            if (promotion.channel == channel) {
                return promotion;
            }
        }
        return std::nullopt;
    };

    if (auto preferred = find_channel(preferred_channel)) {
        return preferred;
    }
    const ForgeChannel other = preferred_channel == ForgeChannel::Recommended
                                   ? ForgeChannel::Latest
                                   : ForgeChannel::Recommended;
    return find_channel(other);
}

std::string neoforge_version_prefix(std::string_view minecraft_version) {
    require_safe_minecraft_version(minecraft_version);
    if (minecraft_version == "1.20.1") {
        // NeoForge 1.20.1 is published as net.neoforged:forge (47.1.x), not
        // under the net.neoforged:neoforge Maven artifact.
        return {};
    }

    std::string base(minecraft_version);
    if (base.rfind("1.", 0) == 0) {
        base.erase(0, 2);
    }
    const auto parts = split(base, '.');
    if (parts.empty() || parts[0].empty()) {
        fail("Unable to derive a NeoForge version prefix from " +
             std::string(minecraft_version));
    }
    if (!std::all_of(parts[0].begin(), parts[0].end(), [](unsigned char character) {
            return std::isdigit(character) != 0;
        })) {
        fail("NeoForge requires a numeric Minecraft release version, got " +
             std::string(minecraft_version));
    }
    if (parts.size() == 1) {
        return parts[0] + ".0.";
    }
    if (!std::all_of(parts[1].begin(), parts[1].end(), [](unsigned char character) {
            return std::isdigit(character) != 0;
        })) {
        fail("NeoForge requires a numeric Minecraft release version, got " +
             std::string(minecraft_version));
    }
    return parts[0] + "." + parts[1] + ".";
}

std::vector<std::string> select_neoforge_versions(const std::vector<std::string>& versions,
                                                  std::string_view minecraft_version) {
    const std::string prefix = neoforge_version_prefix(minecraft_version);
    if (prefix.empty()) {
        return {};
    }

    std::vector<std::string> result;
    std::set<std::string> seen;
    for (const std::string& version : versions) {
        if (version.size() <= prefix.size() ||
            version.compare(0, prefix.size(), prefix) != 0 ||
            std::isdigit(static_cast<unsigned char>(version[prefix.size()])) == 0) {
            continue;
        }
        require_safe_loader_version(version, "NeoForge");
        if (seen.insert(version).second) {
            result.push_back(version);
        }
    }
    std::sort(result.begin(), result.end(), loader_version_greater);
    return result;
}

std::optional<std::string> select_neoforge_version(const std::vector<std::string>& versions,
                                                   std::string_view minecraft_version) {
    const auto selected = select_neoforge_versions(versions, minecraft_version);
    if (selected.empty()) {
        return std::nullopt;
    }
    return selected.front();
}

std::vector<std::string> parse_neoforge_metadata(std::string_view xml) {
    if (xml.size() >= 3 && static_cast<unsigned char>(xml[0]) == 0xEF &&
        static_cast<unsigned char>(xml[1]) == 0xBB &&
        static_cast<unsigned char>(xml[2]) == 0xBF) {
        xml.remove_prefix(3);
    }
    if (xml.empty() || xml.find("<metadata") == std::string_view::npos) {
        fail("NeoForge Maven metadata is not valid XML");
    }

    const std::size_t versions_open = xml.find("<versions");
    if (versions_open == std::string_view::npos) {
        fail("NeoForge Maven metadata has no <versions> element");
    }
    const std::size_t versions_tag_end = xml.find('>', versions_open);
    if (versions_tag_end == std::string_view::npos) {
        fail("NeoForge Maven metadata has a malformed <versions> element");
    }
    const std::size_t versions_close = xml.find("</versions>", versions_tag_end + 1);
    if (versions_close == std::string_view::npos) {
        fail("NeoForge Maven metadata has an unterminated <versions> element");
    }
    if (xml.find("</metadata>", versions_close) == std::string_view::npos) {
        fail("NeoForge Maven metadata has an unterminated <metadata> element");
    }

    const std::string_view block =
        xml.substr(versions_tag_end + 1, versions_close - versions_tag_end - 1);
    std::vector<std::string> versions;
    std::set<std::string> seen;
    std::size_t position = 0;
    while (position < block.size()) {
        const std::size_t open = block.find("<version", position);
        if (open == std::string_view::npos) {
            break;
        }
        const std::size_t name_end = open + 8;
        if (name_end >= block.size() ||
            (block[name_end] != '>' &&
             std::isspace(static_cast<unsigned char>(block[name_end])) == 0)) {
            position = name_end;
            continue;
        }
        const std::size_t open_end = block.find('>', name_end);
        if (open_end == std::string_view::npos) {
            fail("NeoForge Maven metadata has a malformed <version> element");
        }
        if (open_end > 0 && block[open_end - 1] == '/') {
            fail("NeoForge Maven metadata contains an empty <version> element");
        }
        const std::size_t close = block.find("</version>", open_end + 1);
        if (close == std::string_view::npos) {
            fail("NeoForge Maven metadata has an unterminated <version> element");
        }
        const std::string value = trim(block.substr(open_end + 1, close - open_end - 1));
        if (value.empty() || value.find('<') != std::string::npos ||
            value.find('&') != std::string::npos) {
            fail("NeoForge Maven metadata contains an invalid version entry");
        }
        require_safe_loader_version(value, "NeoForge");
        if (seen.insert(value).second) {
            versions.push_back(value);
        }
        position = close + 10;
    }

    if (versions.empty()) {
        fail("NeoForge Maven metadata has no <version> entries");
    }
    return versions;
}

std::string normalize_forge_version(std::string_view minecraft_version,
                                    std::string_view forge_version) {
    require_safe_minecraft_version(minecraft_version);
    std::string value = trim(forge_version);
    const std::string minecraft_prefix = std::string(minecraft_version) + "-";
    if (value.rfind(minecraft_prefix + "forge-", 0) == 0) {
        value.erase(0, minecraft_prefix.size() + 6);
    } else if (value.rfind(minecraft_prefix, 0) == 0) {
        value.erase(0, minecraft_prefix.size());
    } else if (value.rfind("forge-", 0) == 0) {
        value.erase(0, 6);
    }
    if (value.rfind("forge", 0) == 0 && value.size() > 5 &&
        std::isdigit(static_cast<unsigned char>(value[5])) != 0) {
        value.erase(0, 5);
    }
    require_safe_loader_version(value, "Forge");
    return value;
}

std::string normalize_neoforge_version(std::string_view neoforge_version) {
    std::string value = trim(neoforge_version);
    if (value.rfind("neoforge-", 0) == 0) {
        value.erase(0, 9);
    } else {
        const std::size_t marker = value.find("-neoforge-");
        if (marker != std::string::npos) {
            value.erase(0, marker + 10);
        }
    }
    require_safe_loader_version(value, "NeoForge");
    return value;
}

int required_java_major_for(ForgeKind kind,
                            std::string_view minecraft_version,
                            std::string_view loader_version) {
    require_safe_minecraft_version(minecraft_version);
    int required = 17;
    if (minecraft_version.rfind("1.", 0) == 0) {
        const auto parts = split(std::string(minecraft_version.substr(2)), '.');
        int minor = 0;
        int patch = 0;
        if (!parts.empty()) {
            minor = std::atoi(parts[0].c_str());
        }
        if (parts.size() > 1) {
            patch = std::atoi(parts[1].c_str());
        }
        if (minor >= 21 || (minor == 20 && patch >= 5)) {
            required = 21;
        }
    } else {
        // Year-based Minecraft versions (for example 26.1) are known to need
        // at least Java 21. The caller can raise this with required_java_major.
        required = 21;
    }

    // NeoForge 1.20.1 was published as net.neoforged:forge 47.1.x. That
    // legacy line still runs on Java 17, so only the new NeoForge version
    // scheme (20.x/21.x/...) participates in the Java 21 bump.
    if (kind == ForgeKind::NeoForge && minecraft_version != "1.20.1" &&
        !loader_version.empty()) {
        const VersionKey key = make_version_key(loader_version);
        if (!key.numbers.empty()) {
            if (key.numbers[0] >= 21) {
                required = std::max(required, 21);
            } else if (key.numbers[0] >= 20) {
                required = std::max(required, 17);
            }
        }
    }
    return required;
}

std::string forge_installer_url(std::string_view minecraft_version,
                                std::string_view forge_version,
                                const ForgeEndpoints& endpoints) {
    require_safe_minecraft_version(minecraft_version);
    const std::string version = normalize_forge_version(minecraft_version, forge_version);
    const std::string combined = std::string(minecraft_version) + "-" + version;
    return join_url(
        endpoints.forge_maven_root,
        "net/minecraftforge/forge/" + combined + "/forge-" + combined + "-installer.jar");
}

std::string neoforge_installer_url(std::string_view neoforge_version,
                                   const ForgeEndpoints& endpoints) {
    const std::string version = normalize_neoforge_version(neoforge_version);
    return join_url(
        endpoints.neoforge_maven_root,
        "net/neoforged/neoforge/" + version + "/neoforge-" + version + "-installer.jar");
}

std::string sha1_sidecar_url(std::string_view artifact_url) {
    if (artifact_url.empty()) {
        fail("Cannot build a SHA-1 sidecar URL for an empty artifact URL");
    }
    return std::string(artifact_url) + ".sha1";
}

std::string parse_sha1_sidecar(std::string_view content) {
    std::size_t position = 0;
    while (position < content.size() &&
           std::isspace(static_cast<unsigned char>(content[position])) != 0) {
        ++position;
    }
    const std::size_t start = position;
    while (position < content.size() &&
           std::isspace(static_cast<unsigned char>(content[position])) == 0) {
        ++position;
    }
    if (start == position) {
        fail("SHA-1 sidecar is empty");
    }
    std::string token(content.substr(start, position - start));
    if (!token.empty() && token.front() == '*') {
        token.erase(token.begin());
    }
    if (token.size() != 40) {
        fail("SHA-1 sidecar does not contain a 40-character hash");
    }
    for (const char character : token) {
        if (!is_hex_digit(character)) {
            fail("SHA-1 sidecar contains a non-hexadecimal hash");
        }
    }
    return to_lower_ascii(std::move(token));
}

std::string expected_forge_version_id(std::string_view minecraft_version,
                                      std::string_view forge_version) {
    return std::string(minecraft_version) + "-forge-" +
           normalize_forge_version(minecraft_version, forge_version);
}

std::string expected_neoforge_version_id(std::string_view neoforge_version) {
    return "neoforge-" + normalize_neoforge_version(neoforge_version);
}

std::string InstallerCommand::command_line() const {
    return build_command_line(java, arguments);
}

InstallerCommand build_installer_command(const std::filesystem::path& java_executable,
                                         const std::filesystem::path& installer_jar,
                                         const std::filesystem::path& game_dir) {
    if (java_executable.empty() || installer_jar.empty() || game_dir.empty()) {
        fail("Java, installer and game directory are required for the Forge installer");
    }
    InstallerCommand command;
    command.java = absolute_path(java_executable);
    command.working_directory = absolute_path(game_dir);
    command.arguments = {
        "-Djava.awt.headless=true",
        "-Dfile.encoding=UTF-8",
        "-jar",
        path_to_utf8(absolute_path(installer_jar)),
        "--installClient",
        path_to_utf8(command.working_directory),
    };
    return command;
}

std::optional<ForgeVersionLocation> find_generated_forge_version(
    const std::filesystem::path& game_dir,
    ForgeKind kind,
    std::string_view minecraft_version,
    std::string_view loader_version,
    std::string_view expected_version_id) {
    require_safe_minecraft_version(minecraft_version);
    require_safe_loader_version(loader_version, kind_display(kind));

    const auto game_path = absolute_path(game_dir);
    std::error_code error;
    if (!std::filesystem::is_directory(game_path, error)) {
        return std::nullopt;
    }
    const auto canonical_game = std::filesystem::weakly_canonical(game_path, error);
    const auto game_root = error ? game_path : canonical_game;
    const auto versions_path = game_root / "versions";
    if (!std::filesystem::is_directory(versions_path, error)) {
        return std::nullopt;
    }
    const auto canonical_versions = std::filesystem::weakly_canonical(versions_path, error);
    const auto versions_root = error ? versions_path : canonical_versions;
    if (!path_within(game_root, versions_root)) {
        return std::nullopt;
    }

    struct Candidate {
        std::string id;
        std::filesystem::path json_path;
        int rank = 0;
        std::filesystem::file_time_type modified{};
    };

    const std::string kind_name = kind_token(kind);
    const std::string lowered_minecraft = to_lower_ascii(std::string(minecraft_version));
    const std::string lowered_loader = to_lower_ascii(std::string(loader_version));
    const std::string expected = trim(expected_version_id);
    std::vector<Candidate> candidates;

    std::filesystem::directory_iterator iterator(versions_root, error);
    const std::filesystem::directory_iterator end;
    while (!error && iterator != end) {
        const auto& entry = *iterator;
        std::error_code entry_error;
        if (entry.is_symlink(entry_error) || entry_error) {
            iterator.increment(error);
            continue;
        }
        if (!entry.is_directory(entry_error) || entry_error) {
            iterator.increment(error);
            continue;
        }

        const auto directory = entry.path();
        const std::string directory_name = path_to_utf8(directory.filename());
        std::filesystem::path json_name = directory.filename();
        json_name += ".json";
        std::vector<std::filesystem::path> json_files;
        const auto exact = directory / json_name;
        if (std::filesystem::is_regular_file(exact, entry_error) && !entry_error) {
            json_files.push_back(exact);
        } else {
            std::error_code scan_error;
            std::filesystem::directory_iterator nested(directory, scan_error);
            const std::filesystem::directory_iterator nested_end;
            while (!scan_error && nested != nested_end) {
                const auto& child = *nested;
                std::error_code child_error;
                if (!child.is_symlink(child_error) && !child_error &&
                    child.is_regular_file(child_error) && !child_error &&
                    iequals(path_to_utf8(child.path().extension()), ".json")) {
                    json_files.push_back(child.path());
                }
                nested.increment(scan_error);
            }
            std::sort(json_files.begin(), json_files.end());
        }

        for (const auto& json_file : json_files) {
            std::error_code canonical_error;
            const auto canonical_json =
                std::filesystem::weakly_canonical(json_file, canonical_error);
            if (canonical_error || !path_within(game_root, canonical_json) ||
                !path_within(versions_root, canonical_json)) {
                continue;
            }
            if (std::filesystem::is_symlink(json_file, canonical_error) || canonical_error) {
                continue;
            }
            Json value;
            try {
                value = read_json_file(canonical_json);
            } catch (const std::exception&) {
                continue;
            }
            if (!value.is_object()) {
                continue;
            }

            const std::string id = json_string(value, "id", directory_name);
            if (id.empty() || id != directory_name || !is_safe_loader_version(id)) {
                continue;
            }
            if (json_string(value, "inheritsFrom") != minecraft_version) {
                continue;
            }
            if (json_string(value, "mainClass").empty()) {
                continue;
            }

            const std::string lowered_id = to_lower_ascii(id);
            if (kind == ForgeKind::Forge) {
                if (lowered_id.find("neoforge") != std::string::npos ||
                    lowered_id.find("forge") == std::string::npos) {
                    continue;
                }
            } else if (lowered_id.find(kind_name) == std::string::npos) {
                continue;
            }
            if (!contains_version_token(lowered_id, lowered_loader)) {
                continue;
            }

            std::error_code time_error;
            const auto modified = std::filesystem::last_write_time(canonical_json, time_error);
            int rank = 2;
            if (!expected.empty() && id == expected) {
                rank = 0;
            } else if (kind == ForgeKind::Forge &&
                       lowered_id.rfind(lowered_minecraft + "-forge", 0) == 0) {
                rank = 1;
            } else if (kind == ForgeKind::NeoForge &&
                       lowered_id.rfind("neoforge-", 0) == 0) {
                rank = 1;
            }
            candidates.push_back(Candidate{
                id, canonical_json, rank, time_error ? std::filesystem::file_time_type{} : modified});
        }
        iterator.increment(error);
    }

    if (candidates.empty()) {
        return std::nullopt;
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& left, const Candidate& right) {
        if (left.rank != right.rank) {
            return left.rank < right.rank;
        }
        if (left.modified != right.modified) {
            return left.modified > right.modified;
        }
        return left.json_path < right.json_path;
    });
    return ForgeVersionLocation{candidates.front().id, candidates.front().json_path};
}

void verify_forge_installer_archive(const std::filesystem::path& path,
                                    ForgeKind kind,
                                    std::string_view minecraft_version,
                                    std::string_view loader_version) {
    require_safe_minecraft_version(minecraft_version);
    require_safe_loader_version(loader_version, kind_display(kind));
    require_regular_file(path, "Installer archive");
    if (!iequals(path_to_utf8(path.extension()), ".jar")) {
        fail("Installer archive is not a JAR file: " + path_to_utf8(path));
    }

    const auto data = read_binary_file(path, kMaxInstallerBytes, "installer archive");
    ZipReader reader;
    if (!mz_zip_reader_init_mem(&reader.archive, data.data(), data.size(), 0)) {
        fail("Installer archive is not a valid JAR/ZIP file: " + path_to_utf8(path));
    }
    reader.initialized = true;

    const auto manifest = read_zip_entry(
        reader.archive, "META-INF/MANIFEST.MF", kMaxInstallerMetadataBytes);
    const auto install_profile = read_zip_entry(
        reader.archive, "install_profile.json", kMaxInstallerMetadataBytes);
    const auto version_json = read_zip_entry(
        reader.archive, "version.json", kMaxInstallerMetadataBytes);
    if (!manifest || !install_profile || !version_json) {
        fail("Installer archive is missing its official metadata entries: " + path_to_utf8(path));
    }

    const std::string main_class = manifest_main_class(*manifest);
    if (main_class.empty() || to_lower_ascii(main_class).find("installer") == std::string::npos) {
        fail("Installer archive does not declare an installer main class: " + path_to_utf8(path));
    }

    const Json profile = parse_json(*install_profile, "installer install_profile.json");
    if (!profile.is_object()) {
        fail("Installer install_profile.json is not an object");
    }
    if (json_string(profile, "minecraft") != minecraft_version) {
        fail("Installer does not target Minecraft " + std::string(minecraft_version));
    }
    const std::string profile_name = json_string(profile, "profile");
    const std::string profile_version = json_string(profile, "version");
    const std::string profile_description =
        to_lower_ascii(profile_name + " " + profile_version);
    if (kind == ForgeKind::Forge) {
        if (profile_description.find("neoforge") != std::string::npos ||
            profile_description.find("forge") == std::string::npos) {
            fail("Installer profile does not identify itself as Forge");
        }
    } else if (profile_description.find("neoforge") == std::string::npos) {
        fail("Installer profile does not identify itself as NeoForge");
    }
    if (!profile_version.empty() && !iequals(profile_version, loader_version)) {
        fail("Installer profile does not contain the requested loader version");
    }

    const Json version = parse_json(*version_json, "installer version.json");
    if (!version.is_object()) {
        fail("Installer version.json is not an object");
    }
    const std::string id = json_string(version, "id");
    if (!is_safe_loader_version(id)) {
        fail("Installer version.json has an unsafe or empty id");
    }
    if (json_string(version, "inheritsFrom") != minecraft_version) {
        fail("Installer version.json does not inherit from Minecraft " +
             std::string(minecraft_version));
    }
    if (json_string(version, "mainClass").empty()) {
        fail("Installer version.json has no mainClass");
    }
    const std::string lowered_id = to_lower_ascii(id);
    if (!contains_version_token(lowered_id, to_lower_ascii(std::string(loader_version)))) {
        fail("Installer version.json does not contain the requested loader version");
    }
    if (kind == ForgeKind::Forge) {
        if (lowered_id.find("forge") == std::string::npos ||
            lowered_id.find("neoforge") != std::string::npos) {
            fail("Installer version.json is not a Forge version");
        }
    } else if (lowered_id.find("neoforge") == std::string::npos) {
        fail("Installer version.json is not a NeoForge version");
    }
}

ForgeInstallResult install_forge(const ForgeInstallRequest& request) {
    if (request.game_dir.empty()) {
        fail("A game directory is required to install " + kind_display(request.kind));
    }
    require_safe_minecraft_version(request.minecraft_version);
    const std::string minecraft_version(request.minecraft_version);
    const auto game_dir = absolute_path(request.game_dir);
    ensure_directory(game_dir);

    if (request.kind == ForgeKind::NeoForge && minecraft_version == "1.20.1") {
        fail("NeoForge for Minecraft 1.20.1 is published as net.neoforged:forge "
             "(47.1.x), which is not served by the net.neoforged:neoforge Maven "
             "installer endpoint; this module does not support it");
    }

    std::string loader_version;
    if (!trim(request.loader_version).empty()) {
        loader_version = request.kind == ForgeKind::Forge
                             ? normalize_forge_version(minecraft_version, request.loader_version)
                             : normalize_neoforge_version(request.loader_version);
    } else if (request.kind == ForgeKind::Forge) {
        require_https_url(request.endpoints.forge_promotions_url, "Forge promotions");
        const std::string promotions =
            http_get(request.endpoints.forge_promotions_url);
        const auto selected = select_forge_version(
            promotions,
            minecraft_version,
            std::nullopt,
            request.channel.value_or(ForgeChannel::Recommended));
        if (!selected) {
            fail("No Forge promotion is available for Minecraft " + minecraft_version);
        }
        loader_version = selected->version;
    } else {
        require_https_url(request.endpoints.neoforge_metadata_url, "NeoForge metadata");
        const std::string metadata = http_get(request.endpoints.neoforge_metadata_url);
        const auto versions = parse_neoforge_metadata(metadata);
        const auto selected = select_neoforge_version(versions, minecraft_version);
        if (!selected) {
            fail("No NeoForge version is available for Minecraft " + minecraft_version +
                 " (NeoForge version scheme: <minecraft-minor>.<minecraft-patch>.<build>)");
        }
        loader_version = *selected;
    }
    require_safe_loader_version(loader_version, kind_display(request.kind));
    if (request.kind == ForgeKind::NeoForge) {
        const std::string prefix = neoforge_version_prefix(minecraft_version);
        if (prefix.empty() || loader_version.size() <= prefix.size() ||
            loader_version.compare(0, prefix.size(), prefix) != 0 ||
            std::isdigit(static_cast<unsigned char>(loader_version[prefix.size()])) == 0) {
            fail("NeoForge " + loader_version + " does not target Minecraft " +
                 minecraft_version);
        }
    }

    const std::string installer_url =
        request.kind == ForgeKind::Forge
            ? forge_installer_url(minecraft_version, loader_version, request.endpoints)
            : neoforge_installer_url(loader_version, request.endpoints);
    require_https_url(installer_url, "Installer");

    std::string expected_sha1;
    if (!trim(request.expected_sha1).empty()) {
        expected_sha1 = parse_sha1_sidecar(request.expected_sha1);
    } else {
        std::optional<std::string> sidecar_text;
        try {
            sidecar_text = http_get(sha1_sidecar_url(installer_url));
        } catch (const Error& error) {
            log_warn("SHA-1 sidecar is unavailable for " + installer_url + ": " +
                     error.what());
        }
        if (sidecar_text) {
            expected_sha1 = parse_sha1_sidecar(*sidecar_text);
        } else if (!request.allow_unverified_installer) {
            fail("Refusing to run an unverified third-party installer: no SHA-1 "
                 "sidecar was available for " +
                 installer_url +
                 ". Pin expected_sha1 or explicitly set allow_unverified_installer");
        } else {
            log_warn("Running an unverified third-party installer because "
                     "allow_unverified_installer is set: " +
                     installer_url);
        }
    }

    const std::string installer_name =
        request.kind == ForgeKind::Forge
            ? "forge-" + minecraft_version + "-" + loader_version + "-installer.jar"
            : "neoforge-" + loader_version + "-installer.jar";
    const auto staging_dir = game_dir / ".aurora" / "forge";
    ensure_directory(staging_dir);
    const auto installer_path = staging_dir / path_from_utf8(installer_name);
    require_path_within(game_dir, installer_path, "installer staging");

    if (expected_sha1.empty()) {
        std::error_code remove_error;
        std::filesystem::remove(installer_path, remove_error);
        if (std::filesystem::exists(installer_path, remove_error)) {
            fail("Unable to remove the previous unverified installer: " +
                 path_to_utf8(installer_path));
        }
    }
    http_download(installer_url, installer_path, expected_sha1);
    verify_forge_installer_archive(
        installer_path, request.kind, minecraft_version, loader_version);
    require_regular_file(installer_path, "Installer archive");
    require_path_within(game_dir, installer_path, "installer archive");

    const int required_java = request.required_java_major > 0
                                  ? request.required_java_major
                                  : required_java_major_for(
                                        request.kind, minecraft_version, loader_version);
    std::optional<JavaRuntime> java;
    if (!request.java_home.empty()) {
        java = probe_java_home(request.java_home, "Forge installer");
        if (!java) {
            fail("No usable Java runtime was found at " + path_to_utf8(request.java_home));
        }
        if (java->major < required_java) {
            fail("Java " + std::to_string(required_java) + "+ is required, but " +
                 path_to_utf8(request.java_home) + " is Java " +
                 std::to_string(java->major));
        }
    } else {
        java = find_java_runtime(required_java);
        if (!java) {
            fail("No Java " + std::to_string(required_java) +
                 "+ runtime was found for the Forge installer");
        }
    }

    std::filesystem::path java_executable = java->home / "bin" / "java.exe";
    std::error_code java_error;
    if (!std::filesystem::is_regular_file(java_executable, java_error)) {
        java_executable = java->executable;
    }
    if (!std::filesystem::is_regular_file(java_executable, java_error)) {
        fail("Java executable was not found for " + path_to_utf8(java->home));
    }

    const InstallerCommand command =
        build_installer_command(java_executable, installer_path, game_dir);
    ensure_launcher_profiles(game_dir);

    const auto process_log = staging_dir / path_from_utf8(installer_name + ".stdout.log");
    require_path_within(game_dir, process_log, "installer log");
    log_info("Running " + kind_display(request.kind) + " installer: " +
             command.command_line());
    const int exit_code =
        run_process(command.java, command.arguments, command.working_directory, true, process_log);
    // Both official installers write "<installer file name>.log" relative to
    // their working directory, which is the game directory here.
    const auto installer_log = game_dir / path_from_utf8(installer_name + ".log");
    require_path_within(game_dir, installer_log, "installer log");
    if (exit_code != 0) {
        fail(kind_display(request.kind) + " installer failed with exit code " +
             std::to_string(exit_code) + ". See " + path_to_utf8(installer_log) +
             " and " + path_to_utf8(process_log));
    }

    const std::string expected_id =
        request.kind == ForgeKind::Forge
            ? expected_forge_version_id(minecraft_version, loader_version)
            : expected_neoforge_version_id(loader_version);
    const auto location = find_generated_forge_version(
        game_dir, request.kind, minecraft_version, loader_version, expected_id);
    if (!location) {
        fail(kind_display(request.kind) +
             " installer exited successfully but no matching version JSON was "
             "generated under " +
             path_to_utf8(game_dir / "versions") + ". The installer log is " +
             path_to_utf8(installer_log));
    }

    log_info("Installed " + kind_display(request.kind) + " " + loader_version +
             " as version " + location->id);
    ForgeInstallResult result;
    result.kind = request.kind;
    result.minecraft_version = minecraft_version;
    result.loader_version = loader_version;
    result.version_id = location->id;
    result.version_json = location->json_path;
    result.installer_jar = installer_path;
    result.installer_log = installer_log;
    result.process_log = process_log;
    result.sha1_verified = !expected_sha1.empty();
    result.installer_exit_code = exit_code;
    return result;
}

} // namespace aurora
