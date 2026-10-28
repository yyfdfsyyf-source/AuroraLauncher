#include "core/curseforge.hpp"
#include "core/error.hpp"
#include "core/json.hpp"
#include "core/modpack.hpp"
#include "core/paths.hpp"
#include "core/repository.hpp"
#include "core/zip.hpp"

#include <miniz.h>

#include <chrono>
#include <exception>
#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <string_view>
#include <utility>
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
                      ("aurora-modpack-tests-" + name + "-" + std::to_string(stamp));
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

void write_zip(const std::filesystem::path& path,
               const std::vector<std::pair<std::string, std::string>>& entries) {
    mz_zip_archive archive{};
    const std::string filename = aurora::path_to_utf8(path);
    if (!mz_zip_writer_init_file(&archive, filename.c_str(), 0)) {
        throw aurora::Error("Unable to create test ZIP: " + filename);
    }
    for (const auto& [name, content] : entries) {
        if (!mz_zip_writer_add_mem(&archive, name.c_str(), content.data(),
                                   content.size(),
                                   static_cast<mz_uint>(MZ_DEFAULT_COMPRESSION))) {
            mz_zip_writer_end(&archive);
            throw aurora::Error("Unable to add test ZIP entry: " + name);
        }
    }
    if (!mz_zip_writer_finalize_archive(&archive)) {
        mz_zip_writer_end(&archive);
        throw aurora::Error("Unable to finalize test ZIP: " + filename);
    }
    mz_zip_writer_end(&archive);
}

constexpr std::string_view kModSha1 = "b966a51ca16425938eb9c962c9b361c8752adca3";
constexpr std::string_view kCfSha1 = "74d72c6c760a7e79aee531a741b3c37a8683f044";

std::string mrpack_json(std::string_view file_path = "mods/test.jar",
                        std::string_view url =
                            "https://cdn.modrinth.com/data/x/versions/1/mod.jar") {
    aurora::Json root = aurora::Json::object();
    root["formatVersion"] = 1;
    root["game"] = "minecraft";
    root["name"] = "Test Pack";
    root["versionId"] = "1.0.0";
    root["summary"] = "test";
    root["dependencies"] = aurora::Json::object(
        {{"minecraft", "1.21.1"}, {"fabric-loader", "0.16.10"}});
    aurora::Json file = aurora::Json::object();
    file["path"] = std::string(file_path);
    file["hashes"] = aurora::Json::object({{"sha1", std::string(kModSha1)}});
    file["downloads"] = aurora::Json::array({std::string(url)});
    file["fileSize"] = 11;
    file["env"] = aurora::Json::object({{"client", "required"}});
    root["files"] = aurora::Json::array({file});
    return root.dump();
}

std::string curseforge_manifest_json() {
    aurora::Json loader = aurora::Json::object(
        {{"id", "forge-47.2.0"}, {"primary", true}});
    aurora::Json minecraft = aurora::Json::object(
        {{"version", "1.20.1"}, {"modLoaders", aurora::Json::array({loader})}});
    aurora::Json required = aurora::Json::object(
        {{"projectID", 100}, {"fileID", 200}, {"required", true}});
    aurora::Json optional = aurora::Json::object(
        {{"projectID", 101}, {"fileID", 201}, {"required", false}});
    aurora::Json root = aurora::Json::object();
    root["manifestType"] = "minecraftModpack";
    root["manifestVersion"] = 1;
    root["name"] = "CF Pack";
    root["version"] = "2.0";
    root["author"] = "author";
    root["minecraft"] = minecraft;
    root["files"] = aurora::Json::array({required, optional});
    root["overrides"] = "overrides";
    return root.dump();
}

void test_parse_modrinth() {
    const auto info = aurora::parse_modrinth_index(mrpack_json());
    expect(
        info.format == "modrinth" && info.name == "Test Pack" &&
            info.version == "1.0.0" && info.minecraft_version == "1.21.1",
        "mrpack metadata");
    expect(
        info.loader == "fabric" && info.loader_version == "0.16.10",
        "mrpack loader dependency");
    expect(info.files.size() == 1, "mrpack files");
    expect(
        info.files[0].path == "mods/test.jar" && info.files[0].sha1 == kModSha1 &&
            info.files[0].url.rfind("https://", 0) == 0,
        "mrpack file fields");
    expect(
        info.override_prefixes.size() == 2 &&
            info.override_prefixes[0] == "overrides/",
        "mrpack override prefixes");

    const auto server_only = aurora::parse_modrinth_index(R"({
      "formatVersion": 1, "game": "minecraft", "name": "x", "versionId": "1",
      "dependencies": {"minecraft": "1.21.1"},
      "files": [
        {"path": "mods/server.jar", "hashes": {"sha1":
          "b966a51ca16425938eb9c962c9b361c8752adca3"},
         "downloads": ["https://cdn.modrinth.com/x.jar"],
         "env": {"client": "unsupported"}}
      ]
    })");
    expect(server_only.files.empty(), "server-only mrpack files are skipped");

    expect(
        throws([] { aurora::parse_modrinth_index(R"({"formatVersion":2})"); }),
        "unsupported mrpack formatVersion is rejected");
    expect(
        throws([] {
            aurora::parse_modrinth_index(R"({
              "formatVersion":1, "dependencies":{"minecraft":"1.21.1"},
              "dependencies2": {}
            })");
        }),
        "mrpack without files is rejected");
    expect(
        throws([] {
            aurora::parse_modrinth_index(R"({
              "formatVersion":1,
              "dependencies":{"minecraft":"1.21.1"},
              "files":[{"path":"mods/a.jar",
                "hashes":{"sha1":"b966a51ca16425938eb9c962c9b361c8752adca3"},
                "downloads":["http://insecure.example/a.jar"]}]
            })");
        }),
        "mrpack files need HTTPS downloads");
    expect(
        throws([] {
            aurora::parse_modrinth_index(R"({
              "formatVersion":1,
              "dependencies":{"minecraft":"1.21.1"},
              "files":[{"path":"mods/a.jar","hashes":{},
                "downloads":["https://cdn.modrinth.com/a.jar"]}]
            })");
        }),
        "mrpack files need SHA-1");
    expect(
        throws([] {
            aurora::parse_modrinth_index(R"({
              "formatVersion":1,
              "dependencies":{"minecraft":"1.21.1",
                "fabric-loader":"0.16.0","forge":"47.2.0"},
              "files":[]
            })");
        }),
        "multiple loaders are rejected");
}

void test_parse_curseforge() {
    const auto info = aurora::parse_curseforge_manifest(curseforge_manifest_json());
    expect(
        info.format == "curseforge" && info.name == "CF Pack" &&
            info.version == "2.0" && info.minecraft_version == "1.20.1",
        "CurseForge manifest metadata");
    expect(
        info.loader == "forge" && info.loader_version == "47.2.0",
        "CurseForge loader");
    expect(info.files.size() == 1, "optional CurseForge files are skipped");
    expect(
        info.files[0].project_id == 100 && info.files[0].file_id == 200,
        "CurseForge file ids");
    expect(
        info.override_prefixes.size() == 1 &&
            info.override_prefixes[0] == "overrides/",
        "CurseForge overrides prefix");

    expect(
        throws([] {
            aurora::parse_curseforge_manifest(R"({"manifestType":"other"})");
        }),
        "non-modpack manifest is rejected");
    expect(
        throws([] {
            aurora::parse_curseforge_manifest(R"({
              "manifestType":"minecraftModpack","manifestVersion":1,
              "minecraft":{"version":"1.20.1","modLoaders":[
                {"id":"liteloader-1.12.2","primary":true}]},
              "files":[]
            })");
        }),
        "unsupported CurseForge loader is rejected");
}

void test_loader_command() {
    const auto info = aurora::parse_modrinth_index(mrpack_json());
    const std::string command = aurora::modpack_loader_command(
        info, std::filesystem::path("D:\\games\\pack"));
    expect(
        command.find("aurora loader install fabric 1.21.1 --loader 0.16.10") == 0,
        "Fabric loader command");
    expect(
        command.find("--game-dir") != std::string::npos,
        "loader command carries the game directory");

    const auto cf_info =
        aurora::parse_curseforge_manifest(curseforge_manifest_json());
    expect(
        aurora::modpack_loader_command(cf_info, "D:\\games\\pack")
                .find("aurora forge install forge 1.20.1 --loader 47.2.0") == 0,
        "Forge loader command");
}

void test_zip_helpers() {
    const auto root = make_temp_dir("zip");
    const auto archive = root / "test.zip";
    write_zip(archive, {{"a.txt", "A"}, {"dir/b.txt", "B"}});

    const auto a = aurora::read_zip_entry(archive, "a.txt");
    expect(a && *a == "A", "single ZIP entry is read");
    expect(
        !aurora::read_zip_entry(archive, "missing.txt"),
        "missing ZIP entry is nullopt");

    const auto destination = root / "out";
    const std::size_t written =
        aurora::extract_zip_subtree(archive, "dir/", destination);
    expect(written == 1, "subtree extraction counts files");
    expect(
        std::filesystem::is_regular_file(destination / "b.txt") &&
            !std::filesystem::exists(destination / "a.txt"),
        "subtree extraction strips the prefix");
    cleanup(root);
}

void test_install_mrpack() {
    const auto root = make_temp_dir("mrpack");
    const auto archive = root / "pack.mrpack";
    const auto game_dir = root / "game";
    write_zip(archive,
              {{"modrinth.index.json", mrpack_json()},
               {"overrides/config/a.txt", "override base"},
               {"client-overrides/config/a.txt", "override client"},
               {"overrides/options.txt", "server option"},
               {"server-overrides/ignored.txt", "nope"}});

    aurora::ensure_directory(game_dir / "versions" / "1.21.1");
    aurora::write_text_file(
        game_dir / "versions" / "1.21.1" / "1.21.1.json",
        R"({"id":"1.21.1","mainClass":"net.minecraft.client.main.Main"})");

    std::map<std::string, std::string> downloads;
    downloads["https://cdn.modrinth.com/data/x/versions/1/mod.jar"] = "mod content";
    aurora::ModpackInstallOptions options;
    options.archive = archive;
    options.game_dir = game_dir;
    options.install_loader = false;
    options.install_game = false;
    options.download = [&downloads](std::string_view url,
                                    const std::filesystem::path& destination,
                                    std::string_view) {
        const auto entry = downloads.find(std::string(url));
        if (entry == downloads.end()) {
            throw aurora::Error("Unexpected download URL: " + std::string(url));
        }
        aurora::write_text_file(destination, entry->second);
    };

    const auto result = aurora::install_modpack(options);
    const auto instance = game_dir / "versions" / "Test Pack";
    expect(
        result.version_id == "Test Pack" && result.instance_dir == instance,
        "pack name becomes the instance id");
    expect(
        result.files_installed == 1 && result.files_skipped == 0,
        "mrpack file is installed");
    expect(
        aurora::read_text_file(instance / "mods" / "test.jar") == "mod content",
        "mrpack mod content is isolated in the instance");
    expect(
        aurora::read_text_file(instance / "config" / "a.txt") == "override client",
        "client overrides win over base overrides");
    expect(
        std::filesystem::is_regular_file(instance / "options.txt"),
        "base overrides are extracted");
    expect(
        !std::filesystem::exists(instance / "server-overrides"),
        "server overrides are not extracted");
    expect(
        std::filesystem::is_regular_file(instance / "Test Pack.json") &&
            aurora::read_json_file(instance / "Test Pack.json")["inheritsFrom"] ==
                "1.21.1",
        "instance version JSON inherits from the parent game version");
    expect(
        std::filesystem::is_regular_file(result.manifest_path) &&
            aurora::read_text_file(result.manifest_path).find("\"Test Pack\"") !=
                std::string::npos &&
            aurora::read_text_file(result.manifest_path).find("\"overrides\"") !=
                std::string::npos,
        "modpack marker records the pack and override hashes");
    const aurora::ResolvedVersion resolved =
        aurora::VersionRepository(game_dir).resolve(result.version_id);
    expect(
        resolved.run_dir == instance && resolved.game_dir == game_dir &&
            resolved.meta.main_class == "net.minecraft.client.main.Main",
        "launch resolves the instance run directory from modpack.json");

    const auto again = aurora::install_modpack(options);
    expect(
        again.files_installed == 0 && again.files_skipped == 1,
        "reinstall skips the verified file");
    expect(
        again.overrides_extracted == 2 && again.overrides_skipped == 0,
        "reinstall refreshes untouched overrides");

    aurora::write_text_file(instance / "config" / "a.txt", "user edit");
    options.force = true;
    const auto forced = aurora::install_modpack(options);
    expect(
        aurora::read_text_file(instance / "config" / "a.txt") == "override client",
        "--force replaces overrides");
    expect(forced.overrides_extracted == 2, "forced overrides are counted");

    const auto unsafe_archive = root / "unsafe.mrpack";
    write_zip(unsafe_archive, {{"modrinth.index.json",
                                mrpack_json("../evil.jar")}});
    options.archive = unsafe_archive;
    options.game_dir = root / "unsafe-game";
    options.force = false;
    expect(
        throws([&options] { aurora::install_modpack(options); }),
        "unsafe modpack paths are rejected");
    expect(
        !std::filesystem::exists(root / "evil.jar"),
        "unsafe modpack file is not written");

    cleanup(root);
}

void test_install_curseforge() {
    const auto root = make_temp_dir("curseforge");
    const auto archive = root / "pack.zip";
    const auto game_dir = root / "game";
    write_zip(archive,
              {{"manifest.json", curseforge_manifest_json()},
               {"overrides/config/b.txt", "cf override"}});

    const std::string file_url = aurora::curseforge_file_url(100, 200);
    const std::string mod_url = aurora::curseforge_mod_url(100);
    aurora::Json file = aurora::Json::object();
    file["id"] = 200;
    file["modId"] = 100;
    file["fileName"] = "cf.jar";
    file["downloadUrl"] = "https://mediafilez.forgecdn.net/files/2/200/cf.jar";
    file["fileLength"] = 10;
    file["releaseType"] = 1;
    file["fileDate"] = "2025-01-01T00:00:00Z";
    file["isAvailable"] = true;
    file["hashes"] = aurora::Json::array(
        {aurora::Json::object({{"algo", 1}, {"value", std::string(kCfSha1)}})});
    aurora::Json mod = aurora::Json::object();
    mod["id"] = 100;
    mod["name"] = "CF Mod";
    mod["classId"] = 6;
    mod["allowModDistribution"] = true;
    mod["authors"] = aurora::Json::array();

    aurora::ModpackInstallOptions options;
    options.archive = archive;
    options.game_dir = game_dir;
    options.install_loader = false;
    options.install_game = false;
    options.fetch = [&file, &mod, &file_url, &mod_url](std::string_view url) {
        if (url == file_url) {
            return aurora::Json::object({{"data", file}}).dump();
        }
        if (url == mod_url) {
            return aurora::Json::object({{"data", mod}}).dump();
        }
        throw aurora::Error("Unexpected CurseForge URL: " + std::string(url));
    };
    options.download = [](std::string_view url,
                          const std::filesystem::path& destination,
                          std::string_view) {
        if (url != "https://mediafilez.forgecdn.net/files/2/200/cf.jar") {
            throw aurora::Error("Unexpected download URL: " + std::string(url));
        }
        aurora::write_text_file(destination, "cf content");
    };

    const auto result = aurora::install_modpack(options);
    const auto instance = game_dir / "versions" / "CF Pack";
    expect(
        result.info.format == "curseforge" && result.files_installed == 1,
        "CurseForge pack files are installed");
    expect(
        aurora::read_text_file(instance / "mods" / "cf.jar") == "cf content",
        "CurseForge file lands in mods");
    expect(
        aurora::read_text_file(instance / "config" / "b.txt") == "cf override",
        "CurseForge overrides are extracted");

    aurora::Json blocked = file;
    blocked["downloadUrl"] = nullptr;
    options.fetch = [&blocked, &file_url](std::string_view url) {
        if (url == file_url) {
            return aurora::Json::object({{"data", blocked}}).dump();
        }
        return aurora::Json::object({{"data", aurora::Json::object()}}).dump();
    };
    options.game_dir = root / "blocked-game";
    bool threw = false;
    try {
        aurora::install_modpack(options);
    } catch (const aurora::Error& error) {
        threw = std::string(error.what()).find("disabled third-party distribution") !=
                std::string::npos;
    }
    expect(threw, "CurseForge files without download URLs are rejected");

    cleanup(root);
}

std::string empty_mrpack_json(std::string_view name, std::string_view version) {
    aurora::Json root = aurora::Json::object();
    root["formatVersion"] = 1;
    root["game"] = "minecraft";
    root["name"] = std::string(name);
    root["versionId"] = std::string(version);
    root["dependencies"] = aurora::Json::object(
        {{"minecraft", "1.21.1"}, {"fabric-loader", "0.16.10"}});
    root["files"] = aurora::Json::array();
    return root.dump();
}

void test_instance_loader_parent() {
    const auto root = make_temp_dir("loader");
    const auto archive = root / "pack.mrpack";
    const auto game_dir = root / "game";
    write_zip(archive, {{"modrinth.index.json", empty_mrpack_json("Loader Pack", "1")}});

    aurora::ModpackInstallOptions options;
    options.archive = archive;
    options.game_dir = game_dir;
    options.install_game = false;
    options.loader_installer = [](const aurora::ModpackInfo& info,
                                  const std::filesystem::path&) {
        return std::string(info.loader) + "-loader-" + info.loader_version + "-" +
               info.minecraft_version;
    };

    const auto result = aurora::install_modpack(options);
    expect(
        result.loader_version_id == "fabric-loader-0.16.10-1.21.1",
        "loader installer result is used as the parent");
    const auto profile = aurora::read_json_file(result.version_json);
    expect(
        profile["inheritsFrom"] == "fabric-loader-0.16.10-1.21.1",
        "instance version JSON inherits from the installed loader");
    expect(
        result.version_json.parent_path() == result.instance_dir &&
            std::filesystem::is_regular_file(result.instance_dir / "modpack.json"),
        "instance files live under versions/<id>");
    cleanup(root);
}

void test_upgrade_preserves_user_files() {
    const auto root = make_temp_dir("upgrade");
    const auto game_dir = root / "game";
    const auto archive_v1 = root / "v1.mrpack";
    const auto archive_v2 = root / "v2.mrpack";
    write_zip(archive_v1,
              {{"modrinth.index.json", mrpack_json()},
               {"overrides/config/a.txt", "v1 a"},
               {"overrides/config/keep.txt", "v1 keep"},
               {"overrides/options.txt", "v1 options"}});

    std::map<std::string, std::string> downloads;
    downloads["https://cdn.modrinth.com/data/x/versions/1/mod.jar"] = "mod content";
    aurora::ModpackInstallOptions options;
    options.archive = archive_v1;
    options.game_dir = game_dir;
    options.install_loader = false;
    options.install_game = false;
    options.download = [&downloads](std::string_view url,
                                    const std::filesystem::path& destination,
                                    std::string_view) {
        aurora::write_text_file(destination, downloads.at(std::string(url)));
    };
    aurora::install_modpack(options);

    const auto instance = game_dir / "versions" / "Test Pack";
    aurora::write_text_file(instance / "config" / "a.txt", "user edit");
    std::filesystem::remove(instance / "options.txt");

    write_zip(archive_v2,
              {{"modrinth.index.json", mrpack_json()},
               {"overrides/config/a.txt", "v2 a"},
               {"overrides/config/keep.txt", "v2 keep"},
               {"overrides/config/new.txt", "v2 new"}});
    options.archive = archive_v2;
    options.backup = true;
    const auto upgraded = aurora::install_modpack(options);

    expect(upgraded.upgraded && !upgraded.backup_path.empty(),
           "upgrade is detected and backed up");
    expect(
        std::filesystem::is_regular_file(upgraded.backup_path / "config" / "a.txt"),
        "backup contains the instance before the upgrade");
    expect(
        aurora::read_text_file(instance / "config" / "a.txt") == "user edit",
        "upgrade keeps user-modified overrides");
    expect(
        aurora::read_text_file(instance / "config" / "keep.txt") == "v2 keep",
        "upgrade refreshes untouched overrides");
    expect(
        aurora::read_text_file(instance / "config" / "new.txt") == "v2 new",
        "upgrade extracts new overrides");
    expect(
        !std::filesystem::exists(instance / "options.txt"),
        "user deletion is preserved and dropped overrides stay absent");
    expect(
        upgraded.overrides_extracted == 2 && upgraded.overrides_skipped == 1,
        "upgrade override counters");
    cleanup(root);
}

void test_upgrade_failure_restores_backup() {
    const auto root = make_temp_dir("rollback");
    const auto game_dir = root / "game";
    const auto archive_v1 = root / "v1.mrpack";
    const auto archive_v2 = root / "v2.mrpack";
    write_zip(archive_v1,
              {{"modrinth.index.json", mrpack_json()},
               {"overrides/config/a.txt", "v1"}});

    aurora::ModpackInstallOptions options;
    options.archive = archive_v1;
    options.game_dir = game_dir;
    options.install_loader = false;
    options.install_game = false;
    options.download = [](std::string_view,
                          const std::filesystem::path& destination,
                          std::string_view) {
        aurora::write_text_file(destination, "mod content");
    };
    aurora::install_modpack(options);
    const auto instance = game_dir / "versions" / "Test Pack";
    aurora::write_text_file(instance / "config" / "a.txt", "user edit");

    write_zip(archive_v2,
              {{"modrinth.index.json",
                mrpack_json("mods/test2.jar",
                            "https://cdn.modrinth.com/data/x/versions/2/mod2.jar")},
               {"overrides/config/a.txt", "v2"}});
    options.archive = archive_v2;
    options.download = [](std::string_view,
                          const std::filesystem::path&,
                          std::string_view) {
        throw aurora::Error("simulated download failure");
    };
    expect(
        throws([&options] { aurora::install_modpack(options); }),
        "failed upgrade reports the error");
    expect(
        aurora::read_text_file(instance / "config" / "a.txt") == "user edit" &&
            aurora::read_text_file(instance / "mods" / "test.jar") ==
                "mod content",
        "failed upgrade restores the pre-upgrade instance");
    cleanup(root);
}

void test_curseforge_removes_stale_files() {
    const auto root = make_temp_dir("cf-upgrade");
    const auto game_dir = root / "game";
    const auto archive_v1 = root / "v1.zip";
    const auto archive_v2 = root / "v2.zip";
    write_zip(archive_v1, {{"manifest.json", curseforge_manifest_json()}});

    aurora::Json file = aurora::Json::object();
    file["id"] = 200;
    file["modId"] = 100;
    file["fileName"] = "cf.jar";
    file["downloadUrl"] = "https://mediafilez.forgecdn.net/files/2/200/cf.jar";
    file["hashes"] = aurora::Json::array(
        {aurora::Json::object({{"algo", 1}, {"value", std::string(kCfSha1)}})});
    aurora::Json mod = aurora::Json::object(
        {{"id", 100}, {"classId", 6}, {"name", "CF Mod"}});

    aurora::ModpackInstallOptions options;
    options.archive = archive_v1;
    options.game_dir = game_dir;
    options.install_loader = false;
    options.install_game = false;
    options.fetch = [&file, &mod](std::string_view url) {
        if (std::string(url) == aurora::curseforge_file_url(100, 200)) {
            return aurora::Json::object({{"data", file}}).dump();
        }
        if (std::string(url) == aurora::curseforge_mod_url(100)) {
            return aurora::Json::object({{"data", mod}}).dump();
        }
        throw aurora::Error("Unexpected CurseForge URL: " + std::string(url));
    };
    options.download = [](std::string_view,
                          const std::filesystem::path& destination,
                          std::string_view) {
        aurora::write_text_file(destination, "cf content");
    };
    aurora::install_modpack(options);
    const auto instance = game_dir / "versions" / "CF Pack";
    expect(std::filesystem::is_regular_file(instance / "mods" / "cf.jar"),
           "CurseForge file is installed before the upgrade");

    // A CurseForge manifest with no mods; the removed file must disappear.
    write_zip(archive_v2,
              {{"manifest.json", R"({
                "manifestType":"minecraftModpack","manifestVersion":1,
                "name":"CF Pack","version":"3.0",
                "minecraft":{"version":"1.20.1","modLoaders":[]},
                "files":[],"overrides":"overrides"
              })"}});
    options.archive = archive_v2;
    const auto upgraded = aurora::install_modpack(options);
    expect(
        upgraded.files_removed == 1 &&
            !std::filesystem::exists(instance / "mods" / "cf.jar"),
        "CurseForge upgrade removes files dropped from the manifest");
    cleanup(root);
}

void test_instance_collision() {
    const auto root = make_temp_dir("collision");
    const auto archive = root / "pack.mrpack";
    const auto game_dir = root / "game";
    write_zip(archive, {{"modrinth.index.json", empty_mrpack_json("Clash", "1")}});
    const auto existing = game_dir / "versions" / "Clash";
    std::filesystem::create_directories(existing);
    aurora::write_text_file(existing / "Clash.json", R"({"id":"Clash"})");

    aurora::ModpackInstallOptions options;
    options.archive = archive;
    options.game_dir = game_dir;
    options.install_loader = false;
    options.install_game = false;
    expect(
        throws([&options] { aurora::install_modpack(options); }),
        "installing over a non-modpack version is refused");
    cleanup(root);
}

} // namespace

int main() {
    try {
        test_parse_modrinth();
        test_parse_curseforge();
        test_loader_command();
        test_zip_helpers();
        test_install_mrpack();
        test_install_curseforge();
        test_instance_loader_parent();
        test_upgrade_preserves_user_files();
        test_upgrade_failure_restores_backup();
        test_curseforge_removes_stale_files();
        test_instance_collision();
    } catch (const std::exception& error) {
        std::cerr << "Unexpected exception: " << error.what() << '\n';
        return 1;
    }

    if (failures != 0) {
        std::cerr << failures << " modpack test(s) failed\n";
        return 1;
    }
    std::cout << "All modpack tests passed\n";
    return 0;
}
