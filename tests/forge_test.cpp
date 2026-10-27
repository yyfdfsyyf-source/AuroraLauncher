#include "core/forge.hpp"

#include "core/error.hpp"
#include "core/paths.hpp"
#include "core/process.hpp"
#include "core/strings.hpp"

#include <miniz.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {

using aurora::ForgeChannel;
using aurora::ForgeKind;

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

template <typename Fn>
void expect_throws(Fn&& fn, const std::string& message) {
    try {
        fn();
    } catch (const aurora::Error&) {
        return;
    } catch (const std::exception& error) {
        ++failures;
        std::cerr << "FAIL: " << message << " threw an unexpected exception: " << error.what()
                  << '\n';
        return;
    }
    ++failures;
    std::cerr << "FAIL: " << message << " did not throw\n";
}

template <typename Fn>
void expect_no_throw(Fn&& fn, const std::string& message) {
    try {
        fn();
    } catch (const std::exception& error) {
        ++failures;
        std::cerr << "FAIL: " << message << " threw: " << error.what() << '\n';
    }
}

std::filesystem::path make_temp_dir(const std::string& name) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
                      ("aurora-forge-tests-" + name + "-" + std::to_string(stamp));
    std::filesystem::create_directories(path);
    return path;
}

// Creating a symbolic link is not permitted for ordinary users unless
// Developer Mode is enabled. Probing that with std::filesystem can block for
// tens of seconds on some systems, so check the capability up front.
bool symlink_creation_is_available() {
#ifdef _WIN32
    DWORD developer_mode = 0;
    DWORD size = sizeof(developer_mode);
    if (RegGetValueW(
            HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\AppModelUnlock",
            L"AllowDevelopmentWithoutDevLicense",
            RRF_RT_REG_DWORD,
            nullptr,
            &developer_mode,
            &size) == ERROR_SUCCESS &&
        developer_mode == 1) {
        return true;
    }

    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return false;
    }
    TOKEN_ELEVATION elevation{};
    size = sizeof(elevation);
    const bool elevated =
        GetTokenInformation(token, TokenElevation, &elevation, size, &size) != 0 &&
        elevation.TokenIsElevated != 0;
    CloseHandle(token);
    return elevated;
#else
    return true;
#endif
}

struct ZipEntry {
    std::string name;
    std::string content;
};

bool write_zip(const std::filesystem::path& path, const std::vector<ZipEntry>& entries) {
    mz_zip_archive archive{};
    if (!mz_zip_writer_init_heap(&archive, 0, 0)) {
        return false;
    }
    for (const ZipEntry& entry : entries) {
        if (!mz_zip_writer_add_mem(
                &archive,
                entry.name.c_str(),
                entry.content.data(),
                entry.content.size(),
                MZ_NO_COMPRESSION)) {
            mz_zip_writer_end(&archive);
            return false;
        }
    }

    void* buffer = nullptr;
    size_t size = 0;
    if (!mz_zip_writer_finalize_heap_archive(&archive, &buffer, &size)) {
        mz_zip_writer_end(&archive);
        return false;
    }
    mz_zip_writer_end(&archive);

    bool written = false;
    if (buffer != nullptr && size != 0) {
        try {
            aurora::write_text_file(
                path,
                std::string_view(static_cast<const char*>(buffer), size));
            written = true;
        } catch (const std::exception&) {
            written = false;
        }
    }
    mz_free(buffer);
    return written;
}

std::string forge_version_json(const std::string& id,
                               const std::string& minecraft,
                               const std::string& main_class) {
    return "{\"id\":\"" + id + "\",\"inheritsFrom\":\"" + minecraft +
           "\",\"mainClass\":\"" + main_class + "\"}";
}

std::vector<ZipEntry> installer_entries(ForgeKind kind,
                                        std::string_view minecraft,
                                        std::string_view loader) {
    const bool forge = kind == ForgeKind::Forge;
    const std::string token = forge ? "forge" : "neoforge";
    const std::string id =
        forge ? std::string(minecraft) + "-forge-" + std::string(loader)
              : "neoforge-" + std::string(loader);
    const std::string main_class =
        forge ? "net.minecraftforge.installer.SimpleInstaller"
              : "net.neoforged.installer.LegacyInstaller";
    const std::string coordinate =
        forge ? "net.minecraftforge:forge:" + id
              : "net.neoforged:neoforge:" + std::string(loader);

    std::vector<ZipEntry> entries;
    entries.push_back(
        {"META-INF/MANIFEST.MF",
         "Manifest-Version: 1.0\r\nMain-Class: " + main_class + "\r\n\r\n"});
    entries.push_back(
        {"install_profile.json",
         "{\"spec\":1,\"profile\":\"" + token + "\",\"version\":\"" +
             std::string(loader) + "\",\"minecraft\":\"" + std::string(minecraft) +
             "\",\"path\":\"" + coordinate + "\"}"});
    entries.push_back(
        {"version.json",
         forge_version_json(id, std::string(minecraft), "com.example.Launcher")});
    return entries;
}

void replace_entry(std::vector<ZipEntry>& entries,
                   std::string_view name,
                   std::string_view content) {
    for (ZipEntry& entry : entries) {
        if (entry.name == name) {
            entry.content = std::string(content);
            return;
        }
    }
    entries.push_back({std::string(name), std::string(content)});
}

void remove_entry(std::vector<ZipEntry>& entries, std::string_view name) {
    entries.erase(
        std::remove_if(
            entries.begin(),
            entries.end(),
            [name](const ZipEntry& entry) { return entry.name == name; }),
        entries.end());
}

std::filesystem::path make_installer(const std::filesystem::path& directory,
                                     const std::string& name,
                                     const std::vector<ZipEntry>& entries) {
    const auto path = directory / name;
    if (!write_zip(path, entries)) {
        expect(false, "unable to write test archive " + name);
    }
    return path;
}

void expect_archive_rejected(const std::filesystem::path& directory,
                             const std::string& name,
                             std::vector<ZipEntry> entries,
                             ForgeKind kind,
                             std::string_view minecraft,
                             std::string_view loader,
                             const std::string& message) {
    const auto path = make_installer(directory, name, entries);
    expect_throws(
        [&path, kind, minecraft, loader]() {
            aurora::verify_forge_installer_archive(path, kind, minecraft, loader);
        },
        message);
}

void expect_archive_accepted(const std::filesystem::path& directory,
                             const std::string& name,
                             std::vector<ZipEntry> entries,
                             ForgeKind kind,
                             std::string_view minecraft,
                             std::string_view loader,
                             const std::string& message) {
    const auto path = make_installer(directory, name, entries);
    expect_no_throw(
        [&path, kind, minecraft, loader]() {
            aurora::verify_forge_installer_archive(path, kind, minecraft, loader);
        },
        message);
}

std::filesystem::path write_version(const std::filesystem::path& game_dir,
                                    const std::string& id,
                                    const std::string& content) {
    const auto directory = game_dir / "versions" / aurora::path_from_utf8(id);
    std::filesystem::create_directories(directory);
    const auto file = directory / aurora::path_from_utf8(id + ".json");
    aurora::write_text_file(file, content);
    return file;
}

void test_forge_promotions() {
    const std::string promotions = R"({
  "homepage": "https://example.invalid/",
  "promos": {
    "1.19.2-latest": "43.3.0",
    "1.20.1-latest": "47.2.0",
    "1.20.1-recommended": "47.1.3",
    "1.20.1-beta": "1.0.0",
    "1.20.10-latest": "99.0.0",
    "1.20.2-latest": "48.1.0"
  }
})";

    const auto parsed = aurora::parse_forge_promotions(promotions, "1.20.1");
    expect(parsed.size() == 2, "promotions only for the requested Minecraft version");
    bool has_recommended = false;
    bool has_latest = false;
    for (const auto& promotion : parsed) {
        expect(promotion.minecraft == "1.20.1", "promotion Minecraft version");
        if (promotion.channel == ForgeChannel::Recommended) {
            has_recommended = true;
            expect(promotion.version == "47.1.3", "recommended promotion version");
        } else {
            has_latest = true;
            expect(promotion.version == "47.2.0", "latest promotion version");
        }
        expect(!promotion.explicit_version, "promotion is not explicit");
    }
    expect(has_recommended && has_latest, "both official channels are parsed");

    const auto recommended = aurora::select_forge_version(promotions, "1.20.1");
    expect(recommended && recommended->version == "47.1.3", "recommended is preferred");
    expect(
        aurora::select_forge_version(promotions, "1.20.1", std::nullopt, ForgeChannel::Latest)
                ->version == "47.2.0",
        "latest channel selection");

    const auto explicit_version = aurora::select_forge_version(
        promotions, "1.20.1", std::string("1.20.1-forge-47.2.0"));
    expect(
        explicit_version && explicit_version->version == "47.2.0" &&
            explicit_version->explicit_version,
        "explicit Forge version is normalised");

    const std::string latest_only = R"({"promos":{"1.20.1-latest":"47.2.0"}})";
    const auto fallback_latest = aurora::select_forge_version(latest_only, "1.20.1");
    expect(fallback_latest && fallback_latest->version == "47.2.0", "latest fallback");

    const std::string recommended_only =
        R"({"promos":{"1.20.1-recommended":"47.1.3"}})";
    const auto fallback_recommended = aurora::select_forge_version(
        recommended_only, "1.20.1", std::nullopt, ForgeChannel::Latest);
    expect(
        fallback_recommended && fallback_recommended->version == "47.1.3",
        "recommended fallback");

    const auto missing = aurora::select_forge_version(
        R"({"promos":{"1.19.2-latest":"43.3.0"}})", "1.20.1");
    expect(!missing, "missing promotion returns nullopt");

    const auto trimmed = aurora::select_forge_version(
        R"({"promos":{"1.20.1-latest":" 47.2.0 "}})", "1.20.1");
    expect(trimmed && trimmed->version == "47.2.0", "promotion value is trimmed");

    expect_throws(
        [] { aurora::parse_forge_promotions("{", "1.20.1"); },
        "malformed promotions JSON is rejected");
    expect_throws(
        [] { aurora::parse_forge_promotions(R"({"homepage":"x"})", "1.20.1"); },
        "promotions without promos object is rejected");
    expect_throws(
        [] { aurora::parse_forge_promotions(R"({"promos":[]})", "1.20.1"); },
        "promotions with non-object promos is rejected");
    expect_throws(
        [] {
            aurora::parse_forge_promotions(
                R"({"promos":{"1.20.1-latest":47}})", "1.20.1");
        },
        "non-string promotion is rejected");
    expect_throws(
        [] {
            aurora::parse_forge_promotions(
                R"({"promos":{"1.20.1-latest":"../evil"}})", "1.20.1");
        },
        "unsafe promotion version is rejected");
    expect_throws(
        [&promotions] { aurora::parse_forge_promotions(promotions, "../1.20.1"); },
        "unsafe Minecraft version is rejected");
    expect_throws(
        [&promotions] {
            aurora::select_forge_version(promotions, "1.20.1", std::string("../evil"));
        },
        "unsafe explicit Forge version is rejected");
}

void test_neoforge_metadata() {
    const std::string metadata = R"(<?xml version="1.0" encoding="UTF-8"?>
<metadata>
  <groupId>net.neoforged</groupId>
  <artifactId>neoforge</artifactId>
  <versioning>
    <latest>21.1.72</latest>
    <release>21.1.72</release>
    <versions>
      <version>20.4.237</version>
      <version>21.0.167</version>
      <version>21.1.9</version>
      <version>21.1.72</version>
      <version>21.1.10</version>
      <version>21.10.1</version>
      <version>26.1.0-beta</version>
      <version>21.1.72</version>
      <version>21.1.5</version>
    </versions>
    <lastUpdated>20250101000000</lastUpdated>
  </versioning>
</metadata>
)";

    const auto versions = aurora::parse_neoforge_metadata(metadata);
    expect(versions.size() == 8, "NeoForge metadata versions are parsed and deduplicated");
    expect(
        std::find(versions.begin(), versions.end(), "21.1.72") != versions.end(),
        "NeoForge version is present");

    const auto with_bom = aurora::parse_neoforge_metadata(std::string("\xEF\xBB\xBF") + metadata);
    expect(with_bom.size() == versions.size(), "UTF-8 BOM metadata is parsed");

    expect(
        aurora::select_neoforge_versions(versions, "1.21.1") ==
            std::vector<std::string>{"21.1.72", "21.1.10", "21.1.9", "21.1.5"},
        "NeoForge versions for 1.21.1 are filtered and sorted");
    expect(
        aurora::select_neoforge_version(versions, "1.21.1") ==
            std::optional<std::string>("21.1.72"),
        "newest NeoForge version for 1.21.1");
    expect(
        aurora::select_neoforge_versions(versions, "1.21") ==
            std::vector<std::string>{"21.0.167"},
        "NeoForge versions for 1.21");
    expect(
        aurora::select_neoforge_versions(versions, "1.20.4") ==
            std::vector<std::string>{"20.4.237"},
        "NeoForge versions for 1.20.4");
    expect(
        aurora::select_neoforge_versions(versions, "26.1") ==
            std::vector<std::string>{"26.1.0-beta"},
        "NeoForge year-based version");
    expect(
        aurora::select_neoforge_versions(versions, "1.20.1").empty(),
        "NeoForge 1.20.1 is not served by this Maven endpoint");
    expect(
        !aurora::select_neoforge_version(versions, "1.22.0"),
        "no NeoForge version for an unknown Minecraft version");

    expect(aurora::neoforge_version_prefix("1.20.2") == "20.2.", "NeoForge prefix 1.20.2");
    expect(aurora::neoforge_version_prefix("1.21") == "21.0.", "NeoForge prefix 1.21");
    expect(aurora::neoforge_version_prefix("1.21.1") == "21.1.", "NeoForge prefix 1.21.1");
    expect(aurora::neoforge_version_prefix("26.1") == "26.1.", "NeoForge prefix 26.1");
    expect(aurora::neoforge_version_prefix("1.20.1").empty(), "NeoForge 1.20.1 is special");
    expect_throws(
        [] { aurora::neoforge_version_prefix("1.21-pre1"); },
        "snapshot Minecraft version has no NeoForge prefix");

    expect_throws(
        [] { aurora::parse_neoforge_metadata(""); },
        "empty metadata is rejected");
    expect_throws(
        [] { aurora::parse_neoforge_metadata(R"(<metadata></metadata>)"); },
        "metadata without versions is rejected");
    expect_throws(
        [] {
            aurora::parse_neoforge_metadata(
                R"(<metadata><versions><version>1.2.3</version></versions>)");
        },
        "unterminated metadata is rejected");
    expect_throws(
        [] {
            aurora::parse_neoforge_metadata(
                R"(<metadata><versions></versions></metadata>)");
        },
        "metadata without entries is rejected");
    expect_throws(
        [] {
            aurora::parse_neoforge_metadata(
                R"(<metadata><versions><version>1.2.3</versions></metadata>)");
        },
        "unterminated version is rejected");
    expect_throws(
        [] {
            aurora::parse_neoforge_metadata(
                R"(<metadata><versions><version/></versions></metadata>)");
        },
        "empty version element is rejected");
    expect_throws(
        [] {
            aurora::parse_neoforge_metadata(
                R"(<metadata><versions><version>1.2&amp;3</version></versions></metadata>)");
        },
        "XML entities in versions are rejected");
    expect_throws(
        [] {
            aurora::parse_neoforge_metadata(
                R"(<metadata><versions><version>../evil</version></versions></metadata>)");
        },
        "unsafe metadata version is rejected");
}

void test_urls_and_versions() {
    expect(
        aurora::normalize_forge_version("1.20.1", "47.2.0") == "47.2.0",
        "normalize bare Forge version");
    expect(
        aurora::normalize_forge_version("1.20.1", "1.20.1-47.2.0") == "47.2.0",
        "normalize Minecraft-prefixed Forge version");
    expect(
        aurora::normalize_forge_version("1.20.1", "1.20.1-forge-47.2.0") == "47.2.0",
        "normalize full Forge version");
    expect(
        aurora::normalize_forge_version("1.20.1", "forge-47.2.0") == "47.2.0",
        "normalize forge-prefixed version");
    expect(
        aurora::normalize_forge_version("1.20.1", " 47.2.0 ") == "47.2.0",
        "normalize trimmed Forge version");
    expect(
        aurora::normalize_neoforge_version("neoforge-21.1.72") == "21.1.72",
        "normalize NeoForge version");
    expect(
        aurora::normalize_neoforge_version("1.21.1-neoforge-21.1.72") == "21.1.72",
        "normalize full NeoForge version");
    expect(
        aurora::normalize_neoforge_version(" 21.1.72 ") == "21.1.72",
        "normalize trimmed NeoForge version");

    expect(aurora::is_safe_loader_version("47.2.0"), "safe loader version");
    expect(aurora::is_safe_loader_version("21.1.72-beta"), "safe prerelease version");
    expect(!aurora::is_safe_loader_version("../evil"), "traversal loader version");
    expect(!aurora::is_safe_loader_version("a/b"), "separator loader version");
    expect(!aurora::is_safe_loader_version(""), "empty loader version");
    expect(!aurora::is_safe_loader_version(".."), "dot-dot loader version");
    expect(!aurora::is_safe_loader_version("1.0."), "trailing dot loader version");
    expect(!aurora::is_safe_loader_version("1.0 "), "trailing space loader version");
    expect(aurora::is_safe_minecraft_version("1.21.1"), "safe Minecraft version");
    expect(!aurora::is_safe_minecraft_version("1.21.1/evil"), "unsafe Minecraft version");

    const std::string forge_url = aurora::forge_installer_url("1.20.1", "47.2.0");
    expect(
        forge_url ==
            "https://maven.minecraftforge.net/net/minecraftforge/forge/1.20.1-47.2.0/"
            "forge-1.20.1-47.2.0-installer.jar",
        "default Forge installer URL");
    expect(
        aurora::forge_installer_url("1.20.1", "1.20.1-forge-47.2.0") == forge_url,
        "Forge URL accepts full version");

    aurora::ForgeEndpoints endpoints;
    endpoints.forge_maven_root = "https://mirror.example/maven/";
    expect(
        aurora::forge_installer_url("1.20.1", "47.2.0", endpoints) ==
            "https://mirror.example/maven/net/minecraftforge/forge/1.20.1-47.2.0/"
            "forge-1.20.1-47.2.0-installer.jar",
        "injectable Forge Maven root");

    const std::string neoforge_url = aurora::neoforge_installer_url("21.1.72");
    expect(
        neoforge_url ==
            "https://maven.neoforged.net/releases/net/neoforged/neoforge/21.1.72/"
            "neoforge-21.1.72-installer.jar",
        "default NeoForge installer URL");
    endpoints.neoforge_maven_root = "https://mirror.example/releases";
    expect(
        aurora::neoforge_installer_url("neoforge-21.1.72", endpoints) ==
            "https://mirror.example/releases/net/neoforged/neoforge/21.1.72/"
            "neoforge-21.1.72-installer.jar",
        "injectable NeoForge Maven root");

    expect(
        aurora::sha1_sidecar_url(forge_url) == forge_url + ".sha1",
        "SHA-1 sidecar URL");

    const std::string hash = "A9993E364706816ABA3E25717850C26C9CD0D89D";
    expect(
        aurora::parse_sha1_sidecar(hash) ==
            "a9993e364706816aba3e25717850c26c9cd0d89d",
        "SHA-1 sidecar hash is lowercased");
    expect(
        aurora::parse_sha1_sidecar("  " + hash + "  forge.jar\n") ==
            "a9993e364706816aba3e25717850c26c9cd0d89d",
        "SHA-1 sidecar ignores surrounding whitespace and filename");
    expect(
        aurora::parse_sha1_sidecar("*" + hash) ==
            "a9993e364706816aba3e25717850c26c9cd0d89d",
        "SHA-1 sidecar accepts a leading binary marker");
    expect_throws(
        [] { aurora::parse_sha1_sidecar(""); },
        "empty SHA-1 sidecar is rejected");
    expect_throws(
        [] { aurora::parse_sha1_sidecar("   \n"); },
        "whitespace SHA-1 sidecar is rejected");
    expect_throws(
        [] { aurora::parse_sha1_sidecar("a9993e364706816aba3e25717850c26c9cd0d89"); },
        "short SHA-1 sidecar is rejected");
    expect_throws(
        [] {
            aurora::parse_sha1_sidecar(
                "a9993e364706816aba3e25717850c26c9cd0d89d0");
        },
        "long SHA-1 sidecar is rejected");
    expect_throws(
        [] {
            aurora::parse_sha1_sidecar(
                "g9993e364706816aba3e25717850c26c9cd0d89d");
        },
        "non-hexadecimal SHA-1 sidecar is rejected");
    expect_throws(
        [] { aurora::sha1_sidecar_url(""); },
        "empty SHA-1 sidecar URL is rejected");
    expect_throws(
        [] { aurora::forge_installer_url("1.20.1", "../evil"); },
        "unsafe Forge installer version is rejected");
    expect_throws(
        [] { aurora::neoforge_installer_url("../evil"); },
        "unsafe NeoForge installer version is rejected");
    expect_throws(
        [] {
            aurora::ForgeEndpoints empty;
            empty.forge_maven_root.clear();
            aurora::forge_installer_url("1.20.1", "47.2.0", empty);
        },
        "empty Forge Maven root is rejected");

    expect(
        aurora::expected_forge_version_id("1.20.1", "47.2.0") ==
            "1.20.1-forge-47.2.0",
        "expected Forge version id");
    expect(
        aurora::expected_forge_version_id("1.20.1", "1.20.1-forge-47.2.0") ==
            "1.20.1-forge-47.2.0",
        "expected Forge version id with full version");
    expect(
        aurora::expected_neoforge_version_id("21.1.72") == "neoforge-21.1.72",
        "expected NeoForge version id");
    expect(
        aurora::expected_neoforge_version_id("neoforge-21.1.72") ==
            "neoforge-21.1.72",
        "expected NeoForge version id with prefix");
}

void test_java_defaults() {
    expect(
        aurora::required_java_major_for(ForgeKind::Forge, "1.19.2", "43.3.0") == 17,
        "Forge 1.19.2 uses Java 17");
    expect(
        aurora::required_java_major_for(ForgeKind::Forge, "1.20.1", "47.2.0") == 17,
        "Forge 1.20.1 uses Java 17");
    expect(
        aurora::required_java_major_for(ForgeKind::Forge, "1.20.4", "49.0.30") == 17,
        "Forge 1.20.4 uses Java 17");
    expect(
        aurora::required_java_major_for(ForgeKind::Forge, "1.20.5", "51.0.0") == 21,
        "Forge 1.20.5 uses Java 21");
    expect(
        aurora::required_java_major_for(ForgeKind::Forge, "1.20.6", "51.0.0") == 21,
        "Forge 1.20.6 uses Java 21");
    expect(
        aurora::required_java_major_for(ForgeKind::Forge, "1.21.1", "52.0.1") == 21,
        "Forge 1.21.1 uses Java 21");
    expect(
        aurora::required_java_major_for(ForgeKind::Forge, "26.1", "61.0.0") == 21,
        "year-based Minecraft defaults to Java 21");
    expect(
        aurora::required_java_major_for(ForgeKind::NeoForge, "1.20.2", "20.2.86") == 17,
        "NeoForge 1.20.2 uses Java 17");
    expect(
        aurora::required_java_major_for(ForgeKind::NeoForge, "1.20.4", "20.4.237") == 17,
        "NeoForge 1.20.4 uses Java 17");
    expect(
        aurora::required_java_major_for(ForgeKind::NeoForge, "1.20.6", "20.6.119") == 21,
        "NeoForge 1.20.6 uses Java 21");
    expect(
        aurora::required_java_major_for(ForgeKind::NeoForge, "1.21.1", "21.1.72") == 21,
        "NeoForge 1.21.1 uses Java 21");
    expect(
        aurora::required_java_major_for(ForgeKind::NeoForge, "1.20.1", "47.1.106") == 17,
        "legacy NeoForge 1.20.1 uses Java 17");
    expect_throws(
        [] {
            aurora::required_java_major_for(ForgeKind::Forge, "../1.20.1", "47.2.0");
        },
        "unsafe Minecraft version in Java selection is rejected");
}

void test_installer_command() {
    const auto root = make_temp_dir("installer-command");
    const auto game_dir = root / "game dir";
    std::filesystem::create_directories(game_dir);
    const auto java = root / "jdk 21" / "bin" / "java.exe";
    const auto installer = root / "forge installer.jar";

    const auto command = aurora::build_installer_command(java, installer, game_dir);
    expect(command.arguments.size() == 6, "installer command argument count");
    expect(
        command.arguments[0] == "-Djava.awt.headless=true",
        "installer command is headless");
    expect(
        command.arguments[1] == "-Dfile.encoding=UTF-8",
        "installer command encoding");
    expect(command.arguments[2] == "-jar", "installer command uses -jar");
    expect(
        command.arguments[3] == aurora::path_to_utf8(aurora::absolute_path(installer)),
        "installer command uses absolute JAR path");
    expect(
        command.arguments[4] == "--installClient",
        "installer command uses --installClient");
    expect(
        command.arguments[5] == aurora::path_to_utf8(aurora::absolute_path(game_dir)),
        "installer command uses absolute game directory");
    expect(
        command.working_directory == aurora::absolute_path(game_dir),
        "installer working directory is the game directory");
    expect(
        command.java == aurora::absolute_path(java),
        "installer Java path is absolute");
    expect(
        command.command_line().find("--installClient") != std::string::npos,
        "installer command line is renderable for logging");

    expect_throws(
        [] {
            aurora::build_installer_command(
                {},
                std::filesystem::path("installer.jar"),
                std::filesystem::path("game"));
        },
        "installer command requires Java");
    expect_throws(
        [] {
            aurora::build_installer_command(
                std::filesystem::path("java.exe"),
                {},
                std::filesystem::path("game"));
        },
        "installer command requires installer JAR");
    expect_throws(
        [] {
            aurora::build_installer_command(
                std::filesystem::path("java.exe"),
                std::filesystem::path("installer.jar"),
                {});
        },
        "installer command requires game directory");
}

void test_version_discovery() {
    {
        const auto game_dir = make_temp_dir("discovery-preferred");
        write_version(
            game_dir,
            "1.20.1-forge-47.1.3",
            forge_version_json(
                "1.20.1-forge-47.1.3", "1.20.1", "com.example.Launcher"));
        const auto expected_file = write_version(
            game_dir,
            "1.20.1-forge-47.2.0",
            forge_version_json(
                "1.20.1-forge-47.2.0", "1.20.1", "com.example.Launcher"));
        const auto found = aurora::find_generated_forge_version(
            game_dir,
            ForgeKind::Forge,
            "1.20.1",
            "47.2.0",
            "1.20.1-forge-47.2.0");
        expect(
            found && found->id == "1.20.1-forge-47.2.0",
            "expected Forge version id is preferred");
        expect(
            found && found->json_path.filename() == expected_file.filename(),
            "discovered Forge version JSON path");
    }

    {
        const auto game_dir = make_temp_dir("discovery-legacy");
        write_version(
            game_dir,
            "1.20.1-forge-47.1.3",
            forge_version_json(
                "1.20.1-forge-47.1.3", "1.20.1", "com.example.Launcher"));
        const auto found = aurora::find_generated_forge_version(
            game_dir,
            ForgeKind::Forge,
            "1.20.1",
            "47.1.3",
            "1.20.1-forge-47.2.0");
        expect(
            found && found->id == "1.20.1-forge-47.1.3",
            "legacy Forge id is accepted when expected id is absent");
    }

    {
        const auto game_dir = make_temp_dir("discovery-nested");
        const auto directory = game_dir / "versions" / "1.20.1-forge-47.2.0";
        std::filesystem::create_directories(directory);
        aurora::write_text_file(
            directory / "custom-name.json",
            forge_version_json(
                "1.20.1-forge-47.2.0", "1.20.1", "com.example.Launcher"));
        const auto found = aurora::find_generated_forge_version(
            game_dir, ForgeKind::Forge, "1.20.1", "47.2.0");
        expect(
            found && found->id == "1.20.1-forge-47.2.0",
            "version JSON with a non-standard filename is discovered");
    }

    {
        const auto game_dir = make_temp_dir("discovery-unrelated");
        write_version(
            game_dir,
            "fabric-loader-0.15.0-1.20.1",
            forge_version_json(
                "fabric-loader-0.15.0-1.20.1", "1.20.1", "net.fabricmc.Loader"));
        const auto found = aurora::find_generated_forge_version(
            game_dir, ForgeKind::Forge, "1.20.1", "0.15.0");
        expect(!found, "unrelated loader version is ignored");
    }

    {
        const auto game_dir = make_temp_dir("discovery-wrong-parent");
        write_version(
            game_dir,
            "1.20.1-forge-47.2.0",
            forge_version_json(
                "1.20.1-forge-47.2.0", "1.20.2", "com.example.Launcher"));
        const auto found = aurora::find_generated_forge_version(
            game_dir, ForgeKind::Forge, "1.20.1", "47.2.0");
        expect(!found, "version with wrong inheritsFrom is ignored");
    }

    {
        const auto game_dir = make_temp_dir("discovery-no-main-class");
        write_version(
            game_dir,
            "1.20.1-forge-47.2.0",
            R"({"id":"1.20.1-forge-47.2.0","inheritsFrom":"1.20.1"})");
        const auto found = aurora::find_generated_forge_version(
            game_dir, ForgeKind::Forge, "1.20.1", "47.2.0");
        expect(!found, "version without mainClass is ignored");
    }

    {
        const auto game_dir = make_temp_dir("discovery-id-mismatch");
        write_version(
            game_dir,
            "1.20.1-forge-47.2.0",
            forge_version_json(
                "1.20.1-forge-47.1.0", "1.20.1", "com.example.Launcher"));
        const auto found = aurora::find_generated_forge_version(
            game_dir, ForgeKind::Forge, "1.20.1", "47.2.0");
        expect(!found, "version JSON id must match its directory");
    }

    {
        const auto game_dir = make_temp_dir("discovery-malformed");
        write_version(game_dir, "1.20.1-forge-47.2.0", "{");
        const auto found = aurora::find_generated_forge_version(
            game_dir, ForgeKind::Forge, "1.20.1", "47.2.0");
        expect(!found, "malformed version JSON is ignored");
    }

    {
        const auto game_dir = make_temp_dir("discovery-missing");
        const auto found = aurora::find_generated_forge_version(
            game_dir, ForgeKind::Forge, "1.20.1", "47.2.0");
        expect(!found, "missing versions directory returns nullopt");
    }

    {
        const auto game_dir = make_temp_dir("discovery-neoforge");
        write_version(
            game_dir,
            "neoforge-21.1.72",
            forge_version_json(
                "neoforge-21.1.72", "1.21.1", "com.example.Launcher"));
        const auto found = aurora::find_generated_forge_version(
            game_dir, ForgeKind::NeoForge, "1.21.1", "21.1.72");
        expect(
            found && found->id == "neoforge-21.1.72",
            "NeoForge version is discovered");
        const auto forge_lookup = aurora::find_generated_forge_version(
            game_dir, ForgeKind::Forge, "1.21.1", "21.1.72");
        expect(!forge_lookup, "NeoForge version is not accepted as Forge");
    }

    {
        const auto game_dir = make_temp_dir("discovery-token-boundary");
        write_version(
            game_dir,
            "neoforge-21.1.72",
            forge_version_json(
                "neoforge-21.1.72", "1.21.1", "com.example.Launcher"));
        const auto found = aurora::find_generated_forge_version(
            game_dir, ForgeKind::NeoForge, "1.21.1", "21.1.7");
        expect(!found, "loader version must match a complete version token");
    }

    if (!symlink_creation_is_available()) {
        std::cout << "SKIP: symbolic link creation is not permitted on this system\n";
    } else {
        const auto game_dir = make_temp_dir("discovery-symlink");
        std::filesystem::create_directories(game_dir / "versions");
        const auto outside = make_temp_dir("discovery-symlink-outside");
        const auto outside_version = outside / "1.20.1-forge-47.2.0";
        std::filesystem::create_directories(outside_version);
        aurora::write_text_file(
            outside_version / "1.20.1-forge-47.2.0.json",
            forge_version_json(
                "1.20.1-forge-47.2.0", "1.20.1", "com.example.Launcher"));
        std::error_code error;
        std::filesystem::create_directory_symlink(
            outside_version, game_dir / "versions" / "1.20.1-forge-47.2.0", error);
        if (!error) {
            const auto found = aurora::find_generated_forge_version(
                game_dir, ForgeKind::Forge, "1.20.1", "47.2.0");
            expect(!found, "symlinked version directory is ignored");
        } else {
            std::cout << "SKIP: directory symlink creation failed: " << error.message()
                      << '\n';
        }
    }
}

void test_installer_archive_verification() {
    const auto directory = make_temp_dir("installer-archive");

    expect_archive_accepted(
        directory,
        "valid-forge.jar",
        installer_entries(ForgeKind::Forge, "1.20.1", "47.2.0"),
        ForgeKind::Forge,
        "1.20.1",
        "47.2.0",
        "valid Forge installer archive");
    expect_archive_accepted(
        directory,
        "valid-neoforge.jar",
        installer_entries(ForgeKind::NeoForge, "1.21.1", "21.1.72"),
        ForgeKind::NeoForge,
        "1.21.1",
        "21.1.72",
        "valid NeoForge installer archive");

    {
        auto entries = installer_entries(ForgeKind::Forge, "1.20.1", "47.2.0");
        remove_entry(entries, "META-INF/MANIFEST.MF");
        expect_archive_rejected(
            directory,
            "no-manifest.jar",
            std::move(entries),
            ForgeKind::Forge,
            "1.20.1",
            "47.2.0",
            "installer without manifest is rejected");
    }
    {
        auto entries = installer_entries(ForgeKind::Forge, "1.20.1", "47.2.0");
        replace_entry(
            entries,
            "META-INF/MANIFEST.MF",
            "Manifest-Version: 1.0\r\nMain-Class: com.example.Main\r\n\r\n");
        expect_archive_rejected(
            directory,
            "wrong-main-class.jar",
            std::move(entries),
            ForgeKind::Forge,
            "1.20.1",
            "47.2.0",
            "installer without installer main class is rejected");
    }
    {
        auto entries = installer_entries(ForgeKind::Forge, "1.20.1", "47.2.0");
        remove_entry(entries, "install_profile.json");
        expect_archive_rejected(
            directory,
            "no-profile.jar",
            std::move(entries),
            ForgeKind::Forge,
            "1.20.1",
            "47.2.0",
            "installer without install_profile.json is rejected");
    }
    {
        auto entries = installer_entries(ForgeKind::Forge, "1.20.1", "47.2.0");
        replace_entry(
            entries,
            "install_profile.json",
            R"({"spec":1,"profile":"forge","version":"47.2.0","minecraft":"1.20.2"})");
        expect_archive_rejected(
            directory,
            "wrong-minecraft.jar",
            std::move(entries),
            ForgeKind::Forge,
            "1.20.1",
            "47.2.0",
            "installer for another Minecraft version is rejected");
    }
    {
        auto entries = installer_entries(ForgeKind::Forge, "1.20.1", "47.2.0");
        replace_entry(
            entries,
            "install_profile.json",
            R"({"spec":1,"profile":"neoforge","version":"47.2.0","minecraft":"1.20.1"})");
        expect_archive_rejected(
            directory,
            "wrong-kind.jar",
            std::move(entries),
            ForgeKind::Forge,
            "1.20.1",
            "47.2.0",
            "installer profile for another loader is rejected");
    }
    {
        auto entries = installer_entries(ForgeKind::Forge, "1.20.1", "47.2.0");
        replace_entry(
            entries,
            "install_profile.json",
            R"({"spec":1,"profile":"forge","version":"47.1.3","minecraft":"1.20.1"})");
        expect_archive_rejected(
            directory,
            "wrong-profile-version.jar",
            std::move(entries),
            ForgeKind::Forge,
            "1.20.1",
            "47.2.0",
            "installer profile for another loader version is rejected");
    }
    {
        auto entries = installer_entries(ForgeKind::Forge, "1.20.1", "47.2.0");
        remove_entry(entries, "version.json");
        expect_archive_rejected(
            directory,
            "no-version.jar",
            std::move(entries),
            ForgeKind::Forge,
            "1.20.1",
            "47.2.0",
            "installer without version.json is rejected");
    }
    {
        auto entries = installer_entries(ForgeKind::Forge, "1.20.1", "47.2.0");
        replace_entry(
            entries,
            "version.json",
            forge_version_json(
                "1.20.1-forge-47.2.0", "1.20.2", "com.example.Launcher"));
        expect_archive_rejected(
            directory,
            "wrong-version-parent.jar",
            std::move(entries),
            ForgeKind::Forge,
            "1.20.1",
            "47.2.0",
            "installer version for another Minecraft version is rejected");
    }
    {
        auto entries = installer_entries(ForgeKind::Forge, "1.20.1", "47.2.0");
        replace_entry(
            entries,
            "version.json",
            forge_version_json(
                "1.20.1-forge-47.1.3", "1.20.1", "com.example.Launcher"));
        expect_archive_rejected(
            directory,
            "wrong-version-id.jar",
            std::move(entries),
            ForgeKind::Forge,
            "1.20.1",
            "47.2.0",
            "installer version with another loader version is rejected");
    }
    {
        auto entries = installer_entries(ForgeKind::Forge, "1.20.1", "47.2.0");
        replace_entry(
            entries,
            "version.json",
            forge_version_json("1.20.1-forge-47.2.0", "1.20.1", ""));
        expect_archive_rejected(
            directory,
            "no-main-class.jar",
            std::move(entries),
            ForgeKind::Forge,
            "1.20.1",
            "47.2.0",
            "installer version without mainClass is rejected");
    }
    {
        auto entries = installer_entries(ForgeKind::Forge, "1.20.1", "47.2.0");
        replace_entry(
            entries,
            "version.json",
            forge_version_json(
                "neoforge-21.1.72", "1.20.1", "com.example.Launcher"));
        expect_archive_rejected(
            directory,
            "wrong-version-kind.jar",
            std::move(entries),
            ForgeKind::Forge,
            "1.20.1",
            "47.2.0",
            "Forge installer version cannot be NeoForge");
    }

    {
        const auto path = directory / "wrong-extension.zip";
        expect(
            write_zip(path, installer_entries(ForgeKind::Forge, "1.20.1", "47.2.0")),
            "write wrong-extension archive");
        expect_throws(
            [&path] {
                aurora::verify_forge_installer_archive(
                    path, ForgeKind::Forge, "1.20.1", "47.2.0");
            },
            "installer with non-JAR extension is rejected");
    }
    {
        const auto path = directory / "not-a-zip.jar";
        aurora::write_text_file(path, "this is not a JAR");
        expect_throws(
            [&path] {
                aurora::verify_forge_installer_archive(
                    path, ForgeKind::Forge, "1.20.1", "47.2.0");
            },
            "invalid installer archive is rejected");
    }
    {
        const auto path = directory / "empty.jar";
        aurora::write_text_file(path, "");
        expect_throws(
            [&path] {
                aurora::verify_forge_installer_archive(
                    path, ForgeKind::Forge, "1.20.1", "47.2.0");
            },
            "empty installer archive is rejected");
    }

    if (!symlink_creation_is_available()) {
        std::cout << "SKIP: symbolic link creation is not permitted on this system\n";
    } else {
        const auto target = make_installer(
            directory,
            "symlink-target.jar",
            installer_entries(ForgeKind::Forge, "1.20.1", "47.2.0"));
        const auto link = directory / "symlink-installer.jar";
        std::error_code error;
        std::filesystem::create_symlink(target, link, error);
        if (!error) {
            expect_throws(
                [&link] {
                    aurora::verify_forge_installer_archive(
                        link, ForgeKind::Forge, "1.20.1", "47.2.0");
                },
                "symlinked installer archive is rejected");
        } else {
            std::cout << "SKIP: file symlink creation failed: " << error.message() << '\n';
        }
    }
}

void test_installer_process_logging() {
#ifdef _WIN32
    const std::string comspec = aurora::environment_value("ComSpec");
    if (comspec.empty()) {
        std::cout << "SKIP: ComSpec is not available\n";
        return;
    }
    const auto directory = make_temp_dir("process-log");
    const auto log = directory / "process.log";
    const int exit_code = aurora::run_process(
        aurora::path_from_utf8(comspec),
        {"/d",
         "/c",
         "echo aurora-stdout & echo aurora-stderr 1>&2 & "
         "set /p aurora_input= & echo aurora-eof"},
        directory,
        true,
        log);
    expect(exit_code == 0, "process log command exit code");
    const std::string text = aurora::read_text_file(log);
    expect(
        text.find("aurora-stdout") != std::string::npos,
        "process stdout is written to the log");
    expect(
        text.find("aurora-stderr") != std::string::npos,
        "process stderr is written to the log");
    expect(
        text.find("aurora-eof") != std::string::npos,
        "process stdin is redirected from NUL instead of blocking");
    expect(
        text.find("==== ") != std::string::npos,
        "process log contains a command header");
#endif
}

void test_install_entry_validation() {
    const auto root = make_temp_dir("install-entry");

    {
        aurora::ForgeInstallRequest request;
        request.minecraft_version = "1.20.1";
        request.loader_version = "47.2.0";
        expect_throws(
            [&request] { aurora::install_forge(request); },
            "install requires a game directory");
    }
    {
        aurora::ForgeInstallRequest request;
        request.kind = ForgeKind::NeoForge;
        request.game_dir = root / "neoforge-1.20.1";
        request.minecraft_version = "1.20.1";
        request.loader_version = "47.1.106";
        expect_throws(
            [&request] { aurora::install_forge(request); },
            "NeoForge 1.20.1 is rejected before any download");
    }
    {
        aurora::ForgeInstallRequest request;
        request.kind = ForgeKind::NeoForge;
        request.game_dir = root / "neoforge-mismatch";
        request.minecraft_version = "1.21.1";
        request.loader_version = "20.4.237";
        expect_throws(
            [&request] { aurora::install_forge(request); },
            "NeoForge loader for another Minecraft version is rejected");
    }
    {
        aurora::ForgeInstallRequest request;
        request.game_dir = root / "forge-insecure";
        request.minecraft_version = "1.20.1";
        request.loader_version = "47.2.0";
        request.endpoints.forge_maven_root = "http://insecure.example/";
        expect_throws(
            [&request] { aurora::install_forge(request); },
            "non-HTTPS Forge Maven endpoint is rejected before download");
    }
    {
        aurora::ForgeInstallRequest request;
        request.game_dir = root / "forge-insecure-promotions";
        request.minecraft_version = "1.20.1";
        request.endpoints.forge_promotions_url = "http://insecure.example/promotions.json";
        expect_throws(
            [&request] { aurora::install_forge(request); },
            "non-HTTPS promotions endpoint is rejected before download");
    }
    {
        aurora::ForgeInstallRequest request;
        request.kind = ForgeKind::NeoForge;
        request.game_dir = root / "neoforge-insecure";
        request.minecraft_version = "1.21.1";
        request.loader_version = "21.1.72";
        request.endpoints.neoforge_maven_root = "http://insecure.example/";
        expect_throws(
            [&request] { aurora::install_forge(request); },
            "non-HTTPS NeoForge Maven endpoint is rejected before download");
    }
    {
        aurora::ForgeInstallRequest request;
        request.game_dir = root / "forge-unsafe";
        request.minecraft_version = "1.20.1";
        request.loader_version = "../evil";
        expect_throws(
            [&request] { aurora::install_forge(request); },
            "unsafe Forge loader version is rejected before download");
    }
}

} // namespace

int main() {
    test_forge_promotions();
    test_neoforge_metadata();
    test_urls_and_versions();
    test_java_defaults();
    test_installer_command();
    test_version_discovery();
    test_installer_archive_verification();
    test_installer_process_logging();
    test_install_entry_validation();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All Forge tests passed\n";
    return 0;
}
