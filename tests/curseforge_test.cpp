#include "core/curseforge.hpp"
#include "core/error.hpp"
#include "core/json.hpp"
#include "core/mods.hpp"
#include "core/paths.hpp"

#include <chrono>
#include <exception>
#include <filesystem>
#include <functional>
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
                      ("aurora-curseforge-tests-" + name + "-" + std::to_string(stamp));
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

constexpr std::string_view kRootSha1 = "c3b564f18bea41407e7243da3aec1cd04b245689";
constexpr std::string_view kDepSha1 = "f3f8c6bc997b6ad0c59d080969a74e7bc387ca39";
constexpr std::string_view kBetaSha1 = "4b03391be84a81994836f670f8f499b5db9e83c3";

std::string file_json(std::uint64_t id,
                      std::uint64_t mod_id,
                      std::string_view filename,
                      std::string_view download_url,
                      std::string_view date,
                      int release_type,
                      std::string_view sha1,
                      std::string_view dependencies = "[]") {
    std::string json = "{\"id\":" + std::to_string(id) +
                       ",\"modId\":" + std::to_string(mod_id) +
                       ",\"displayName\":\"file " + std::to_string(id) +
                       "\",\"fileName\":\"" + std::string(filename) + "\"";
    if (download_url.empty()) {
        json += ",\"downloadUrl\":null";
    } else {
        json += ",\"downloadUrl\":\"" + std::string(download_url) + "\"";
    }
    json += ",\"fileLength\":12,\"releaseType\":" + std::to_string(release_type) +
            ",\"fileDate\":\"" + std::string(date) + "\",\"isAvailable\":true";
    if (sha1.empty()) {
        json += ",\"hashes\":[]";
    } else {
        json += ",\"hashes\":[{\"algo\":1,\"value\":\"" + std::string(sha1) +
                "\"}]";
    }
    json += ",\"dependencies\":" + std::string(dependencies) + "}";
    return json;
}

void test_urls_and_loader_types() {
    expect(aurora::curseforge_loader_type("") == 0, "empty loader means no filter");
    expect(aurora::curseforge_loader_type("forge") == 1, "Forge loader type");
    expect(aurora::curseforge_loader_type("fabric") == 4, "Fabric loader type");
    expect(aurora::curseforge_loader_type("quilt") == 5, "Quilt loader type");
    expect(aurora::curseforge_loader_type("neoforge") == 6, "NeoForge loader type");
    expect(
        throws([] { aurora::curseforge_loader_type("liteloader"); }),
        "unknown loader is rejected");

    expect(
        aurora::curseforge_search_url("jei", "fabric", "1.21.1", 5) ==
            "https://api.curseforge.com/v1/mods/search?gameId=432&classId=6"
            "&sortField=2&sortOrder=desc&pageSize=5&searchFilter=jei"
            "&modLoaderType=4&gameVersion=1.21.1",
        "search URL filters loader and game version");
    expect(
        aurora::curseforge_search_url("sodium extra", "", "", 10).find(
            "searchFilter=sodium%20extra") != std::string::npos,
        "search query is encoded");
    expect(
        aurora::curseforge_mod_url(42) ==
            "https://api.curseforge.com/v1/mods/42",
        "mod URL");
    expect(
        aurora::curseforge_files_url(42, "fabric", "1.21.1") ==
            "https://api.curseforge.com/v1/mods/42/files?pageSize=50"
            "&modLoaderType=4&gameVersion=1.21.1",
        "files URL filters loader and game version");
    expect(
        aurora::curseforge_file_url(42, 100) ==
            "https://api.curseforge.com/v1/mods/42/files/100",
        "single file URL");

    expect(
        throws([] { aurora::curseforge_search_url("x", "", "", 0); }),
        "search limit is validated");
    expect(
        throws([] { aurora::curseforge_mod_url(0); }), "mod id is validated");
    expect(
        throws([] {
            aurora::CurseForgeEndpoints endpoints;
            endpoints.api_root = "http://api.curseforge.com";
            aurora::curseforge_mod_url(1, endpoints);
        }),
        "API root must use HTTPS");
    expect(
        throws([] { aurora::curseforge_files_url(1, "fabric", "../1.21.1"); }),
        "game version is validated");
}

void test_search_parsing() {
    const std::string search = R"({
  "data": [
    {
      "id": 238222,
      "name": "Just Enough Items",
      "slug": "jei",
      "summary": "View items and recipes",
      "downloadCount": 123456,
      "allowModDistribution": true,
      "authors": [{"name": "mezz"}]
    },
    {
      "id": 306612,
      "name": "Fabric API",
      "slug": "fabric-api",
      "summary": "",
      "downloadCount": 42,
      "authors": []
    }
  ],
  "pagination": {"totalCount": 2}
})";
    const auto result = aurora::parse_curseforge_search(search);
    expect(result.total_count == 2 && result.mods.size() == 2, "search is parsed");
    expect(
        result.mods[0].id == 238222 && result.mods[0].name == "Just Enough Items" &&
            result.mods[0].slug == "jei" && result.mods[0].author == "mezz" &&
            result.mods[0].downloads == 123456 &&
            result.mods[0].allow_mod_distribution,
        "search entry fields");
    expect(result.mods[1].author.empty(), "missing author is empty");

    expect(
        throws([] { aurora::parse_curseforge_search("[]"); }),
        "search response must be an object");
    expect(
        throws([] { aurora::parse_curseforge_search(R"({"data":{}})"); }),
        "search data must be an array");

    const std::string mod = R"({"data":{
  "id": 42, "name": "JEI", "slug": "jei", "summary": "x",
  "downloadCount": 7, "allowModDistribution": false,
  "authors": [{"name": "mezz"}]
}})";
    const auto parsed_mod = aurora::parse_curseforge_mod(mod);
    expect(
        parsed_mod.id == 42 && parsed_mod.name == "JEI" &&
            !parsed_mod.allow_mod_distribution,
        "single mod is parsed");
    expect(
        throws([] { aurora::parse_curseforge_mod(R"({"data":[]})"); }),
        "mod data must be an object");
}

void test_file_parsing_and_selection() {
    const std::string dependencies =
        R"([{"modId":77,"relationType":3},{"modId":88,"relationType":2}])";
    const std::string files = "{\"data\":[" +
        file_json(100, 42, "jei-1.0.jar",
                  "https://mediafilez.forgecdn.net/files/1/100/jei-1.0.jar",
                  "2025-01-01T00:00:00Z", 1, kRootSha1, dependencies) + "," +
        file_json(101, 42, "jei-1.1-beta.jar",
                  "https://mediafilez.forgecdn.net/files/1/101/jei-1.1-beta.jar",
                  "2025-02-01T00:00:00Z", 2, kBetaSha1) + "," +
        file_json(102, 42, "jei-nourl.jar", "", "2025-03-01T00:00:00Z", 1,
                  kRootSha1) + "," +
        file_json(103, 42, "jei-nohash.jar",
                  "https://mediafilez.forgecdn.net/files/1/103/jei-nohash.jar",
                  "2025-04-01T00:00:00Z", 1, "") + "," +
        "{\"id\":104,\"modId\":42,\"fileName\":\"../evil.jar\","
        "\"downloadUrl\":\"https://mediafilez.forgecdn.net/files/1/104/x.jar\","
        "\"releaseType\":1,\"fileDate\":\"2025-05-01T00:00:00Z\","
        "\"hashes\":[{\"algo\":1,\"value\":\"" + std::string(kRootSha1) + "\"}]}" +
        "]}";

    const auto parsed = aurora::parse_curseforge_files(files);
    expect(parsed.size() == 5, "files are parsed");
    expect(
        parsed[0].sha1 == kRootSha1 && parsed[0].length == 12 &&
            parsed[0].release_type == 1 && parsed[0].available,
        "file fields");
    expect(
        parsed[0].dependencies.size() == 2 &&
            parsed[0].dependencies[0].mod_id == 77 &&
            parsed[0].dependencies[0].relation_type == 3,
        "file dependencies");

    const auto release = aurora::select_curseforge_file(parsed, 0, false);
    expect(
        release && release->id == 100, "release is preferred over a newer beta");
    const auto prerelease = aurora::select_curseforge_file(parsed, 0, true);
    expect(
        prerelease && prerelease->id == 101,
        "prerelease selection chooses the newest downloadable file");
    const auto explicit_beta = aurora::select_curseforge_file(parsed, 101, false);
    expect(
        explicit_beta && explicit_beta->id == 101,
        "explicit file bypasses the release filter");
    expect(
        !aurora::select_curseforge_file(parsed, 999, false),
        "unknown explicit file is not selected");
    expect(
        release && release->id != 102 && release->id != 103 && release->id != 104,
        "files without URL, SHA-1 or a safe name are skipped");

    expect(
        throws([] { aurora::parse_curseforge_files("{}"); }),
        "files response needs a data array");

    const std::string single = "{\"data\":" +
        file_json(100, 42, "jei.jar",
                  "https://mediafilez.forgecdn.net/files/1/100/jei.jar",
                  "2025-01-01T00:00:00Z", 1, kRootSha1) +
        "}";
    expect(
        aurora::parse_curseforge_file(single).id == 100, "single file is parsed");
}

struct FetchFixture {
    std::map<std::string, std::string> responses;
};

aurora::CurseForgeFetcher make_fetcher(const FetchFixture& fixture,
                                       std::vector<std::string>* requested = nullptr) {
    return [&fixture, requested](std::string_view url) -> std::string {
        const std::string key(url);
        if (requested != nullptr) {
            requested->push_back(key);
        }
        const auto entry = fixture.responses.find(key);
        if (entry == fixture.responses.end()) {
            throw aurora::Error("Unexpected CurseForge URL: " + key);
        }
        return entry->second;
    };
}

FetchFixture make_install_fixture() {
    FetchFixture fixture;
    const std::string mod_url = aurora::curseforge_mod_url(42);
    fixture.responses[mod_url] = R"({"data":{
  "id":42,"name":"JEI","slug":"jei","summary":"x","downloadCount":7,
  "allowModDistribution":true,"authors":[{"name":"mezz"}]}})";
    const std::string files_url =
        aurora::curseforge_files_url(42, "fabric", "1.21.1");
    fixture.responses[files_url] = "{\"data\":[" +
        file_json(100, 42, "jei-1.0.jar",
                  "https://mediafilez.forgecdn.net/files/1/100/jei-1.0.jar",
                  "2025-01-01T00:00:00Z", 1, kRootSha1,
                  R"([{"modId":77,"relationType":3},{"modId":88,"relationType":2}])") +
        "]}";
    fixture.responses[aurora::curseforge_mod_url(77)] = R"({"data":{
  "id":77,"name":"Dependency","slug":"dep","summary":"d","downloadCount":1,
  "allowModDistribution":true,"authors":[{"name":"dep-author"}]}})";
    fixture.responses[aurora::curseforge_files_url(77, "fabric", "1.21.1")] =
        "{\"data\":[" +
        file_json(200, 77, "dep-1.0.jar",
                  "https://mediafilez.forgecdn.net/files/2/200/dep-1.0.jar",
                  "2025-01-02T00:00:00Z", 1, kDepSha1) +
        "]}";
    return fixture;
}

void test_install() {
    const FetchFixture fixture = make_install_fixture();
    const auto game_dir = make_temp_dir("install");
    std::map<std::string, std::string> contents;
    contents["https://mediafilez.forgecdn.net/files/1/100/jei-1.0.jar"] =
        "root content";
    contents["https://mediafilez.forgecdn.net/files/2/200/dep-1.0.jar"] =
        "dep content";
    std::vector<std::pair<std::string, std::string>> download_calls;

    aurora::CurseForgeInstallOptions options;
    options.game_dir = game_dir;
    options.mod_id = 42;
    options.loader = "fabric";
    options.game_version = "1.21.1";
    options.fetch = make_fetcher(fixture);
    options.download = [&contents, &download_calls](
                           std::string_view url,
                           const std::filesystem::path& destination,
                           std::string_view sha1) {
        const std::string key(url);
        const auto entry = contents.find(key);
        if (entry == contents.end()) {
            throw aurora::Error("Unexpected download URL: " + key);
        }
        download_calls.emplace_back(key, std::string(sha1));
        aurora::write_text_file(destination, entry->second);
    };

    const auto result = aurora::install_curseforge_mod(options);
    expect(
        result.root.mod.id == 42 && result.root.file.id == 100 &&
            result.root.downloaded,
        "root mod is installed");
    expect(
        result.root.file_path == game_dir / "mods" / "jei-1.0.jar",
        "root file path");
    expect(
        result.dependencies.size() == 1 && result.dependencies[0].mod.id == 77 &&
            result.dependencies[0].dependency,
        "required dependency is installed");
    expect(download_calls.size() == 2, "two files are downloaded");
    expect(
        std::filesystem::is_regular_file(game_dir / "mods" / "dep-1.0.jar"),
        "dependency file exists");

    const auto again = aurora::install_curseforge_mod(options);
    expect(
        !again.root.downloaded && !again.dependencies[0].downloaded,
        "existing verified files are not downloaded again");

    const auto no_deps_dir = make_temp_dir("nodeps");
    options.game_dir = no_deps_dir;
    options.install_dependencies = false;
    const auto no_deps = aurora::install_curseforge_mod(options);
    expect(no_deps.dependencies.empty(), "--no-deps skips dependencies");
    expect(
        !std::filesystem::exists(no_deps_dir / "mods" / "dep-1.0.jar"),
        "dependency is not written with --no-deps");

    cleanup(game_dir);
    cleanup(no_deps_dir);
}

void test_install_rejects_unavailable_file() {
    FetchFixture fixture;
    fixture.responses[aurora::curseforge_mod_url(42)] = R"({"data":{
  "id":42,"name":"JEI","slug":"jei","summary":"x","downloadCount":7,
  "allowModDistribution":false,"authors":[]}})";
    fixture.responses[aurora::curseforge_files_url(42, "fabric", "1.21.1")] =
        "{\"data\":[" +
        file_json(100, 42, "jei.jar", "", "2025-01-01T00:00:00Z", 1, kRootSha1) +
        "]}";
    fixture.responses[aurora::curseforge_file_url(42, 100)] =
        "{\"data\":" +
        file_json(100, 42, "jei.jar", "", "2025-01-01T00:00:00Z", 1, kRootSha1) +
        "}";

    const auto game_dir = make_temp_dir("unavailable");
    aurora::CurseForgeInstallOptions options;
    options.game_dir = game_dir;
    options.mod_id = 42;
    options.file_id = 100;
    options.loader = "fabric";
    options.game_version = "1.21.1";
    options.fetch = make_fetcher(fixture);
    options.download = [](std::string_view, const std::filesystem::path&,
                          std::string_view) {
        throw aurora::Error("download must not be called");
    };

    bool threw = false;
    try {
        aurora::install_curseforge_mod(options);
    } catch (const aurora::Error& error) {
        threw = std::string(error.what()).find("disabled third-party distribution") !=
                std::string::npos;
    }
    expect(threw, "files without a download URL are rejected");
    expect(
        !std::filesystem::exists(game_dir / "mods" / "jei.jar"),
        "no file is written for an unavailable file");
    cleanup(game_dir);
}

void test_api_key_required() {
    const auto game_dir = make_temp_dir("key");
    aurora::CurseForgeInstallOptions options;
    options.game_dir = game_dir;
    options.mod_id = 42;
    options.loader = "fabric";
    options.game_version = "1.21.1";
    bool threw = false;
    try {
        aurora::install_curseforge_mod(options);
    } catch (const aurora::Error& error) {
        threw = std::string(error.what()).find("API key is required") !=
                std::string::npos;
    }
    expect(threw, "missing API key is rejected before any request");
    cleanup(game_dir);
}

void test_fingerprint() {
    const auto root = make_temp_dir("fingerprint");
    const auto hello = root / "hello.jar";
    aurora::write_text_file(hello, "hello");
    expect(
        aurora::curseforge_fingerprint(hello) == 0xa631918eu,
        "MurmurHash2 fingerprint matches the reference value");

    // CurseForge ignores ASCII whitespace inside the JAR.
    const auto spaced = root / "spaced.jar";
    aurora::write_text_file(spaced, "a b\nc\td\r");
    expect(
        aurora::curseforge_fingerprint(spaced) == 0xc93f7a16u,
        "whitespace bytes are stripped before fingerprinting");

    aurora::Json file = aurora::Json::object();
    file["id"] = 200;
    file["modId"] = 100;
    file["fileName"] = "hello.jar";
    file["downloadUrl"] = "https://mediafilez.forgecdn.net/files/2/200/hello.jar";
    file["hashes"] = aurora::Json::array(
        {aurora::Json::object({{"algo", 1}, {"value", std::string("aaf4c61ddcc5e8a2dabede0f3b482cd9aea9434d")}})});
    aurora::Json response = aurora::Json::object();
    response["data"] = aurora::Json::object(
        {{"exactMatches", aurora::Json::array(
             {aurora::Json::object({{"id", 0xa631918eu}, {"file", file}})})},
         {"unmatchedFingerprints", aurora::Json::array()}});
    const auto matches = aurora::parse_curseforge_fingerprint_matches(
        response.dump(), {0xa631918eu});
    expect(
        matches.size() == 1 && matches[0].fingerprint == 0xa631918eu &&
            matches[0].file.mod_id == 100 && matches[0].file.id == 200,
        "fingerprint matches include the owning project");
    cleanup(root);
}

void test_update_check() {
    const auto root = make_temp_dir("update");
    aurora::ensure_directory(aurora::mods_directory(root));
    aurora::write_text_file(aurora::mods_directory(root) / "old.jar", "hello");

    const std::string current_file =
        file_json(200, 100, "old.jar", "https://mediafilez.forgecdn.net/files/2/200/old.jar",
                  "2024-01-01T00:00:00Z", 1,
                  "aaf4c61ddcc5e8a2dabede0f3b482cd9aea9434d");
    const std::string latest_file =
        file_json(201, 100, "new.jar", "https://mediafilez.forgecdn.net/files/2/201/new.jar",
                  "2025-01-01T00:00:00Z", 1,
                  "ca527369d9e8c1e081558bd92f90f65c4eb77e21");

    std::size_t post_calls = 0;
    aurora::CurseForgeUpdateOptions options;
    options.game_dir = root;
    options.loader = "fabric";
    options.game_version = "1.21.1";
    options.post = [&](std::string_view url, std::string_view body) {
        ++post_calls;
        expect(
            std::string(url).find("/v1/fingerprints/432") != std::string::npos &&
                body.find("2788266382") != std::string::npos,
            "fingerprint POST carries the hash");
        return "{\"data\":{\"exactMatches\":[{\"id\":2788266382,\"file\":" +
               current_file + "}],\"unmatchedFingerprints\":[]}}";
    };
    options.fetch = [&](std::string_view url) {
        expect(
            std::string(url).find("/v1/mods/100/files") != std::string::npos,
            "update check queries the project files");
        return "{\"data\":[" + latest_file + "]}";
    };
    options.download = [](std::string_view,
                          const std::filesystem::path& destination,
                          std::string_view) {
        aurora::write_text_file(destination, "new content");
    };

    const auto result = aurora::check_curseforge_updates(options);
    expect(
        post_calls == 1 && result.updates == 1 && result.applied == 0 &&
            result.entries.size() == 1 &&
            result.entries[0].status == aurora::CurseForgeUpdateStatus::UpdateAvailable,
        "update check reports the newer CurseForge file");
    expect(
        std::filesystem::is_regular_file(aurora::mods_directory(root) / "old.jar"),
        "checking does not replace files");

    options.apply = true;
    const auto applied = aurora::check_curseforge_updates(options);
    expect(
        applied.applied == 1 &&
            applied.entries[0].installed_file.filename() == "new.jar" &&
            aurora::read_text_file(aurora::mods_directory(root) / "new.jar") ==
                "new content" &&
            !std::filesystem::exists(aurora::mods_directory(root) / "old.jar"),
        "applying replaces the old JAR and keeps the new file name");

    // A local JAR with no fingerprint match is reported, not failed.
    aurora::write_text_file(aurora::mods_directory(root) / "unknown.jar", "other");
    options.post = [](std::string_view, std::string_view) {
        return std::string(
            "{\"data\":{\"exactMatches\":[],\"unmatchedFingerprints\":[]}}");
    };
    options.apply = false;
    const auto unknown = aurora::check_curseforge_updates(options);
    bool saw_unknown = false;
    for (const auto& entry : unknown.entries) {
        if (entry.local_file == "unknown.jar") {
            saw_unknown = entry.status ==
                          aurora::CurseForgeUpdateStatus::NotOnCurseForge;
        }
    }
    expect(saw_unknown, "unmatched local JARs are reported as unknown");
    cleanup(root);
}

void test_update_preserves_disabled_state() {
    const auto root = make_temp_dir("update-disabled");
    aurora::ensure_directory(aurora::mods_directory(root));
    aurora::write_text_file(aurora::mods_directory(root) / "old.jar.disabled",
                            "hello");

    const std::string current_file =
        file_json(200, 100, "old.jar", "https://mediafilez.forgecdn.net/files/2/200/old.jar",
                  "2024-01-01T00:00:00Z", 1,
                  "aaf4c61ddcc5e8a2dabede0f3b482cd9aea9434d");
    const std::string latest_file =
        file_json(201, 100, "new.jar", "https://mediafilez.forgecdn.net/files/2/201/new.jar",
                  "2025-01-01T00:00:00Z", 1,
                  "ca527369d9e8c1e081558bd92f90f65c4eb77e21");

    aurora::CurseForgeUpdateOptions options;
    options.game_dir = root;
    options.loader = "fabric";
    options.game_version = "1.21.1";
    options.apply = true;
    options.post = [&](std::string_view, std::string_view) {
        return "{\"data\":{\"exactMatches\":[{\"id\":2788266382,\"file\":" +
               current_file + "}],\"unmatchedFingerprints\":[]}}";
    };
    options.fetch = [&](std::string_view) {
        return "{\"data\":[" + latest_file + "]}";
    };
    options.download = [](std::string_view,
                          const std::filesystem::path& destination,
                          std::string_view) {
        aurora::write_text_file(destination, "new content");
    };

    const auto result = aurora::check_curseforge_updates(options);
    expect(
        result.applied == 1 &&
            std::filesystem::is_regular_file(aurora::mods_directory(root) /
                                             "new.jar.disabled") &&
            !std::filesystem::exists(aurora::mods_directory(root) / "new.jar") &&
            !std::filesystem::exists(aurora::mods_directory(root) /
                                     "old.jar.disabled"),
        "disabled mods stay disabled after an update");
    cleanup(root);
}

} // namespace

int main() {
    try {
        test_urls_and_loader_types();
        test_search_parsing();
        test_file_parsing_and_selection();
        test_install();
        test_install_rejects_unavailable_file();
        test_api_key_required();
        test_fingerprint();
        test_update_check();
        test_update_preserves_disabled_state();
    } catch (const std::exception& error) {
        std::cerr << "Unexpected exception: " << error.what() << '\n';
        return 1;
    }

    if (failures != 0) {
        std::cerr << failures << " CurseForge test(s) failed\n";
        return 1;
    }
    std::cout << "All CurseForge tests passed\n";
    return 0;
}
