#include "core/error.hpp"
#include "core/paths.hpp"
#include "core/resourcepacks.hpp"
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

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

std::filesystem::path make_temp_dir(const std::string& name) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
                      ("aurora-resourcepacks-tests-" + name + "-" +
                       std::to_string(stamp));
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

void write_pack_zip(const std::filesystem::path& path,
                    std::string_view meta,
                    bool icon = false) {
    mz_zip_archive archive{};
    const std::string filename = aurora::path_to_utf8(path);
    if (!mz_zip_writer_init_file(&archive, filename.c_str(), 0)) {
        throw aurora::Error("Unable to create test ZIP: " + filename);
    }
    if (!mz_zip_writer_add_mem(&archive, "pack.mcmeta", meta.data(), meta.size(),
                               static_cast<mz_uint>(MZ_DEFAULT_COMPRESSION))) {
        mz_zip_writer_end(&archive);
        throw aurora::Error("Unable to add pack.mcmeta");
    }
    if (icon) {
        const char png[] = "not really a png";
        mz_zip_writer_add_mem(&archive, "pack.png", png, sizeof(png),
                              static_cast<mz_uint>(MZ_DEFAULT_COMPRESSION));
    }
    if (!mz_zip_writer_finalize_archive(&archive)) {
        mz_zip_writer_end(&archive);
        throw aurora::Error("Unable to finalize test ZIP");
    }
    mz_zip_writer_end(&archive);
}

void test_scan_and_toggle() {
    const auto game_dir = make_temp_dir("scan");
    const auto packs = game_dir / "resourcepacks";
    aurora::ensure_directory(packs);
    aurora::write_text_file(packs / "a.zip", "zip");
    aurora::write_text_file(packs / "b.zip", "zip");
    aurora::ensure_directory(packs / "folder");
    aurora::write_text_file(packs / "notes.txt", "n");
    aurora::write_text_file(
        game_dir / "options.txt",
        "version:3465\r\nresourcePacks:[\"vanilla\",\"file/a.zip\"]\r\n"
        "incompatibleResourcePacks:[\"file/b.zip\"]\r\nlang:en_us\r\n");

    const auto listed = aurora::scan_resource_packs(game_dir);
    expect(listed.size() == 3, "only ZIPs and pack directories are listed");
    if (listed.size() == 3) {
        expect(
            listed[0].name == "a.zip" && listed[0].enabled && !listed[0].directory,
            "enabled ZIP pack");
        expect(
            listed[1].name == "b.zip" && !listed[1].enabled, "disabled ZIP pack");
        expect(
            listed[2].name == "folder" && !listed[2].enabled &&
                listed[2].directory,
            "pack directory");
    }

    const auto disabled = aurora::set_resource_pack_enabled(game_dir, "a.zip", false);
    expect(
        disabled.changed && !disabled.enabled, "disabling a pack changes options");
    std::string options = aurora::read_text_file(game_dir / "options.txt");
    expect(
        options.find("resourcePacks:[\"vanilla\"]") != std::string::npos,
        "resourcePacks keeps other entries");
    expect(
        options.find("incompatibleResourcePacks:[\"file/b.zip\"]") !=
            std::string::npos,
        "disabling does not touch unrelated incompatible packs");
    expect(
        options.find("version:3465\r\n") == 0 &&
            options.find("lang:en_us\r\n") != std::string::npos,
        "other options and CRLF line endings are preserved");

    expect(
        !aurora::set_resource_pack_enabled(game_dir, "a.zip", false).changed,
        "disabling twice is idempotent");

    const auto enabled = aurora::set_resource_pack_enabled(game_dir, "B.ZIP", true);
    expect(enabled.changed && enabled.enabled && enabled.name == "b.zip",
           "enabling uses the on-disk name");
    options = aurora::read_text_file(game_dir / "options.txt");
    expect(
        options.find("resourcePacks:[\"vanilla\",\"file/b.zip\"]") !=
            std::string::npos,
        "enabled pack is appended to resourcePacks");

    const auto disabled_b = aurora::set_resource_pack_enabled(game_dir, "b.zip", false);
    expect(disabled_b.changed, "disabling an incompatible pack changes options");
    options = aurora::read_text_file(game_dir / "options.txt");
    expect(
        options.find("resourcePacks:[\"vanilla\"]") != std::string::npos,
        "disabled pack is removed");
    expect(
        options.find("incompatibleResourcePacks:[]") != std::string::npos,
        "disabled pack is removed from incompatibleResourcePacks");

    expect(
        aurora::set_resource_pack_enabled(game_dir, "folder", true).changed,
        "pack directories can be enabled");

    expect(
        throws([&game_dir] {
            aurora::set_resource_pack_enabled(game_dir, "../escape.zip", true);
        }),
        "traversal is rejected");
    expect(
        throws([&game_dir] {
            aurora::set_resource_pack_enabled(game_dir, "missing.zip", true);
        }),
        "missing pack is rejected");
    expect(
        throws([&game_dir] {
            aurora::set_resource_pack_enabled(game_dir, "notes.txt", true);
        }),
        "non-pack file is rejected");

    aurora::write_text_file(game_dir / "options.txt", "resourcePacks:oops\r\n");
    expect(
        throws([&game_dir] {
            aurora::set_resource_pack_enabled(game_dir, "a.zip", true);
        }),
        "malformed resourcePacks is rejected");
    const auto corrupt_list = aurora::scan_resource_packs(game_dir);
    bool any_enabled = false;
    for (const auto& pack : corrupt_list) {
        any_enabled = any_enabled || pack.enabled;
    }
    expect(!any_enabled, "malformed resourcePacks disables listing without failing");

    cleanup(game_dir);
}

void test_missing_options_and_line_endings() {
    const auto game_dir = make_temp_dir("new");
    const auto packs = game_dir / "resourcepacks";
    aurora::ensure_directory(packs);
    aurora::write_text_file(packs / "x.zip", "zip");

    const auto initially = aurora::scan_resource_packs(game_dir);
    expect(
        initially.size() == 1 && !initially[0].enabled,
        "missing options.txt means nothing is enabled");

    const auto result = aurora::set_resource_pack_enabled(game_dir, "x.zip", true);
    expect(result.changed, "enabling creates options.txt");
    const std::string options = aurora::read_text_file(game_dir / "options.txt");
    expect(
        options == "resourcePacks:[\"file/x.zip\"]\r\n",
        "new options.txt uses CRLF and a minimal resourcePacks line");

    const auto lf_dir = make_temp_dir("lf");
    aurora::ensure_directory(lf_dir / "resourcepacks");
    aurora::write_text_file(lf_dir / "resourcepacks" / "y.zip", "zip");
    aurora::write_text_file(lf_dir / "options.txt", "lang:en_us\nresourcePacks:[]\n");
    expect(
        aurora::set_resource_pack_enabled(lf_dir, "y.zip", true).changed,
        "LF options can be updated");
    const std::string lf_options = aurora::read_text_file(lf_dir / "options.txt");
    expect(
        lf_options == "lang:en_us\nresourcePacks:[\"file/y.zip\"]\n",
        "LF line endings are preserved");

    const auto no_newline_dir = make_temp_dir("nonewline");
    aurora::ensure_directory(no_newline_dir / "resourcepacks");
    aurora::write_text_file(no_newline_dir / "resourcepacks" / "z.zip", "zip");
    aurora::write_text_file(no_newline_dir / "options.txt", "resourcePacks:[]");
    expect(
        aurora::set_resource_pack_enabled(no_newline_dir, "z.zip", true).changed,
        "options without a trailing newline can be updated");
    const std::string no_newline =
        aurora::read_text_file(no_newline_dir / "options.txt");
    expect(
        no_newline == "resourcePacks:[\"file/z.zip\"]",
        "missing trailing newline is preserved");

    cleanup(game_dir);
    cleanup(lf_dir);
    cleanup(no_newline_dir);
}

void test_pack_mcmeta_compatibility() {
    const auto game_dir = make_temp_dir("meta");
    const auto packs = game_dir / "resourcepacks";
    aurora::ensure_directory(packs);
    write_pack_zip(packs / "current.zip", R"({"pack":{"pack_format":34,
      "description":"Current pack"}})",
                   true);
    write_pack_zip(packs / "range.zip", R"({"pack":{"pack_format":18,
      "supported_formats":[18,34]}})");
    write_pack_zip(packs / "old.zip", R"({"pack":{"pack_format":4}})");

    const aurora::ResourcePackContext context =
        aurora::resource_pack_context(game_dir, "1.21.1");
    expect(context.pack_format == 34, "1.21.1 resource format from the table");

    const auto listed = aurora::scan_resource_packs(game_dir, context);
    const auto find = [&listed](std::string_view name)
        -> const aurora::InstalledResourcePack* {
        for (const auto& pack : listed) {
            if (pack.name == name) {
                return &pack;
            }
        }
        return nullptr;
    };
    const auto* current = find("current.zip");
    const auto* range = find("range.zip");
    const auto* old = find("old.zip");
    expect(current && current->has_metadata && current->pack_format == 34 &&
               current->has_icon && current->description == "Current pack",
           "pack.mcmeta, description and pack.png are read");
    expect(
        current && current->compatibility ==
                       aurora::ResourcePackCompatibility::Compatible,
        "matching pack_format is compatible");
    expect(range && range->compatibility ==
                       aurora::ResourcePackCompatibility::Compatible,
           "supported_formats range is compatible");
    expect(old && old->compatibility == aurora::ResourcePackCompatibility::TooOld,
           "old pack format is reported too old");

    const auto enabled_old =
        aurora::set_resource_pack_enabled(game_dir, "old.zip", true, context);
    expect(enabled_old.changed, "incompatible pack can be enabled");
    std::string options = aurora::read_text_file(game_dir / "options.txt");
    expect(
        options.find("incompatibleResourcePacks:[\"file/old.zip\"]") !=
            std::string::npos,
        "enabling an incompatible pack updates incompatibleResourcePacks");
    const auto enabled_current =
        aurora::set_resource_pack_enabled(game_dir, "current.zip", true, context);
    expect(enabled_current.changed &&
               aurora::read_text_file(game_dir / "options.txt")
                       .find("incompatibleResourcePacks:[\"file/old.zip\"]") !=
                   std::string::npos,
           "compatible packs do not enter the incompatible list");

    const auto rescan = aurora::scan_resource_packs(game_dir, context);
    for (const auto& pack : rescan) {
        if (pack.name == "old.zip") {
            expect(pack.enabled,
                   "an incompatible pack listed in both options arrays is active");
        }
    }
    aurora::set_resource_pack_enabled(game_dir, "old.zip", false, context);
    options = aurora::read_text_file(game_dir / "options.txt");
    expect(options.find("incompatibleResourcePacks:[]") != std::string::npos,
           "disabling removes both list entries");

    // Minecraft 1.12.2 uses the old bare-name options format.
    const auto legacy_dir = make_temp_dir("legacy");
    aurora::ensure_directory(legacy_dir / "resourcepacks");
    write_pack_zip(legacy_dir / "resourcepacks" / "p.zip",
                   R"({"pack":{"pack_format":3}})");
    const auto legacy = aurora::resource_pack_context(legacy_dir, "1.12.2");
    expect(legacy.pack_format == 3 && !aurora::minecraft_uses_new_resource_pack_options("1.12.2"),
           "1.12.2 uses format 3 and bare names");
    aurora::set_resource_pack_enabled(legacy_dir, "p.zip", true, legacy);
    expect(
        aurora::read_text_file(legacy_dir / "options.txt") ==
            "resourcePacks:[\"p.zip\"]\r\n",
        "old versions write bare resource pack names");
    const auto legacy_scan = aurora::scan_resource_packs(legacy_dir, legacy);
    expect(legacy_scan.size() == 1 && legacy_scan[0].enabled,
           "bare names are recognised when scanning");

    // New 1.21.9+ metadata uses min_format/max_format.
    const auto modern_dir = make_temp_dir("modern");
    aurora::ensure_directory(modern_dir / "resourcepacks");
    write_pack_zip(modern_dir / "resourcepacks" / "m.zip",
                   R"({"pack":{"pack_format":64,"min_format":[64,0],
                        "max_format":[70,5]}})");
    aurora::ResourcePackContext modern;
    modern.minecraft_version = "1.21.9";
    modern.pack_format = 69;
    const auto modern_scan = aurora::scan_resource_packs(modern_dir, modern);
    expect(
        modern_scan.size() == 1 &&
            modern_scan[0].compatibility ==
                aurora::ResourcePackCompatibility::Compatible,
        "min_format/max_format ranges cover newer resource versions");

    const auto missing_dir = make_temp_dir("missing-meta");
    aurora::ensure_directory(missing_dir / "resourcepacks");
    aurora::write_text_file(missing_dir / "resourcepacks" / "n.zip", "zip");
    const auto missing = aurora::scan_resource_packs(
        missing_dir, aurora::resource_pack_context(missing_dir, "1.21.1"));
    expect(
        missing.size() == 1 &&
            missing[0].compatibility ==
                aurora::ResourcePackCompatibility::MissingMetadata,
        "a pack without pack.mcmeta reports missing metadata");

    cleanup(game_dir);
    cleanup(legacy_dir);
    cleanup(modern_dir);
    cleanup(missing_dir);
}

} // namespace

int main() {
    try {
        test_scan_and_toggle();
        test_missing_options_and_line_endings();
        test_pack_mcmeta_compatibility();
    } catch (const std::exception& error) {
        std::cerr << "Unexpected exception: " << error.what() << '\n';
        return 1;
    }

    if (failures != 0) {
        std::cerr << failures << " resource pack test(s) failed\n";
        return 1;
    }
    std::cout << "All resource pack tests passed\n";
    return 0;
}
