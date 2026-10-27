#pragma once

#include "core/java.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace aurora {

inline constexpr std::string_view kAdoptiumLatestBaseUrl =
    "https://api.adoptium.net/v3/assets/latest";

struct JavaPackage {
    std::string link;
    std::string name;
    std::string checksum;
    std::string checksum_link;
    std::uint64_t size = 0;
    std::string release_name;
    std::string version;
    int major = 0;
    std::string image_type;
    std::string os;
    std::string arch;
    std::string vendor;
    std::string jvm_impl;
};

struct JavaInstallOptions {
    int feature_version = 21;
    bool force = false;
    std::filesystem::path install_dir;
    std::string api_url;
};

// Overridable for tests and future mirrors. The default implementation uses
// WinHTTP through http_get/http_download and probe_java_home for validation.
struct JavaInstallHooks {
    std::function<std::string(std::string_view)> fetch;
    std::function<void(std::string_view, const std::filesystem::path&)> download;
    std::function<std::optional<JavaRuntime>(const std::filesystem::path&, std::string)> probe;
};

std::string adoptium_asset_url(int feature_version,
                               std::string_view base_url = kAdoptiumLatestBaseUrl);

std::vector<JavaPackage> parse_adoptium_packages(std::string_view json);
std::optional<JavaPackage> select_adoptium_package(const std::vector<JavaPackage>& packages,
                                                   int feature_version = 0);
std::optional<JavaPackage> parse_and_select_adoptium_package(std::string_view json,
                                                             int feature_version = 0);

std::filesystem::path default_java_install_dir(const std::filesystem::path& game_dir);

// Returns true when candidate is inside root (or equal to it) after resolving
// relative components. Path comparison is case-insensitive on Windows.
bool is_path_within(const std::filesystem::path& root,
                    const std::filesystem::path& candidate);

JavaRuntime install_java_runtime(const std::filesystem::path& game_dir,
                                 const JavaInstallOptions& options = {},
                                 const JavaInstallHooks& hooks = {});

} // namespace aurora
