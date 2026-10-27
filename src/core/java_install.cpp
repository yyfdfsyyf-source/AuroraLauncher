#include "core/java_install.hpp"

#include "core/error.hpp"
#include "core/hash.hpp"
#include "core/http.hpp"
#include "core/json.hpp"
#include "core/log.hpp"
#include "core/paths.hpp"
#include "core/strings.hpp"
#include "core/zip.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <initializer_list>
#include <system_error>
#include <utility>

namespace aurora {

namespace {

using JavaProbe =
    std::function<std::optional<JavaRuntime>(const std::filesystem::path&, std::string)>;

bool is_https_url(std::string_view url) {
    return url.size() >= 8 && to_lower_ascii(std::string(url.substr(0, 8))) == "https://";
}

bool is_zip_name(std::string name) {
    const auto query = name.find_first_of("?#");
    if (query != std::string::npos) {
        name.erase(query);
    }
    name = to_lower_ascii(std::move(name));
    return name.size() >= 4 && name.compare(name.size() - 4, 4, ".zip") == 0;
}

std::string url_filename(const std::string& url) {
    std::string value = url;
    const auto query = value.find_first_of("?#");
    if (query != std::string::npos) {
        value.erase(query);
    }
    const auto slash = value.find_last_of('/');
    if (slash != std::string::npos) {
        value.erase(0, slash + 1);
    }
    return value;
}

std::string file_stem(std::string name) {
    const auto slash = name.find_last_of("/\\");
    if (slash != std::string::npos) {
        name.erase(0, slash + 1);
    }
    const auto dot = name.find_last_of('.');
    if (dot != std::string::npos && dot > 0) {
        name.erase(dot);
    }
    return name;
}

int parse_major_version(std::string_view value) {
    const std::string text(value);
    if (text.rfind("1.", 0) == 0) {
        int major = 0;
        for (std::size_t i = 2; i < text.size() && text[i] >= '0' && text[i] <= '9'; ++i) {
            major = major * 10 + (text[i] - '0');
        }
        if (major > 0) {
            return major;
        }
    }

    std::size_t start = 0;
    while (start < text.size() && (text[start] < '0' || text[start] > '9')) {
        ++start;
    }
    int major = 0;
    for (std::size_t i = start; i < text.size() && text[i] >= '0' && text[i] <= '9'; ++i) {
        major = major * 10 + (text[i] - '0');
    }
    return major;
}

const Json* json_object_any(const Json& object, std::initializer_list<const char*> keys) {
    for (const char* key : keys) {
        if (const Json* value = json_object(object, key)) {
            return value;
        }
    }
    return nullptr;
}

std::string json_string_any(const Json& object, std::initializer_list<const char*> keys) {
    for (const char* key : keys) {
        if (const auto value = json_optional_string(object, key); value && !value->empty()) {
            return *value;
        }
    }
    return {};
}

std::int64_t json_integer_any(const Json& object, std::initializer_list<const char*> keys) {
    if (!object.is_object()) {
        return 0;
    }
    for (const char* key : keys) {
        const auto it = object.find(key);
        if (it != object.end() && it->is_number_integer()) {
            return it->get<std::int64_t>();
        }
    }
    return 0;
}

std::optional<JavaPackage> parse_package_entry(const Json& asset, const Json* inherited_binary) {
    if (!asset.is_object()) {
        return std::nullopt;
    }

    const Json* binary = json_object(asset, "binary");
    if (binary == nullptr) {
        binary = inherited_binary;
    }
    const Json* package = binary == nullptr ? nullptr : json_object(*binary, "package");
    if (package == nullptr) {
        package = json_object(asset, "package");
    }
    if (package == nullptr) {
        return std::nullopt;
    }

    JavaPackage result;
    result.link = json_string_any(*package, {"link", "url"});
    if (result.link.empty()) {
        return std::nullopt;
    }
    result.name = json_string_any(*package, {"name", "filename"});
    if (result.name.empty()) {
        result.name = url_filename(result.link);
    }
    result.checksum = json_string_any(*package, {"checksum", "sha256"});
    result.checksum_link = json_string_any(*package, {"checksum_link", "checksum_url"});
    const auto size = json_integer_any(*package, {"size"});
    if (size > 0) {
        result.size = static_cast<std::uint64_t>(size);
    }

    if (binary != nullptr) {
        result.os = json_string_any(*binary, {"os"});
        result.arch = json_string_any(*binary, {"architecture", "arch"});
        result.image_type = json_string_any(*binary, {"image_type", "imageType"});
        result.jvm_impl = json_string_any(*binary, {"jvm_impl", "jvmImpl"});
        result.vendor = json_string_any(*binary, {"vendor"});
        result.release_name = json_string_any(*binary, {"release_name", "releaseName"});
    }
    if (result.os.empty()) {
        result.os = json_string_any(*package, {"os"});
    }
    if (result.arch.empty()) {
        result.arch = json_string_any(*package, {"architecture", "arch"});
    }
    if (result.image_type.empty()) {
        result.image_type = json_string_any(*package, {"image_type", "imageType"});
    }
    if (result.vendor.empty()) {
        result.vendor = json_string_any(asset, {"vendor"});
    }
    if (result.release_name.empty()) {
        result.release_name = json_string_any(asset, {"release_name", "releaseName"});
    }

    if (const Json* version = json_object_any(asset, {"version", "version_data", "versionData"})) {
        result.version = json_string_any(
            *version, {"semver", "openjdk_version", "openjdkVersion", "name"});
        const auto major =
            json_integer_any(*version, {"major", "major_version", "majorVersion"});
        if (major > 0) {
            result.major = static_cast<int>(major);
        }
        if (result.release_name.empty()) {
            result.release_name =
                json_string_any(*version, {"release_name", "releaseName"});
        }
    }
    if (binary != nullptr) {
        if (result.version.empty()) {
            if (const Json* binary_version =
                    json_object_any(*binary, {"version", "version_data", "versionData"})) {
                result.version = json_string_any(
                    *binary_version, {"semver", "openjdk_version", "openjdkVersion", "name"});
                const auto major = json_integer_any(
                    *binary_version, {"major", "major_version", "majorVersion"});
                if (major > 0) {
                    result.major = static_cast<int>(major);
                }
            } else if (const auto value = json_optional_string(*binary, "version")) {
                result.version = *value;
            }
        }
        if (result.major <= 0) {
            const auto major = json_integer_any(*binary, {"major", "major_version"});
            if (major > 0) {
                result.major = static_cast<int>(major);
            }
        }
    }
    if (result.version.empty()) {
        result.version = json_string_any(*package, {"version"});
    }
    if (result.version.empty()) {
        if (const auto value = json_optional_string(asset, "version")) {
            result.version = *value;
        }
    }
    if (result.image_type.empty()) {
        const std::string lower_name = to_lower_ascii(result.name);
        if (lower_name.find("jdk") != std::string::npos) {
            result.image_type = "jdk";
        } else if (lower_name.find("jre") != std::string::npos) {
            result.image_type = "jre";
        }
    }
    if (result.release_name.empty()) {
        result.release_name = json_string_any(*package, {"release_name", "releaseName"});
    }
    if (result.release_name.empty()) {
        result.release_name = file_stem(result.name);
    }
    if (result.major <= 0) {
        result.major = parse_major_version(result.version);
    }
    if (result.major <= 0) {
        result.major = parse_major_version(result.release_name);
    }
    return result;
}

bool is_windows_os(std::string_view os) {
    return os.empty() || iequals(os, "windows");
}

bool is_x64_arch(std::string_view arch) {
    return arch.empty() || iequals(arch, "x64") || iequals(arch, "x86_64") ||
           iequals(arch, "amd64");
}

bool is_supported_image(std::string_view image) {
    return image.empty() || iequals(image, "jdk") || iequals(image, "jre");
}

bool is_supported_package(const JavaPackage& package) {
    if (!is_https_url(package.link)) {
        return false;
    }
    if (!is_zip_name(package.name.empty() ? package.link : package.name)) {
        return false;
    }
    if (!is_windows_os(package.os) || !is_x64_arch(package.arch) ||
        !is_supported_image(package.image_type)) {
        return false;
    }
    return true;
}

int package_score(const JavaPackage& package, int feature_version) {
    int score = 0;
    if (iequals(package.image_type, "jdk")) {
        score += 100;
    } else if (iequals(package.image_type, "jre")) {
        score += 20;
    }
    if (iequals(package.os, "windows")) {
        score += 30;
    } else if (package.os.empty()) {
        score += 1;
    }
    if (is_x64_arch(package.arch) && !package.arch.empty()) {
        score += 30;
    } else if (package.arch.empty()) {
        score += 1;
    }
    if (is_zip_name(package.name.empty() ? package.link : package.name)) {
        score += 20;
    }
    if (iequals(package.jvm_impl, "hotspot")) {
        score += 5;
    }
    if (iequals(package.vendor, "eclipse")) {
        score += 4;
    }
    if (!package.checksum.empty()) {
        score += 4;
    }
    if (!package.checksum_link.empty()) {
        score += 1;
    }
    if (package.size > 0) {
        score += 1;
    }
    if (feature_version > 0 && package.major == feature_version) {
        score += 10;
    }
    return score;
}

std::filesystem::path normalized_absolute_path(const std::filesystem::path& value) {
    std::error_code ec;
    std::filesystem::path result = std::filesystem::absolute(value, ec);
    if (ec) {
        result = value;
        ec.clear();
    }
    const auto canonical = std::filesystem::weakly_canonical(result, ec);
    if (!ec) {
        result = canonical;
    }
    return result.lexically_normal();
}

bool path_component_equal(const std::filesystem::path& left,
                          const std::filesystem::path& right) {
#ifdef _WIN32
    return iequals(path_to_utf8(left), path_to_utf8(right));
#else
    return left == right;
#endif
}

std::string install_folder_name(const JavaPackage& package, int feature_version) {
    std::string value = trim(package.release_name);
    if (value.empty()) {
        value = file_stem(package.name);
    }
    if (value.empty()) {
        value = "jdk-" + std::to_string(feature_version);
    }
    if (value.empty() || value == "." || value == ".." || value.front() == '.') {
        fail("Java package has an unsafe release name: " + package.release_name);
    }
    if (value.find_first_of("/\\:") != std::string::npos) {
        fail("Java package has an unsafe release name: " + package.release_name);
    }
    for (const char character : value) {
        const auto byte = static_cast<unsigned char>(character);
        if (byte < 0x20 || character == '<' || character == '>' || character == '"' ||
            character == '|' || character == '?' || character == '*') {
            fail("Java package has an unsafe release name: " + package.release_name);
        }
    }
    if (value.back() == '.' || value.back() == ' ') {
        fail("Java package has an unsafe release name: " + package.release_name);
    }
    return value;
}

bool has_java_executable(const std::filesystem::path& home) {
    std::error_code ec;
#ifdef _WIN32
    return std::filesystem::is_regular_file(home / "bin" / "java.exe", ec) ||
           std::filesystem::is_regular_file(home / "bin" / "javaw.exe", ec);
#else
    return std::filesystem::is_regular_file(home / "bin" / "java", ec);
#endif
}

std::optional<std::filesystem::path> find_extracted_java_home(
    const std::filesystem::path& extracted,
    const std::string& expected_name) {
    if (has_java_executable(extracted)) {
        return extracted;
    }

    std::vector<std::filesystem::path> candidates;
    std::error_code ec;
    for (std::filesystem::directory_iterator iterator(extracted, ec);
         !ec && iterator != std::filesystem::directory_iterator();
         iterator.increment(ec)) {
        std::error_code type_error;
        if (!iterator->is_directory(type_error) || type_error) {
            continue;
        }
        if (has_java_executable(iterator->path())) {
            candidates.push_back(iterator->path());
        }
    }
    if (ec) {
        fail("Unable to inspect extracted Java archive: " + path_to_utf8(extracted));
    }
    if (candidates.empty()) {
        return std::nullopt;
    }
    if (candidates.size() == 1) {
        return candidates.front();
    }

    for (const auto& candidate : candidates) {
        if (iequals(path_to_utf8(candidate.filename()), expected_name)) {
            return candidate;
        }
    }
    std::vector<std::filesystem::path> jdk_candidates;
    for (const auto& candidate : candidates) {
        const std::string name = to_lower_ascii(path_to_utf8(candidate.filename()));
        if (name.find("jdk") != std::string::npos || name.find("jre") != std::string::npos) {
            jdk_candidates.push_back(candidate);
        }
    }
    if (jdk_candidates.size() == 1) {
        return jdk_candidates.front();
    }

    std::vector<std::string> names;
    names.reserve(candidates.size());
    for (const auto& candidate : candidates) {
        names.push_back(path_to_utf8(candidate.filename()));
    }
    fail("Java archive contains multiple possible runtimes: " + join(names, ", "));
}

std::string unique_suffix() {
    static std::atomic<std::uint64_t> counter{0};
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::to_string(static_cast<std::uint64_t>(stamp)) + "-" +
           std::to_string(counter.fetch_add(1));
}

void remove_tree_quiet(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
    if (ec) {
        log_warn("Unable to remove temporary path: " + path_to_utf8(path) + " (" +
                 ec.message() + ")");
    }
}

class StagingDirectory {
public:
    explicit StagingDirectory(std::filesystem::path path) : path_(std::move(path)) {}

    ~StagingDirectory() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    StagingDirectory(const StagingDirectory&) = delete;
    StagingDirectory& operator=(const StagingDirectory&) = delete;

private:
    std::filesystem::path path_;
};

class BackupGuard {
public:
    BackupGuard(std::filesystem::path target, std::filesystem::path backup)
        : target_(std::move(target)), backup_(std::move(backup)) {}

    ~BackupGuard() {
        if (committed_) {
            return;
        }
        if (installed_) {
            std::error_code ec;
            std::filesystem::remove_all(target_, ec);
        }
        if (!backup_.empty()) {
            std::error_code exists_error;
            if (std::filesystem::exists(backup_, exists_error)) {
                std::error_code rename_error;
                std::filesystem::rename(backup_, target_, rename_error);
                if (rename_error) {
                    log_warn("Unable to restore previous Java runtime from " +
                             path_to_utf8(backup_) + " (" + rename_error.message() + ")");
                }
            }
        }
    }

    BackupGuard(const BackupGuard&) = delete;
    BackupGuard& operator=(const BackupGuard&) = delete;

    void mark_installed() {
        installed_ = true;
    }

    void commit() {
        committed_ = true;
        if (!backup_.empty()) {
            remove_tree_quiet(backup_);
        }
    }

private:
    std::filesystem::path target_;
    std::filesystem::path backup_;
    bool installed_ = false;
    bool committed_ = false;
};

std::optional<JavaRuntime> find_existing_runtime(const std::filesystem::path& install_root,
                                                 int feature_version,
                                                 const JavaProbe& probe) {
    std::error_code ec;
    if (!std::filesystem::is_directory(install_root, ec)) {
        return std::nullopt;
    }

    std::vector<std::filesystem::path> directories;
    for (std::filesystem::directory_iterator iterator(install_root, ec);
         !ec && iterator != std::filesystem::directory_iterator();
         iterator.increment(ec)) {
        std::error_code type_error;
        if (!iterator->is_directory(type_error) || type_error) {
            continue;
        }
        const std::string name = path_to_utf8(iterator->path().filename());
        if (!name.empty() && name.front() == '.') {
            continue;
        }
        directories.push_back(iterator->path());
    }
    std::sort(directories.begin(), directories.end());
    for (const auto& directory : directories) {
        auto runtime = probe(directory, "downloaded");
        if (runtime && runtime->major == feature_version) {
            return runtime;
        }
    }
    return std::nullopt;
}

bool is_hex_character(char character) {
    return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f') ||
           (character >= 'A' && character <= 'F');
}

std::string find_sha256(std::string_view text) {
    for (std::size_t start = 0; start + 64 <= text.size(); ++start) {
        bool matches = true;
        for (std::size_t i = start; i < start + 64; ++i) {
            if (!is_hex_character(text[i])) {
                matches = false;
                break;
            }
        }
        if (matches) {
            return to_lower_ascii(std::string(text.substr(start, 64)));
        }
    }
    return {};
}

} // namespace

std::string adoptium_asset_url(int feature_version, std::string_view base_url) {
    if (feature_version <= 0) {
        fail("Java feature version must be positive");
    }
    std::string base(base_url);
    while (!base.empty() && base.back() == '/') {
        base.pop_back();
    }
    if (base.empty()) {
        fail("Adoptium API base URL is empty");
    }
    return base + "/" + std::to_string(feature_version) +
           "/hotspot?architecture=x64&image_type=jdk&os=windows&vendor=eclipse";
}

std::vector<JavaPackage> parse_adoptium_packages(std::string_view json) {
    const Json root = parse_json(json, "Adoptium API response");
    std::vector<JavaPackage> packages;

    const auto add = [&packages](const Json& asset, const Json* binary) {
        if (auto package = parse_package_entry(asset, binary)) {
            packages.push_back(std::move(*package));
        }
    };
    const auto add_object = [&add](const Json& object) {
        if (!object.is_object()) {
            return;
        }
        if (const Json* binaries = json_array(object, "binaries")) {
            for (const Json& binary : *binaries) {
                add(object, &binary);
            }
            return;
        }
        add(object, nullptr);
    };

    if (root.is_array()) {
        for (const Json& entry : root) {
            add_object(entry);
        }
    } else if (root.is_object()) {
        if (const Json* data = json_array(root, "data")) {
            for (const Json& entry : *data) {
                add_object(entry);
            }
        } else {
            add_object(root);
        }
    } else {
        fail("Adoptium API response has an unexpected JSON shape");
    }
    return packages;
}

std::optional<JavaPackage> select_adoptium_package(const std::vector<JavaPackage>& packages,
                                                   int feature_version) {
    const JavaPackage* best = nullptr;
    int best_score = 0;
    for (const JavaPackage& package : packages) {
        if (!is_supported_package(package)) {
            continue;
        }
        const int score = package_score(package, feature_version);
        if (best == nullptr || score > best_score) {
            best = &package;
            best_score = score;
        }
    }
    if (best == nullptr) {
        return std::nullopt;
    }
    return *best;
}

std::optional<JavaPackage> parse_and_select_adoptium_package(std::string_view json,
                                                             int feature_version) {
    return select_adoptium_package(parse_adoptium_packages(json), feature_version);
}

std::filesystem::path default_java_install_dir(const std::filesystem::path& game_dir) {
    return normalized_absolute_path(game_dir) / "runtime" / "java";
}

bool is_path_within(const std::filesystem::path& root,
                    const std::filesystem::path& candidate) {
    if (root.empty() || candidate.empty()) {
        return false;
    }
    const auto normalized_root = normalized_absolute_path(root);
    const auto normalized_candidate = normalized_absolute_path(candidate);
    auto root_it = normalized_root.begin();
    auto candidate_it = normalized_candidate.begin();
    for (; root_it != normalized_root.end(); ++root_it, ++candidate_it) {
        if (candidate_it == normalized_candidate.end() ||
            !path_component_equal(*root_it, *candidate_it)) {
            return false;
        }
    }
    return true;
}

JavaRuntime install_java_runtime(const std::filesystem::path& game_dir,
                                 const JavaInstallOptions& options,
                                 const JavaInstallHooks& hooks) {
    if (game_dir.empty()) {
        fail("Game directory is empty");
    }
    if (options.feature_version <= 0) {
        fail("Java feature version must be positive");
    }

    const auto game_root = normalized_absolute_path(game_dir);
    const auto install_root = options.install_dir.empty()
        ? default_java_install_dir(game_root)
        : normalized_absolute_path(options.install_dir);
    if (!is_path_within(game_root, install_root)) {
        fail("Java install directory must be inside the game directory: " +
             path_to_utf8(install_root));
    }

    const JavaProbe probe = [&hooks](const std::filesystem::path& home, std::string source) {
        if (hooks.probe) {
            return hooks.probe(home, std::move(source));
        }
        return probe_java_home(home, std::move(source));
    };

    if (!options.force) {
        if (auto existing = find_existing_runtime(install_root, options.feature_version, probe)) {
            log_info("Using existing Java " + std::to_string(existing->major) + " runtime at " +
                     path_to_utf8(existing->home));
            return *existing;
        }
    }

    const std::string api_url = options.api_url.empty()
        ? adoptium_asset_url(options.feature_version)
        : options.api_url;
    if (api_url.empty()) {
        fail("Adoptium API URL is empty");
    }
    log_info("Requesting Adoptium Java " + std::to_string(options.feature_version) +
             " metadata from " + api_url);
    const std::string response =
        hooks.fetch ? hooks.fetch(api_url) : http_get(api_url);
    const auto selected = parse_and_select_adoptium_package(response, options.feature_version);
    if (!selected) {
        fail("Adoptium did not return a usable Windows x64 JDK package for Java " +
             std::to_string(options.feature_version));
    }
    const JavaPackage& package = *selected;
    if (!is_https_url(package.link)) {
        fail("Refusing to download Java from a non-HTTPS URL: " + package.link);
    }

    const std::string folder_name =
        install_folder_name(package, options.feature_version);
    const auto target = install_root / path_from_utf8(folder_name);
    if (target == install_root || !is_path_within(install_root, target) ||
        !is_path_within(game_root, target)) {
        fail("Java runtime target escapes the install directory: " + path_to_utf8(target));
    }

    std::error_code ec;
    const bool target_exists = std::filesystem::exists(target, ec);
    if (ec) {
        fail("Unable to inspect Java runtime target: " + path_to_utf8(target));
    }
    if (target_exists && !options.force) {
        const auto existing = probe(target, "downloaded");
        if (existing && existing->major == options.feature_version) {
            return *existing;
        }
        fail("A Java runtime directory already exists at " + path_to_utf8(target) +
             " but it is not a usable Java " + std::to_string(options.feature_version) +
             " runtime. Use force to replace it.");
    }

    ensure_directory(install_root);
    const auto staging =
        install_root / path_from_utf8(".staging-" + unique_suffix());
    if (!is_path_within(install_root, staging) || !is_path_within(game_root, staging)) {
        fail("Java staging directory escapes the install directory");
    }
    ensure_directory(staging);
    StagingDirectory staging_guard(staging);

    std::string expected_sha256 = find_sha256(package.checksum);
    if (!package.checksum.empty() && expected_sha256.empty()) {
        fail("Adoptium returned an invalid SHA-256 checksum for " + package.name);
    }
    if (expected_sha256.empty() && !package.checksum_link.empty()) {
        if (is_https_url(package.checksum_link)) {
            try {
                const std::string checksum_text =
                    hooks.fetch ? hooks.fetch(package.checksum_link)
                                : http_get(package.checksum_link);
                expected_sha256 = find_sha256(checksum_text);
                if (expected_sha256.empty()) {
                    log_warn("Adoptium checksum response did not contain a SHA-256 digest");
                }
            } catch (const std::exception& e) {
                log_warn(std::string("Unable to fetch the Java package checksum: ") + e.what());
            }
        } else {
            log_warn("Ignoring non-HTTPS Java package checksum URL: " + package.checksum_link);
        }
    }

    const auto archive = staging / "package.zip";
    log_info("Downloading Java package " + package.name + " (" +
             format_bytes(package.size) + ")");
    if (hooks.download) {
        hooks.download(package.link, archive);
    } else {
        http_download(package.link, archive);
    }
    if (!std::filesystem::is_regular_file(archive, ec)) {
        fail("Java package download did not produce a file: " + path_to_utf8(archive));
    }
    if (package.size > 0) {
        const auto actual_size = std::filesystem::file_size(archive, ec);
        if (ec) {
            fail("Unable to read the downloaded Java package size: " + path_to_utf8(archive));
        }
        if (actual_size != package.size) {
            fail("Java package size mismatch: expected " + std::to_string(package.size) +
                 " bytes, got " + std::to_string(actual_size));
        }
    }
    if (!expected_sha256.empty()) {
        const std::string actual_sha256 = sha256_file(archive);
        if (actual_sha256 != expected_sha256) {
            fail("SHA-256 mismatch for " + package.name + " (expected " + expected_sha256 +
                 ", got " + actual_sha256 + ")");
        }
    } else {
        log_warn("Java package has no SHA-256 checksum; only its size was verified");
    }

    const auto extracted = staging / "extracted";
    ensure_directory(extracted);
    extract_zip(archive, extracted);
    std::filesystem::remove(archive, ec);

    const auto extracted_home = find_extracted_java_home(extracted, folder_name);
    if (!extracted_home) {
        fail("Downloaded Java archive does not contain bin/java.exe");
    }
    if (!is_path_within(staging, *extracted_home)) {
        fail("Extracted Java runtime escapes the staging directory");
    }
    const auto staged_runtime = probe(*extracted_home, "downloaded");
    if (!staged_runtime) {
        fail("Downloaded Java archive does not contain a usable Java runtime");
    }
    if (staged_runtime->major != options.feature_version) {
        fail("Downloaded Java runtime is version " + std::to_string(staged_runtime->major) +
             ", expected " + std::to_string(options.feature_version));
    }

    std::filesystem::path backup;
    if (target_exists) {
        backup = install_root / path_from_utf8(".backup-" + unique_suffix());
        if (!is_path_within(install_root, backup) || !is_path_within(game_root, backup)) {
            fail("Java backup directory escapes the install directory");
        }
        std::filesystem::rename(target, backup, ec);
        if (ec) {
            fail("Unable to move the existing Java runtime aside: " + path_to_utf8(target) +
                 " (" + ec.message() + ")");
        }
    }

    BackupGuard backup_guard(target, backup);
    std::filesystem::rename(*extracted_home, target, ec);
    if (ec) {
        fail("Unable to move the Java runtime into place: " + path_to_utf8(target) + " (" +
             ec.message() + ")");
    }
    backup_guard.mark_installed();

    auto runtime = probe(target, "downloaded");
    if (!runtime) {
        fail("Installed Java runtime could not be probed: " + path_to_utf8(target));
    }
    if (runtime->major != options.feature_version) {
        fail("Installed Java runtime is version " + std::to_string(runtime->major) +
             ", expected " + std::to_string(options.feature_version));
    }
    backup_guard.commit();
    log_info("Installed Java " + std::to_string(runtime->major) + " runtime at " +
             path_to_utf8(runtime->home));
    return *runtime;
}

} // namespace aurora
