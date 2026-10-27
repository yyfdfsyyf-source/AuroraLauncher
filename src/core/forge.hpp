#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace aurora {

enum class ForgeKind { Forge, NeoForge };

enum class ForgeChannel { Recommended, Latest };

// Endpoints are injectable so that callers, mirrors and tests can replace
// them. They are only used by the query and install entry points; the pure
// URL builders below never perform network access.
struct ForgeEndpoints {
    std::string forge_promotions_url =
        "https://files.minecraftforge.net/net/minecraftforge/forge/promotions_slim.json";
    std::string neoforge_metadata_url =
        "https://maven.neoforged.net/releases/net/neoforged/neoforge/maven-metadata.xml";
    std::string forge_maven_root = "https://maven.minecraftforge.net/";
    std::string neoforge_maven_root = "https://maven.neoforged.net/releases/";
};

struct ForgePromotion {
    std::string minecraft;
    std::string version;
    ForgeChannel channel = ForgeChannel::Latest;
    bool explicit_version = false;
};

// Forge promotions contain the two official channels per Minecraft version.
// The returned list only contains entries for the requested Minecraft version.
std::vector<ForgePromotion> parse_forge_promotions(std::string_view promotions_json,
                                                   std::string_view minecraft_version);

// If explicit_version is set it is normalised and returned without consulting
// the promotions document. Otherwise the preferred channel is selected and
// the other channel is used as a fallback when the preferred one is absent.
std::optional<ForgePromotion> select_forge_version(
    std::string_view promotions_json,
    std::string_view minecraft_version,
    std::optional<std::string> explicit_version = std::nullopt,
    ForgeChannel preferred_channel = ForgeChannel::Recommended);

// NeoForge Maven metadata. The parser is intentionally limited to the
// <version> entries inside <versions>; it does not implement general XML.
std::vector<std::string> parse_neoforge_metadata(std::string_view xml);

// NeoForge versions use <minecraft-minor>.<minecraft-patch>.<build>, for
// example Minecraft 1.20.2 -> 20.2.x and 1.21.1 -> 21.1.x. Minecraft 1.20.1
// is a special case: NeoForge published that release as net.neoforged:forge
// (47.1.x), which is not part of this Maven endpoint and is therefore not
// selected here. Year-based versions such as 26.1 use the same prefix rule.
std::string neoforge_version_prefix(std::string_view minecraft_version);
std::vector<std::string> select_neoforge_versions(const std::vector<std::string>& versions,
                                                  std::string_view minecraft_version);
std::optional<std::string> select_neoforge_version(const std::vector<std::string>& versions,
                                                   std::string_view minecraft_version);

std::string normalize_forge_version(std::string_view minecraft_version,
                                    std::string_view forge_version);
std::string normalize_neoforge_version(std::string_view neoforge_version);
bool is_safe_loader_version(std::string_view version);
bool is_safe_minecraft_version(std::string_view version);

// Baseline is Java 17. Minecraft 1.20.5+ and NeoForge 21+ use Java 21.
// Year-based Minecraft versions are treated as Java 21 by default because the
// actual requirement is not known here; callers can override the value in
// ForgeInstallRequest.
int required_java_major_for(ForgeKind kind,
                            std::string_view minecraft_version,
                            std::string_view loader_version);

std::string forge_installer_url(std::string_view minecraft_version,
                                std::string_view forge_version,
                                const ForgeEndpoints& endpoints = {});
std::string neoforge_installer_url(std::string_view neoforge_version,
                                   const ForgeEndpoints& endpoints = {});
std::string sha1_sidecar_url(std::string_view artifact_url);

// Parses the first whitespace-delimited token from a Maven .sha1 sidecar and
// verifies that it is a 40-character hexadecimal SHA-1.
std::string parse_sha1_sidecar(std::string_view content);

std::string expected_forge_version_id(std::string_view minecraft_version,
                                      std::string_view forge_version);
std::string expected_neoforge_version_id(std::string_view neoforge_version);

struct InstallerCommand {
    std::filesystem::path java;
    std::vector<std::string> arguments;
    std::filesystem::path working_directory;

    std::string command_line() const;
};

// Builds: java -Djava.awt.headless=true -Dfile.encoding=UTF-8 -jar
//         <installer.jar> --installClient <game_dir>
// The command line is only used for logging; run_process receives argv
// directly. The working directory is the game directory.
InstallerCommand build_installer_command(const std::filesystem::path& java_executable,
                                         const std::filesystem::path& installer_jar,
                                         const std::filesystem::path& game_dir);

struct ForgeVersionLocation {
    std::string id;
    std::filesystem::path json_path;
};

// Scans <game>/versions/*/*.json and returns the newest JSON whose
// inheritsFrom, mainClass, loader token and loader version match. An exact
// expected_version_id is preferred, but legacy Forge ids are also accepted
// when the expected id is not present.
std::optional<ForgeVersionLocation> find_generated_forge_version(
    const std::filesystem::path& game_dir,
    ForgeKind kind,
    std::string_view minecraft_version,
    std::string_view loader_version,
    std::string_view expected_version_id = {});

// Verifies that path is a regular (non-symlink) JAR containing an official
// installer layout: META-INF/MANIFEST.MF (with an installer main class),
// install_profile.json (for the requested Minecraft/loader version) and
// version.json (inheriting from the requested Minecraft version).
void verify_forge_installer_archive(const std::filesystem::path& path,
                                    ForgeKind kind,
                                    std::string_view minecraft_version,
                                    std::string_view loader_version);

struct ForgeInstallRequest {
    ForgeKind kind = ForgeKind::Forge;
    std::filesystem::path game_dir;
    std::string minecraft_version;
    // Empty means: select the recommended Forge promotion or the newest
    // matching NeoForge Maven version.
    std::string loader_version;
    // Forge only; empty means Recommended.
    std::optional<ForgeChannel> channel;
    // Optional caller-pinned SHA-1. When set, the Maven sidecar is not fetched.
    std::string expected_sha1;
    // Refuse to execute the third-party installer when no SHA-1 could be
    // obtained unless this is explicitly enabled.
    bool allow_unverified_installer = false;
    // 0 means derive from the Minecraft/loader versions.
    int required_java_major = 0;
    std::filesystem::path java_home;
    ForgeEndpoints endpoints;
};

struct ForgeInstallResult {
    ForgeKind kind = ForgeKind::Forge;
    std::string minecraft_version;
    std::string loader_version;
    std::string version_id;
    std::filesystem::path version_json;
    std::filesystem::path installer_jar;
    std::filesystem::path installer_log;
    std::filesystem::path process_log;
    bool sha1_verified = false;
    int installer_exit_code = 0;
};

// Explicit installation entry point. It downloads and verifies the official
// installer, runs it headlessly against game_dir and locates the generated
// version JSON. No network access happens during launch.
ForgeInstallResult install_forge(const ForgeInstallRequest& request);

} // namespace aurora
