#include "core/error.hpp"
#include "core/mods.hpp"
#include "core/paths.hpp"
#include "core/strings.hpp"

#include <miniz.h>

#include <chrono>
#include <utility>
#include <exception>
#include <filesystem>
#include <functional>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

int failures = 0;

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

void expect(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

std::filesystem::path make_temp_dir(const std::string& name) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
                      ("aurora-mods-tests-" + name + "-" + std::to_string(stamp));
    std::filesystem::create_directories(path);
    return path;
}

void cleanup(const std::filesystem::path& directory) {
    std::error_code error;
    std::filesystem::remove_all(directory, error);
}

bool throws(const std::function<void()>& action) {
    try {
        action();
    } catch (const aurora::Error&) {
        return true;
    }
    return false;
}

void make_file(const std::filesystem::path& path, std::string_view content = "x") {
    aurora::write_text_file(path, content);
}

void write_jar(const std::filesystem::path& path,
               const std::vector<std::pair<std::string, std::string>>& entries) {
    mz_zip_archive archive{};
    const std::string filename = aurora::path_to_utf8(path);
    if (!mz_zip_writer_init_file(&archive, filename.c_str(), 0)) {
        throw aurora::Error("Unable to create test JAR: " + filename);
    }
    for (const auto& [name, content] : entries) {
        if (!mz_zip_writer_add_mem(&archive, name.c_str(), content.data(),
                                   content.size(),
                                   static_cast<mz_uint>(MZ_DEFAULT_COMPRESSION))) {
            mz_zip_writer_end(&archive);
            throw aurora::Error("Unable to add test JAR entry: " + name);
        }
    }
    if (!mz_zip_writer_finalize_archive(&archive)) {
        mz_zip_writer_end(&archive);
        throw aurora::Error("Unable to finalize test JAR");
    }
    mz_zip_writer_end(&archive);
}

void test_filename_safety() {
    expect(aurora::is_safe_mod_filename("sodium.jar"), "plain JAR name");
    expect(aurora::is_safe_mod_filename("sodium-extra-1.21.1.jar"), "versioned JAR name");
    expect(aurora::is_safe_mod_filename("Sodium.JAR"), "upper-case JAR extension");
    expect(!aurora::is_safe_mod_filename("mods/sodium.jar"), "separator is rejected");
    expect(!aurora::is_safe_mod_filename("mods\\sodium.jar"), "backslash is rejected");
    expect(!aurora::is_safe_mod_filename("../sodium.jar"), "traversal is rejected");
    expect(!aurora::is_safe_mod_filename("bad:name.jar"), "ADS colon is rejected");
    expect(!aurora::is_safe_mod_filename(".hidden.jar"), "leading dot is rejected");
    expect(!aurora::is_safe_mod_filename("sodium.jar "), "trailing space is rejected");
    expect(!aurora::is_safe_mod_filename("sodium.zip"), "non-JAR extension is rejected");
    expect(!aurora::is_safe_mod_filename("con.jar"), "reserved device name is rejected");
    expect(!aurora::is_safe_mod_filename("lpt1.jar"), "reserved port name is rejected");
    expect(!aurora::is_safe_mod_filename(""), "empty name is rejected");
    expect(!aurora::is_safe_mod_filename(std::string(201, 'a') + ".jar"),
           "over-long name is rejected");
}

void test_scan_and_toggle() {
    const std::filesystem::path root = make_temp_dir("scan");
    const std::filesystem::path mods = aurora::mods_directory(root);

    expect(aurora::scan_mods(root).empty(), "missing mods directory lists nothing");

    aurora::ensure_directory(mods);
    make_file(mods / "sodium.jar", "abc");
    make_file(mods / "sodium-extra.jar.disabled", "de");
    make_file(mods / "notes.txt", "n");
    make_file(mods / "junk.txt.disabled", "j");
    aurora::ensure_directory(mods / "directory.jar");

    const auto mods_before = aurora::scan_mods(root);
    expect(mods_before.size() == 2, "only JAR files are listed");
    if (mods_before.size() == 2) {
        expect(mods_before[0].filename == "sodium-extra.jar.disabled",
               "disabled mod sorts before the enabled one");
        expect(!mods_before[0].enabled, "disabled mod state");
        expect(mods_before[0].name == "sodium-extra.jar", "disabled mod base name");
        expect(mods_before[0].size == 2, "disabled mod size");

        expect(mods_before[1].filename == "sodium.jar", "enabled mod name");
        expect(mods_before[1].enabled, "enabled mod state");
        expect(mods_before[1].name == "sodium.jar", "enabled mod base name");
        expect(mods_before[1].size == 3, "enabled mod size");
        expect(mods_before[1].sha1.empty(), "listing does not hash by default");
    }

    const auto hashed = aurora::scan_mods(root, true);
    expect(
        hashed.size() == 2 &&
            hashed[1].sha1 == "a9993e364706816aba3e25717850c26c9cd0d89d",
        "SHA-1 is computed when requested");

    const auto disabled = aurora::set_mod_enabled(root, "sodium.jar", false);
    expect(
        disabled.changed && !disabled.enabled &&
            disabled.filename == "sodium.jar.disabled",
        "disable renames the JAR");
    expect(
        std::filesystem::is_regular_file(disabled.path) &&
            !std::filesystem::exists(mods / "sodium.jar"),
        "disabled file exists and the enabled file is gone");
    expect(
        !aurora::set_mod_enabled(root, "sodium.jar", false).changed,
        "disabling an already disabled mod is idempotent");
    expect(
        !aurora::set_mod_enabled(root, "sodium.jar.disabled", false).changed,
        "disabled name is accepted for disable");

    const auto enabled = aurora::set_mod_enabled(root, "sodium.jar.disabled", true);
    expect(
        enabled.changed && enabled.enabled && enabled.filename == "sodium.jar",
        "enable renames the JAR back");
    expect(
        !aurora::set_mod_enabled(root, "sodium.jar", true).changed,
        "enabling an already enabled mod is idempotent");
    expect(
        !aurora::set_mod_enabled(root, "SODIUM.JAR", true).changed,
        "state detection is case-insensitive");

    const auto case_result = aurora::set_mod_enabled(root, "sodium.jar", false);
    expect(case_result.changed, "case-insensitive toggle");

    make_file(mods / "case.jar");
    const auto renamed = aurora::set_mod_enabled(root, "case.jar", false);
    expect(renamed.changed && renamed.filename == "case.jar.disabled",
           "toggle keeps the requested name");

    make_file(mods / "conflict.jar");
    make_file(mods / "conflict.jar.disabled");
    expect(
        throws([&root] { aurora::set_mod_enabled(root, "conflict.jar", true); }),
        "both variants existing is an error");
    expect(
        throws([&root] { aurora::set_mod_enabled(root, "conflict.jar", false); }),
        "both variants existing is an error for disable");

    expect(
        throws([&root] { aurora::set_mod_enabled(root, "missing.jar", false); }),
        "missing mod is an error");
    expect(
        throws([&root] { aurora::set_mod_enabled(root, "../escape.jar", true); }),
        "traversal is rejected by toggle");
    expect(
        throws([&root] { aurora::set_mod_enabled(root, "notes.txt", false); }),
        "non-JAR file is rejected by toggle");

    if (!symlink_creation_is_available()) {
        std::cout << "SKIP: symbolic link creation is not permitted on this system\n";
    } else {
        std::error_code symlink_error;
        std::filesystem::create_symlink(
            mods / "sodium.jar.disabled", mods / "linked.jar", symlink_error);
        if (symlink_error) {
            std::cout << "SKIP: cannot create a symbolic link: "
                      << symlink_error.message() << '\n';
        } else {
            const auto linked = aurora::scan_mods(root);
            bool contains_link = false;
            for (const auto& mod : linked) {
                if (mod.filename == "linked.jar") {
                    contains_link = true;
                }
            }
            expect(!contains_link, "symlinks are not listed as mods");
            expect(
                throws([&root] { aurora::set_mod_enabled(root, "linked.jar", false); }),
                "symlinks cannot be toggled");
        }
    }

    const std::filesystem::path empty_root = make_temp_dir("empty");
    expect(
        throws([&empty_root] { aurora::set_mod_enabled(empty_root, "a.jar", true); }),
        "toggle without a mods directory is an error");

    cleanup(root);
    cleanup(empty_root);
}

void test_mod_metadata() {
    const auto root = make_temp_dir("metadata");
    aurora::ensure_directory(aurora::mods_directory(root));
    const auto mods = aurora::mods_directory(root);

    write_jar(mods / "fabric.jar",
              {{"fabric.mod.json",
                R"({"id":"sodium","name":"Sodium","version":"0.5.11",
                    "authors":["jellysquid",{"name":"other"}],
                    "description":"Fast renderer"})"}});
    write_jar(mods / "quilt.jar",
              {{"quilt.mod.json",
                R"({"quilt_loader":{"id":"qmod","version":"1.2.3",
                    "metadata":{"name":"Quilt Mod","description":"quilted",
                    "contributors":{"alice":"owner","bob":"contributor"}}}})"}});
    write_jar(mods / "forge.jar",
              {{"META-INF/mods.toml",
                "modLoader=\"javafml\"\n"
                "loaderVersion=\"[47,)\"\n"
                "[[mods]]\n"
                "modId=\"forgemod\"\n"
                "version=\"4.5.6\"\n"
                "displayName=\"Forge Mod\"\n"
                "authors=\"a, b\"\n"
                "description='''multi\nline'''\n"
                "[[dependencies.forgemod]]\n"
                "modId=\"forge\"\n"}});
    write_jar(mods / "neoforge.jar",
              {{"META-INF/neoforge.mods.toml",
                "[[mods]]\n"
                "modId=\"neomod\"\n"
                "version=\"7.8.9\"\n"
                "displayName=\"Neo Mod\"\n"}});
    write_jar(mods / "legacy.jar",
              {{"mcmod.info",
                R"([{"modid":"legacy","name":"Legacy Mod","version":"1.0",
                    "authorList":["old"],"description":"legacy"}])"}});
    write_jar(mods / "litemod.jar",
              {{"litemod.json",
                R"({"name":"Lite","version":"2.0","author":"lite author"})"}});
    make_file(mods / "plain.jar");

    const auto listed = aurora::scan_mods(root, false, true);
    expect(listed.size() == 7, "metadata scan lists every JAR");
    const auto find = [&listed](std::string_view name) {
        for (const auto& mod : listed) {
            if (mod.filename == name) {
                return &mod;
            }
        }
        return static_cast<const aurora::InstalledMod*>(nullptr);
    };
    const auto* fabric = find("fabric.jar");
    expect(
        fabric && fabric->has_metadata && fabric->metadata.loader == "fabric" &&
            fabric->metadata.mod_id == "sodium" &&
            fabric->metadata.name == "Sodium" &&
            fabric->metadata.version == "0.5.11" &&
            fabric->metadata.authors == "jellysquid, other" &&
            fabric->metadata.description == "Fast renderer",
        "fabric.mod.json metadata");
    const auto* quilt = find("quilt.jar");
    expect(
        quilt && quilt->metadata.loader == "quilt" &&
            quilt->metadata.name == "Quilt Mod" &&
            quilt->metadata.version == "1.2.3" &&
            quilt->metadata.authors.find("alice") != std::string::npos,
        "quilt.mod.json metadata");
    const auto* forge = find("forge.jar");
    expect(
        forge && forge->metadata.loader == "forge" &&
            forge->metadata.mod_id == "forgemod" &&
            forge->metadata.name == "Forge Mod" &&
            forge->metadata.version == "4.5.6" &&
            forge->metadata.description.find("multi") != std::string::npos,
        "META-INF/mods.toml metadata");
    const auto* neoforge = find("neoforge.jar");
    expect(
        neoforge && neoforge->metadata.loader == "neoforge" &&
            neoforge->metadata.name == "Neo Mod" &&
            neoforge->metadata.mod_id == "neomod",
        "META-INF/neoforge.mods.toml metadata");
    const auto* legacy = find("legacy.jar");
    expect(
        legacy && legacy->metadata.loader == "forge" &&
            legacy->metadata.name == "Legacy Mod" &&
            legacy->metadata.authors == "old",
        "mcmod.info metadata");
    const auto* lite = find("litemod.jar");
    expect(
        lite && lite->metadata.loader == "liteloader" &&
            lite->metadata.name == "Lite" &&
            lite->metadata.version == "2.0",
        "litemod.json metadata");
    const auto* plain = find("plain.jar");
    expect(plain && !plain->has_metadata, "JARs without metadata are still listed");

    cleanup(root);
}

} // namespace

int main() {
    try {
        test_filename_safety();
        test_scan_and_toggle();
        test_mod_metadata();
    } catch (const std::exception& error) {
        std::cerr << "Unexpected exception: " << error.what() << '\n';
        return 1;
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All mod tests passed\n";
    return 0;
}
