#include "core/catalog.hpp"
#include "core/error.hpp"
#include "core/json.hpp"
#include "core/paths.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
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
                      ("aurora-catalog-tests-" + name + "-" + std::to_string(stamp));
    std::filesystem::create_directories(path);
    return path;
}

void cleanup(const std::filesystem::path& directory) {
    std::error_code error;
    std::filesystem::remove_all(directory, error);
}

constexpr std::string_view kPayload = "shader payload\n";
constexpr std::string_view kPayloadSha1 = "d8705428f4b124ccbeac645933e40c5e53bae956";

aurora::Json make_file(std::string_view url,
                       std::string_view filename,
                       std::uint64_t size,
                       std::string_view sha1,
                       bool primary) {
    aurora::Json hashes = aurora::Json::object();
    hashes["sha1"] = sha1;
    aurora::Json file = aurora::Json::object();
    file["url"] = url;
    file["filename"] = filename;
    file["size"] = size;
    file["hashes"] = std::move(hashes);
    file["primary"] = primary;
    return file;
}

aurora::Json make_version(std::string_view id,
                          std::string_view project_id,
                          std::string_view number,
                          std::string_view type,
                          aurora::Json files,
                          aurora::Json game_versions = aurora::Json::array({"1.21.1"})) {
    aurora::Json version = aurora::Json::object();
    version["id"] = id;
    version["project_id"] = project_id;
    version["name"] = number;
    version["version_number"] = number;
    version["version_type"] = type;
    version["date_published"] = "2026-01-02T03:04:05Z";
    version["game_versions"] = std::move(game_versions);
    version["loaders"] = aurora::Json::array({"iris"});
    version["files"] = std::move(files);
    version["dependencies"] = aurora::Json::array();
    return version;
}

void test_kinds_and_urls() {
    aurora::CatalogKind kind = aurora::CatalogKind::Modpack;
    expect(aurora::catalog_kind_from("shader", kind) &&
               kind == aurora::CatalogKind::Shader,
           "shader kind parses");
    expect(aurora::catalog_kind_from("ResourcePack", kind) &&
               kind == aurora::CatalogKind::ResourcePack,
           "resourcepack kind parses case-insensitively");
    expect(!aurora::catalog_kind_from("mod", kind), "mod is not a catalog kind");
    expect(aurora::catalog_kind_name(aurora::CatalogKind::Modpack) == "modpack",
           "modpack name");
    expect(aurora::catalog_file_extension(aurora::CatalogKind::Modpack) == ".mrpack",
           "modpack extension");
    expect(aurora::catalog_file_extension(aurora::CatalogKind::Shader) == ".zip",
           "shader extension");

    const std::string search = aurora::modrinth_catalog_search_url(
        aurora::CatalogKind::Shader, "", "1.21.1", "", 12);
    expect(search.find("index=downloads") != std::string::npos,
           "empty query sorts by downloads");
    expect(search.find("project_type%3Ashader") != std::string::npos,
           "search facets use the shader project type");
    expect(search.find("versions%3A1.21.1") != std::string::npos,
           "search facets filter the game version");

    const std::string pack_search = aurora::modrinth_catalog_search_url(
        aurora::CatalogKind::Modpack, "adventure", "", "fabric", 20);
    expect(pack_search.find("index=relevance") != std::string::npos,
           "non-empty query sorts by relevance");
    expect(pack_search.find("categories%3Afabric") != std::string::npos,
           "modpack search filters the loader");

    expect(
        aurora::modrinth_catalog_versions_url("fabulously-optimized", "1.21.1", "") ==
            "https://api.modrinth.com/v2/project/fabulously-optimized/version?"
            "game_versions=%5B%221.21.1%22%5D",
        "catalog version URL omits the loader");
}

void test_search_parsing() {
    const std::string body = R"({
      "total_hits": 2,
      "hits": [
        {"project_id":"a","slug":"bliss","title":"Bliss","description":"Shader",
         "author":"x","downloads":42,"categories":["iris"],"icon_url":"https://cdn.example/a.png",
         "project_type":"shader"},
        {"project_id":"b","slug":"sodium","title":"Sodium","description":"Mod",
         "author":"y","downloads":10,"categories":[],"project_type":"mod"}
      ]
    })";
    aurora::CatalogSearchOptions options;
    options.kind = aurora::CatalogKind::Shader;
    options.query = "bliss";
    options.fetch = [&](std::string_view url) {
        expect(url.find("project_type%3Ashader") != std::string::npos,
               "search fetch uses the shader URL");
        return body;
    };
    const aurora::ModrinthSearchResult result = aurora::search_modrinth_catalog(options);
    expect(result.total_hits == 2, "total hits is preserved");
    expect(result.hits.size() == 1, "hits of another project type are dropped");
    expect(result.hits.front().icon_url == "https://cdn.example/a.png", "icon URL parses");
    expect(result.hits.front().project_type == "shader", "project type parses");
}

void test_version_filtering() {
    const aurora::Json versions = aurora::Json::array(
        {make_version("v1", "p", "1.0", "release",
                      aurora::Json::array({make_file("https://cdn.example/a.zip", "a.zip", 10,
                                                     kPayloadSha1, false)})),
         make_version("v2", "p", "2.0", "release",
                      aurora::Json::array({make_file("https://cdn.example/a.jar", "a.jar", 10,
                                                     kPayloadSha1, true)})),
         make_version("v3", "p", "3.0", "release",
                      aurora::Json::array({make_file("https://cdn.example/b.zip", "b.zip", 10,
                                                     kPayloadSha1, true)}),
                      aurora::Json::array({"1.20.1"}))});
    aurora::CatalogVersionQuery query;
    query.kind = aurora::CatalogKind::ResourcePack;
    query.project = "p";
    query.game_version = "1.21.1";
    query.fetch = [&](std::string_view) { return versions.dump(); };
    const auto parsed = aurora::fetch_modrinth_catalog_versions(query);
    expect(parsed.size() == 1, "versions without a .zip or with another game version are dropped");
    expect(parsed.front().id == "v1", "the usable version survives");
}

void test_install() {
    const std::filesystem::path dir = make_temp_dir("install");
    const std::string project_body = R"({
      "id":"shaderproj","slug":"shader-pack","title":"Shader Pack",
      "description":"d","project_type":"shader"
    })";
    const aurora::Json version_body = make_version(
        "ver123", "shaderproj", "1.2", "release",
        aurora::Json::array({make_file("https://cdn.example/shader-pack.zip",
                                       "Shader Pack.zip", kPayload.size(), kPayloadSha1, true)}));
    int downloads = 0;
    aurora::CatalogInstallOptions options;
    options.kind = aurora::CatalogKind::Shader;
    options.target_dir = dir;
    options.project = "shader-pack";
    options.version_id = "ver123";
    options.game_version = "1.21.1";
    options.fetch = [&](std::string_view url) -> std::string {
        if (url.find("/version/ver123") != std::string::npos) {
            return version_body.dump();
        }
        if (url.find("/project/shader-pack") != std::string::npos) {
            return project_body;
        }
        expect(false, "unexpected fetch URL: " + std::string(url));
        return "{}";
    };
    options.download = [&](std::string_view url, const std::filesystem::path& destination,
                           std::string_view sha1) {
        expect(url == "https://cdn.example/shader-pack.zip", "download URL");
        expect(sha1 == kPayloadSha1, "download SHA-1");
        ++downloads;
        std::ofstream(destination, std::ios::binary) << kPayload;
    };

    const aurora::CatalogInstallResult result =
        aurora::install_modrinth_catalog_file(options);
    expect(downloads == 1, "the file is downloaded once");
    expect(result.downloaded, "first install reports a download");
    expect(result.file.filename == "Shader Pack.zip", "file metadata is returned");
    expect(std::filesystem::is_regular_file(dir / "Shader Pack.zip"),
           "file lands in the target directory");
    expect(result.version.id == "ver123", "version is returned");

    const aurora::CatalogInstallResult second =
        aurora::install_modrinth_catalog_file(options);
    expect(downloads == 1, "an existing verified file is not downloaded again");
    expect(!second.downloaded, "second install is a no-op");

    cleanup(dir);
}

void test_rejections() {
    const std::filesystem::path dir = make_temp_dir("reject");
    const std::string shader_project = R"({
      "id":"p","slug":"p","title":"P","description":"d","project_type":"shader"
    })";
    const aurora::Json unsafe_version = make_version(
        "bad", "p", "1.0", "release",
        aurora::Json::array({make_file("https://cdn.example/a.zip", "../escape.zip", 1,
                                       kPayloadSha1, true)}));

    aurora::CatalogInstallOptions base;
    base.kind = aurora::CatalogKind::Shader;
    base.target_dir = dir;
    base.project = "p";
    base.version_id = "bad";
    base.fetch = [&](std::string_view url) -> std::string {
        return url.find("/version/") != std::string::npos ? unsafe_version.dump()
                                                          : shader_project;
    };
    bool threw = false;
    try {
        aurora::install_modrinth_catalog_file(base);
    } catch (const aurora::Error&) {
        threw = true;
    }
    expect(threw, "a path-escaping filename is rejected");

    // A resource pack request for a shader project must fail before any file work.
    aurora::CatalogInstallOptions wrong_kind;
    wrong_kind.kind = aurora::CatalogKind::ResourcePack;
    wrong_kind.target_dir = dir;
    wrong_kind.project = "p";
    wrong_kind.version_id = "bad";
    wrong_kind.fetch = [&](std::string_view url) -> std::string {
        return url.find("/version/") != std::string::npos ? unsafe_version.dump()
                                                          : shader_project;
    };
    threw = false;
    try {
        aurora::install_modrinth_catalog_file(wrong_kind);
    } catch (const aurora::Error&) {
        threw = true;
    }
    expect(threw, "a project of another type is rejected");

    cleanup(dir);
}

} // namespace

int main() {
    test_kinds_and_urls();
    test_search_parsing();
    test_version_filtering();
    test_install();
    test_rejections();
    if (failures != 0) {
        std::cerr << failures << " catalog test(s) failed\n";
        return 1;
    }
    std::cout << "catalog tests passed\n";
    return 0;
}
