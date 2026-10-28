#include "core/error.hpp"
#include "core/http.hpp"
#include "core/json.hpp"
#include "core/modrinth.hpp"
#include "core/mods.hpp"
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
                      ("aurora-modrinth-tests-" + name + "-" + std::to_string(stamp));
    std::filesystem::create_directories(path);
    return path;
}

void cleanup(const std::filesystem::path& directory) {
    std::error_code error;
    std::filesystem::remove_all(directory, error);
}

constexpr std::string_view kRootSha1 = "ce52b41a4d5f899628853c788ace5c5030d866cd";
constexpr std::string_view kDependencySha1 = "ba3f3448d878a679bd0b1479032f58ccddc758c5";
constexpr std::string_view kBetaSha1 = "15c37711e69ae98056dfba56174e8c160ff6d6c5";

aurora::Json make_file(std::string_view url,
                       std::string_view filename,
                       std::uint64_t size,
                       std::string_view sha1,
                       bool primary = true) {
    aurora::Json hashes = aurora::Json::object();
    hashes["sha1"] = sha1;
    hashes["sha512"] = "";
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
                          std::string_view published,
                          aurora::Json files,
                          aurora::Json dependencies = aurora::Json::array()) {
    aurora::Json version = aurora::Json::object();
    version["id"] = id;
    version["project_id"] = project_id;
    version["name"] = number;
    version["version_number"] = number;
    version["version_type"] = type;
    version["date_published"] = published;
    version["game_versions"] = aurora::Json::array({"1.21.1"});
    version["loaders"] = aurora::Json::array({"fabric"});
    version["files"] = std::move(files);
    version["dependencies"] = std::move(dependencies);
    return version;
}

void test_url_builders() {
    const std::string facets =
        "%5B%5B%22project_type%3Amod%22%5D%2C%5B%22categories%3Afabric%22%5D%2C"
        "%5B%22versions%3A1.21.1%22%5D%5D";
    expect(
        aurora::modrinth_search_url("sodium", "fabric", "1.21.1", 5) ==
            "https://api.modrinth.com/v2/search?query=sodium&limit=5&index=relevance"
            "&facets=" +
                facets,
        "search URL contains query, limit and facets");
    expect(
        aurora::modrinth_search_url("my mod", "", "", 10).find("query=my%20mod") !=
            std::string::npos,
        "search query is percent-encoded");
    expect(
        aurora::modrinth_project_url("sodium") ==
            "https://api.modrinth.com/v2/project/sodium",
        "project URL");
    expect(
        aurora::modrinth_project_versions_url("sodium", "fabric", "1.21.1") ==
            "https://api.modrinth.com/v2/project/sodium/version?"
            "loaders=%5B%22fabric%22%5D&game_versions=%5B%221.21.1%22%5D",
        "project versions URL filters loader and game version");
    expect(
        aurora::modrinth_version_url("SMxNOGZ6") ==
            "https://api.modrinth.com/v2/version/SMxNOGZ6",
        "version URL");

    bool threw = false;
    try {
        aurora::modrinth_project_url("../escape");
    } catch (const aurora::Error&) {
        threw = true;
    }
    expect(threw, "unsafe project reference is rejected");

    threw = false;
    try {
        aurora::modrinth_search_url("x", "", "", 0);
    } catch (const aurora::Error&) {
        threw = true;
    }
    expect(threw, "search limit zero is rejected");

    threw = false;
    try {
        aurora::modrinth_search_url("x", "unknown", "", 10);
    } catch (const aurora::Error&) {
        threw = true;
    }
    expect(threw, "unknown loader is rejected");
}

void test_search_parsing() {
    aurora::Json hits = aurora::Json::array();
    aurora::Json first = aurora::Json::object();
    first["project_id"] = "AANobbMI";
    first["slug"] = "sodium";
    first["title"] = "Sodium";
    first["description"] = "desc";
    first["author"] = "jellysquid3";
    first["downloads"] = 123;
    first["categories"] = aurora::Json::array({"fabric", "optimization"});
    hits.push_back(first);
    aurora::Json second = aurora::Json::object();
    second["project_id"] = "P7dR8mSH";
    second["slug"] = "fabric-api";
    second["title"] = "Fabric API";
    second["author"] = "modmuss50";
    second["downloads"] = 456;
    hits.push_back(second);

    aurora::Json root = aurora::Json::object();
    root["hits"] = hits;
    root["total_hits"] = 42;

    const auto result = aurora::parse_modrinth_search(root.dump(), "fixture");
    expect(result.total_hits == 42, "search total hits");
    expect(result.hits.size() == 2, "search hit count");
    if (result.hits.size() == 2) {
        expect(result.hits[0].project_id == "AANobbMI", "search project id");
        expect(result.hits[0].downloads == 123, "search downloads");
        expect(result.hits[0].categories.size() == 2, "search categories");
        expect(result.hits[1].title == "Fabric API", "search title");
    }

    aurora::Json project = aurora::Json::object();
    project["id"] = "AANobbMI";
    project["slug"] = "sodium";
    project["title"] = "Sodium";
    project["project_type"] = "mod";
    const auto parsed = aurora::parse_modrinth_project(project.dump(), "fixture");
    expect(parsed.id == "AANobbMI" && parsed.slug == "sodium", "project parsing");
}

void test_version_selection() {
    aurora::Json files = aurora::Json::array({make_file(
        "https://cdn.modrinth.com/data/root/old.jar", "old.jar", 18, kRootSha1)});
    aurora::Json dependencies = aurora::Json::array();
    aurora::Json required = aurora::Json::object();
    required["project_id"] = "DEPPROJ";
    required["version_id"] = nullptr;
    required["dependency_type"] = "required";
    dependencies.push_back(required);

    aurora::Json versions = aurora::Json::array();
    versions.push_back(make_version(
        "OLD",
        "ROOTPROJ",
        "1.0",
        "release",
        "2025-01-01T00:00:00Z",
        files,
        dependencies));
    versions.push_back(make_version(
        "NEW",
        "ROOTPROJ",
        "2.0-beta",
        "beta",
        "2025-06-01T00:00:00Z",
        aurora::Json::array({make_file(
            "https://cdn.modrinth.com/data/root/new.jar", "new.jar", 18, kBetaSha1)})));

    const auto parsed = aurora::parse_modrinth_versions(versions.dump(), "fixture");
    expect(parsed.size() == 2, "version parsing");
    expect(parsed[0].dependencies.size() == 1, "dependency parsing");
    expect(parsed[0].dependencies[0].project_id == "DEPPROJ", "dependency project id");
    expect(parsed[0].dependencies[0].version_id.empty(), "null version id becomes empty");

    const auto release = aurora::select_modrinth_version(parsed, "", false);
    expect(release && release->id == "OLD", "release version is preferred");
    const auto newest = aurora::select_modrinth_version(parsed, "", true);
    expect(newest && newest->id == "NEW", "prerelease mode selects the newest version");
    const auto pinned = aurora::select_modrinth_version(parsed, "NEW", false);
    expect(pinned && pinned->id == "NEW", "pinned prerelease is accepted explicitly");
    expect(
        !aurora::select_modrinth_version(parsed, "MISSING", false),
        "missing version id returns nullopt");

    if (release) {
        const auto* file = aurora::select_modrinth_file(*release);
        expect(file != nullptr && file->filename == "old.jar", "primary file selection");
    }

    expect(
        aurora::is_safe_mod_filename("sodium-fabric-0.8.13+mc1.21.1.jar"),
        "normal Modrinth file name is safe");
    expect(aurora::is_safe_mod_filename("a.jar"), "short jar name is safe");
    expect(!aurora::is_safe_mod_filename("../evil.jar"), "traversal name is rejected");
    expect(!aurora::is_safe_mod_filename("mod.zip"), "non-jar name is rejected");
    expect(!aurora::is_safe_mod_filename("mod.jar "), "trailing space is rejected");
    expect(!aurora::is_safe_mod_filename("CON.jar"), "reserved device name is rejected");
    expect(!aurora::is_safe_mod_filename("con.foo.jar"), "reserved stem is rejected");
    expect(!aurora::is_safe_mod_filename("lpt9.jar"), "reserved LPT name is rejected");
    expect(aurora::is_safe_mod_filename("lpt10.jar"), "non-reserved LPT name is allowed");
    expect(!aurora::is_safe_mod_filename(".hidden.jar"), "leading dot is rejected");
}

struct InstallFixture {
    std::map<std::string, std::string> responses;
    std::map<std::string, std::string> contents;
};

InstallFixture make_install_fixture() {
    InstallFixture fixture;

    aurora::Json root_project = aurora::Json::object();
    root_project["id"] = "ROOTPROJ";
    root_project["slug"] = "root-mod";
    root_project["title"] = "Root Mod";
    root_project["project_type"] = "mod";
    fixture.responses[aurora::modrinth_project_url("root-mod")] =
        root_project.dump();

    aurora::Json dependencies = aurora::Json::array();
    aurora::Json required_project = aurora::Json::object();
    required_project["project_id"] = "DEPPROJ";
    required_project["version_id"] = nullptr;
    required_project["dependency_type"] = "required";
    dependencies.push_back(required_project);
    aurora::Json required_version = aurora::Json::object();
    required_version["project_id"] = nullptr;
    required_version["version_id"] = "DEPVER";
    required_version["dependency_type"] = "required";
    dependencies.push_back(required_version);
    aurora::Json optional = aurora::Json::object();
    optional["project_id"] = "OPTPROJ";
    optional["version_id"] = nullptr;
    optional["dependency_type"] = "optional";
    dependencies.push_back(optional);

    aurora::Json root_versions = aurora::Json::array({make_version(
        "ROOTVER",
        "ROOTPROJ",
        "1.0",
        "release",
        "2025-01-01T00:00:00Z",
        aurora::Json::array({make_file(
            "https://cdn.modrinth.com/data/root/root.jar",
            "root.jar",
            18,
            kRootSha1)}),
        dependencies)});
    fixture.responses[aurora::modrinth_project_versions_url(
        "root-mod", "fabric", "1.21.1")] = root_versions.dump();

    aurora::Json dep_project = aurora::Json::object();
    dep_project["id"] = "DEPPROJ";
    dep_project["slug"] = "dep-project";
    dep_project["title"] = "Dependency";
    dep_project["project_type"] = "mod";
    fixture.responses[aurora::modrinth_project_url("DEPPROJ")] =
        dep_project.dump();

    aurora::Json dep_versions = aurora::Json::array({make_version(
        "DEPVER2",
        "DEPPROJ",
        "2.0",
        "release",
        "2025-01-01T00:00:00Z",
        aurora::Json::array({make_file(
            "https://cdn.modrinth.com/data/dep/dep.jar",
            "dep.jar",
            20,
            kDependencySha1)}))});
    fixture.responses[aurora::modrinth_project_versions_url(
        "DEPPROJ", "fabric", "1.21.1")] = dep_versions.dump();

    aurora::Json explicit_project = aurora::Json::object();
    explicit_project["id"] = "EXPLICITPROJ";
    explicit_project["slug"] = "explicit-project";
    explicit_project["title"] = "Explicit Dependency";
    explicit_project["project_type"] = "mod";
    fixture.responses[aurora::modrinth_project_url("EXPLICITPROJ")] =
        explicit_project.dump();

    aurora::Json explicit_version = make_version(
        "DEPVER",
        "EXPLICITPROJ",
        "3.0",
        "release",
        "2025-01-01T00:00:00Z",
        aurora::Json::array({make_file(
            "https://cdn.modrinth.com/data/explicit/explicit.jar",
            "explicit.jar",
            20,
            kDependencySha1)}));
    fixture.responses[aurora::modrinth_version_url("DEPVER")] =
        explicit_version.dump();

    fixture.contents["https://cdn.modrinth.com/data/root/root.jar"] =
        "aurora-root-mod-v1";
    fixture.contents["https://cdn.modrinth.com/data/dep/dep.jar"] =
        "aurora-dependency-v1";
    fixture.contents["https://cdn.modrinth.com/data/explicit/explicit.jar"] =
        "aurora-dependency-v1";
    return fixture;
}

void write_test_content(const std::filesystem::path& path, std::string_view content) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!output) {
        throw aurora::Error("Unable to write test file: " +
                            aurora::path_to_utf8(path));
    }
}

void test_install_with_dependencies() {
    const auto game_dir = make_temp_dir("install");
    InstallFixture fixture = make_install_fixture();

    aurora::ModrinthInstallOptions options;
    options.game_dir = game_dir;
    options.project = "root-mod";
    options.loader = "fabric";
    options.game_version = "1.21.1";
    options.fetch = [&fixture](std::string_view url) {
        const auto it = fixture.responses.find(std::string(url));
        if (it == fixture.responses.end()) {
            throw aurora::Error("Unexpected Modrinth URL: " + std::string(url));
        }
        return it->second;
    };
    options.download = [&fixture](std::string_view url,
                                  const std::filesystem::path& destination,
                                  std::string_view) {
        const auto it = fixture.contents.find(std::string(url));
        if (it == fixture.contents.end()) {
            throw aurora::Error("Unexpected download URL: " + std::string(url));
        }
        write_test_content(destination, it->second);
    };

    const auto result = aurora::install_modrinth_mod(options);
    expect(result.root.project.slug == "root-mod", "root project slug");
    expect(result.root.file.filename == "root.jar", "root file name");
    expect(result.root.downloaded, "root file downloaded");
    expect(result.dependencies.size() == 2, "two required dependencies installed");
    if (result.dependencies.size() == 2) {
        expect(result.dependencies[0].project.slug == "dep-project", "project dependency");
        expect(result.dependencies[1].project.slug == "explicit-project",
               "explicit version dependency");
    }
    expect(
        std::filesystem::is_regular_file(game_dir / "mods" / "root.jar"),
        "root file exists");
    expect(
        std::filesystem::is_regular_file(game_dir / "mods" / "dep.jar"),
        "dependency file exists");
    expect(
        std::filesystem::is_regular_file(game_dir / "mods" / "explicit.jar"),
        "explicit dependency file exists");

    const auto again = aurora::install_modrinth_mod(options);
    expect(!again.root.downloaded, "second install keeps the existing root file");
    expect(
        again.dependencies.size() == 2 &&
            !again.dependencies[0].downloaded &&
            !again.dependencies[1].downloaded,
        "second install keeps existing dependency files");

    aurora::ModrinthInstallOptions no_dependencies = options;
    no_dependencies.install_dependencies = false;
    const auto without_dependencies = aurora::install_modrinth_mod(no_dependencies);
    expect(without_dependencies.dependencies.empty(), "--no-deps skips dependencies");

    cleanup(game_dir);
}

void test_install_rejects_non_mod() {
    const auto game_dir = make_temp_dir("modpack");
    InstallFixture fixture = make_install_fixture();
    aurora::Json modpack_project = aurora::Json::object();
    modpack_project["id"] = "ROOTPROJ";
    modpack_project["slug"] = "root-mod";
    modpack_project["title"] = "Root Mod";
    modpack_project["project_type"] = "modpack";
    fixture.responses[aurora::modrinth_project_url("root-mod")] =
        modpack_project.dump();

    aurora::ModrinthInstallOptions options;
    options.game_dir = game_dir;
    options.project = "root-mod";
    options.loader = "fabric";
    options.game_version = "1.21.1";
    options.fetch = [&fixture](std::string_view url) {
        const auto it = fixture.responses.find(std::string(url));
        if (it == fixture.responses.end()) {
            throw aurora::Error("Unexpected Modrinth URL: " + std::string(url));
        }
        return it->second;
    };

    bool threw = false;
    try {
        aurora::install_modrinth_mod(options);
    } catch (const aurora::Error&) {
        threw = true;
    }
    expect(threw, "non-mod projects are rejected");

    cleanup(game_dir);
}

void test_install_rejects_unsafe_file() {
    const auto game_dir = make_temp_dir("unsafe");
    InstallFixture fixture = make_install_fixture();

    aurora::Json unsafe_version = make_version(
        "ROOTVER",
        "ROOTPROJ",
        "1.0",
        "release",
        "2025-01-01T00:00:00Z",
        aurora::Json::array({make_file(
            "https://cdn.modrinth.com/data/root/evil.jar",
            "../evil.jar",
            18,
            kRootSha1)}));
    fixture.responses[aurora::modrinth_project_versions_url(
        "root-mod", "fabric", "1.21.1")] = aurora::Json::array({unsafe_version}).dump();

    aurora::ModrinthInstallOptions options;
    options.game_dir = game_dir;
    options.project = "root-mod";
    options.loader = "fabric";
    options.game_version = "1.21.1";
    options.install_dependencies = false;
    options.fetch = [&fixture](std::string_view url) {
        const auto it = fixture.responses.find(std::string(url));
        if (it == fixture.responses.end()) {
            throw aurora::Error("Unexpected Modrinth URL: " + std::string(url));
        }
        return it->second;
    };
    options.download = [&fixture](std::string_view url,
                                  const std::filesystem::path& destination,
                                  std::string_view) {
        const auto it = fixture.contents.find(std::string(url));
        if (it == fixture.contents.end()) {
            throw aurora::Error("Unexpected download URL: " + std::string(url));
        }
        write_test_content(destination, it->second);
    };

    bool threw = false;
    try {
        aurora::install_modrinth_mod(options);
    } catch (const aurora::Error&) {
        threw = true;
    }
    expect(threw, "unsafe mod file name is rejected");
    expect(
        !std::filesystem::exists(game_dir / "evil.jar") &&
            !std::filesystem::exists(game_dir / "mods" / "evil.jar"),
        "unsafe mod file is not written");

    cleanup(game_dir);
}

void test_update_check() {
    const auto game_dir = make_temp_dir("updates");
    const auto mods = game_dir / "mods";
    std::filesystem::create_directories(mods);
    write_test_content(mods / "newer.jar", "abc");
    write_test_content(mods / "uptodate.jar", "de");
    write_test_content(mods / "older.jar", "ghi");
    write_test_content(mods / "samerelease.jar", "jkl");
    write_test_content(mods / "unknown.jar", "mno");
    write_test_content(mods / "failed.jar", "stu");
    write_test_content(mods / "disabled.jar.disabled", "pqr");

    constexpr std::string_view kNewerSha1 = "a9993e364706816aba3e25717850c26c9cd0d89d";
    constexpr std::string_view kUptodateSha1 = "600ccd1b71569232d01d110bc63e906beab04d8c";
    constexpr std::string_view kOlderSha1 = "481743d632b80d39bc2771d19be3ca3005b3f8af";
    constexpr std::string_view kSameSha1 = "d798d4338adeb553a1089a58e61e18c2fcdf77bb";
    constexpr std::string_view kUnknownSha1 = "da98568d1b2005611973ad49868b38aa8ae68fd7";
    constexpr std::string_view kDisabledSha1 = "836b9b955a98e0f2e2d678c179696d6ac53356eb";

    const aurora::Json newer_current = make_version(
        "V1", "P1", "1.0", "release", "2025-01-01T00:00:00Z",
        aurora::Json::array(
            {make_file("https://cdn.modrinth.com/data/p1/old.jar", "old.jar", 3, kNewerSha1)}));
    const aurora::Json newer_latest = make_version(
        "V2", "P1", "1.1", "release", "2025-02-01T00:00:00Z",
        aurora::Json::array(
            {make_file("https://cdn.modrinth.com/data/p1/new.jar", "new.jar", 3, kDisabledSha1)}));
    const aurora::Json uptodate = make_version(
        "V3", "P2", "2.0", "release", "2025-03-01T00:00:00Z",
        aurora::Json::array(
            {make_file("https://cdn.modrinth.com/data/p2/mod.jar", "mod.jar", 2, kUptodateSha1)}));
    const aurora::Json older_current = make_version(
        "V4", "P3", "3.0", "release", "2025-05-01T00:00:00Z",
        aurora::Json::array(
            {make_file("https://cdn.modrinth.com/data/p3/mod.jar", "mod.jar", 3, kOlderSha1)}));
    const aurora::Json older_release = make_version(
        "V5", "P3", "2.9", "release", "2025-04-01T00:00:00Z",
        aurora::Json::array(
            {make_file("https://cdn.modrinth.com/data/p3/old.jar", "old.jar", 3, kNewerSha1)}));
    const aurora::Json same_current = make_version(
        "V6", "P4", "4.0", "release", "2025-01-01T00:00:00Z",
        aurora::Json::array(
            {make_file("https://cdn.modrinth.com/data/p4/mod.jar", "mod.jar", 3, kSameSha1)}));
    const aurora::Json same_latest = make_version(
        "V7", "P4", "4.1", "release", "2025-02-01T00:00:00Z",
        aurora::Json::array(
            {make_file("https://cdn.modrinth.com/data/p4/same.jar", "same.jar", 3, kSameSha1)}));
    const aurora::Json disabled_current = make_version(
        "V8", "P5", "5.0", "release", "2025-01-01T00:00:00Z",
        aurora::Json::array({make_file(
            "https://cdn.modrinth.com/data/p5/mod.jar", "mod.jar", 3, kDisabledSha1)}));

    std::map<std::string, std::string> by_hash;
    by_hash[std::string(kNewerSha1)] = newer_current.dump();
    by_hash[std::string(kUptodateSha1)] = uptodate.dump();
    by_hash[std::string(kOlderSha1)] = older_current.dump();
    by_hash[std::string(kSameSha1)] = same_current.dump();
    by_hash[std::string(kDisabledSha1)] = disabled_current.dump();

    std::map<std::string, std::string> by_project;
    by_project[aurora::modrinth_project_versions_url("P1", "fabric", "1.21.1")] =
        aurora::Json::array({newer_latest, newer_current}).dump();
    by_project[aurora::modrinth_project_versions_url("P2", "fabric", "1.21.1")] =
        aurora::Json::array({uptodate}).dump();
    by_project[aurora::modrinth_project_versions_url("P3", "fabric", "1.21.1")] =
        aurora::Json::array({older_current, older_release}).dump();
    by_project[aurora::modrinth_project_versions_url("P4", "fabric", "1.21.1")] =
        aurora::Json::array({same_latest, same_current}).dump();
    by_project[aurora::modrinth_project_versions_url("P5", "fabric", "1.21.1")] =
        aurora::Json::array({disabled_current}).dump();

    const auto fetcher = [&by_hash, &by_project](std::string_view url) -> std::string {
        const std::string value(url);
        constexpr std::string_view marker = "/v2/version_file/";
        const auto marker_pos = value.find(marker);
        if (marker_pos != std::string::npos) {
            const auto hash_start = marker_pos + marker.size();
            const auto hash_end = value.find('?', hash_start);
            const std::string hash = value.substr(hash_start, hash_end - hash_start);
            const auto entry = by_hash.find(hash);
            if (entry != by_hash.end()) {
                return entry->second;
            }
            if (hash == kUnknownSha1) {
                throw aurora::HttpStatusError(404, "HTTP 404 for " + value);
            }
            throw aurora::Error("HTTP 503 for " + value);
        }
        const auto entry = by_project.find(value);
        if (entry != by_project.end()) {
            return entry->second;
        }
        throw aurora::Error("Unexpected Modrinth URL: " + value);
    };

    aurora::ModrinthUpdateOptions options;
    options.game_dir = game_dir;
    options.loader = "fabric";
    options.game_version = "1.21.1";
    options.fetch = fetcher;

    const auto result = aurora::check_modrinth_updates(options);
    expect(result.entries.size() == 7, "every local mod is reported");
    expect(
        result.updates == 1 && result.up_to_date == 4 && result.not_on_modrinth == 1 &&
            result.failures == 1,
        "update check counts");

    const auto find_entry = [&result](std::string_view file)
        -> const aurora::ModrinthUpdateEntry* {
        for (const auto& entry : result.entries) {
            if (entry.local_file == file) {
                return &entry;
            }
        }
        return nullptr;
    };

    const auto* newer = find_entry("newer.jar");
    expect(
        newer && newer->status == aurora::ModrinthUpdateStatus::UpdateAvailable,
        "newer version is reported as an update");
    expect(
        newer && newer->current && newer->current->version_number == "1.0" &&
            newer->latest && newer->latest->version_number == "1.1",
        "update entry carries current and latest versions");
    expect(
        newer && newer->latest_file && newer->latest_file->filename == "new.jar" &&
            newer->latest_file->sha1 == kDisabledSha1,
        "update entry carries the newest file");

    const auto* uptodate_entry = find_entry("uptodate.jar");
    expect(
        uptodate_entry &&
            uptodate_entry->status == aurora::ModrinthUpdateStatus::UpToDate &&
            uptodate_entry->current && uptodate_entry->current->id == "V3",
        "identical newest version is up to date");

    const auto* older = find_entry("older.jar");
    expect(
        older && older->status == aurora::ModrinthUpdateStatus::UpToDate,
        "a release published earlier than the local version is not an update");

    const auto* same = find_entry("samerelease.jar");
    expect(
        same && same->status == aurora::ModrinthUpdateStatus::UpToDate,
        "a newer version with the same JAR hash is up to date");

    const auto* unknown = find_entry("unknown.jar");
    expect(
        unknown && unknown->status == aurora::ModrinthUpdateStatus::NotOnModrinth,
        "unknown hash is reported as not on Modrinth");

    const auto* failed = find_entry("failed.jar");
    expect(
        failed && failed->status == aurora::ModrinthUpdateStatus::LookupFailed &&
            failed->error.find("503") != std::string::npos,
        "lookup failure is reported without aborting the check");

    const auto* disabled = find_entry("disabled.jar.disabled");
    expect(
        disabled && !disabled->local_enabled &&
            disabled->status == aurora::ModrinthUpdateStatus::UpToDate,
        "disabled mods are checked and marked disabled");

    options.include_disabled = false;
    const auto enabled_only = aurora::check_modrinth_updates(options);
    expect(enabled_only.entries.size() == 6, "disabled mods can be excluded");
    bool has_disabled = false;
    for (const auto& entry : enabled_only.entries) {
        if (entry.local_file == "disabled.jar.disabled") {
            has_disabled = true;
        }
    }
    expect(!has_disabled, "--enabled-only skips disabled mods");

    cleanup(game_dir);
}

void test_update_apply() {
    const auto game_dir = make_temp_dir("apply");
    const auto mods = game_dir / "mods";
    std::filesystem::create_directories(mods);
    write_test_content(mods / "mod.jar", "abc");
    write_test_content(mods / "off.jar.disabled", "de");
    write_test_content(mods / "fail.jar", "ghi");

    constexpr std::string_view kModSha1 = "a9993e364706816aba3e25717850c26c9cd0d89d";
    constexpr std::string_view kOffSha1 = "600ccd1b71569232d01d110bc63e906beab04d8c";
    constexpr std::string_view kFailSha1 = "481743d632b80d39bc2771d19be3ca3005b3f8af";
    constexpr std::string_view kNewSha1 = "ca527369d9e8c1e081558bd92f90f65c4eb77e21";
    constexpr std::string_view kOffNewSha1 = "d304daeb2debf52e63cb813996a4e88680aa0894";
    constexpr std::string_view kFailNewSha1 = "d4f8c1a62458fd8a783fe5db264d6dca83377592";

    const auto current = [](std::string_view id, std::string_view project,
                            std::string_view published, std::string_view sha1) {
        return make_version(
            id, project, "1.0", "release", published,
            aurora::Json::array({make_file(
                "https://cdn.modrinth.com/data/" + std::string(project) + "/old.jar",
                "old.jar", 3, sha1)}));
    };
    const auto latest = [](std::string_view id, std::string_view project,
                           std::string_view published, std::string_view filename,
                           std::string_view sha1) {
        return make_version(
            id, project, "2.0", "release", published,
            aurora::Json::array({make_file(
                "https://cdn.modrinth.com/data/" + std::string(project) + "/" +
                    std::string(filename),
                filename, 3, sha1)}));
    };

    const auto mod_current = current("A1", "AP1", "2025-01-01T00:00:00Z", kModSha1);
    const auto mod_latest =
        latest("A2", "AP1", "2025-02-01T00:00:00Z", "new.jar", kNewSha1);
    const auto off_current = current("B1", "AP2", "2025-01-01T00:00:00Z", kOffSha1);
    const auto off_latest =
        latest("B2", "AP2", "2025-02-01T00:00:00Z", "off-new.jar", kOffNewSha1);
    const auto fail_current = current("C1", "AP3", "2025-01-01T00:00:00Z", kFailSha1);
    const auto fail_latest =
        latest("C2", "AP3", "2025-02-01T00:00:00Z", "fail-new.jar", kFailNewSha1);

    std::map<std::string, std::string> by_hash;
    by_hash[std::string(kModSha1)] = mod_current.dump();
    by_hash[std::string(kOffSha1)] = off_current.dump();
    by_hash[std::string(kFailSha1)] = fail_current.dump();

    std::map<std::string, std::string> by_project;
    by_project[aurora::modrinth_project_versions_url("AP1", "fabric", "1.21.1")] =
        aurora::Json::array({mod_latest, mod_current}).dump();
    by_project[aurora::modrinth_project_versions_url("AP2", "fabric", "1.21.1")] =
        aurora::Json::array({off_latest, off_current}).dump();
    by_project[aurora::modrinth_project_versions_url("AP3", "fabric", "1.21.1")] =
        aurora::Json::array({fail_latest, fail_current}).dump();

    const auto fetcher = [&by_hash, &by_project](std::string_view url) -> std::string {
        const std::string value(url);
        constexpr std::string_view marker = "/v2/version_file/";
        const auto marker_pos = value.find(marker);
        if (marker_pos != std::string::npos) {
            const auto hash_start = marker_pos + marker.size();
            const auto hash_end = value.find('?', hash_start);
            const auto entry = by_hash.find(
                value.substr(hash_start, hash_end - hash_start));
            if (entry != by_hash.end()) {
                return entry->second;
            }
            throw aurora::HttpStatusError(404, "HTTP 404 for " + value);
        }
        const auto entry = by_project.find(value);
        if (entry != by_project.end()) {
            return entry->second;
        }
        throw aurora::Error("Unexpected Modrinth URL: " + value);
    };

    std::map<std::string, std::string> downloads;
    downloads["https://cdn.modrinth.com/data/AP1/new.jar"] = "new content";
    downloads["https://cdn.modrinth.com/data/AP2/off-new.jar"] = "replacement jar";
    std::vector<std::pair<std::string, std::string>> download_calls;

    aurora::ModrinthUpdateOptions options;
    options.game_dir = game_dir;
    options.loader = "fabric";
    options.game_version = "1.21.1";
    options.apply = true;
    options.fetch = fetcher;
    options.download = [&downloads, &download_calls](
                           std::string_view url,
                           const std::filesystem::path& destination,
                           std::string_view sha1) {
        const std::string key(url);
        const auto entry = downloads.find(key);
        if (entry == downloads.end()) {
            throw aurora::Error("download exploded");
        }
        download_calls.emplace_back(key, std::string(sha1));
        write_test_content(destination, entry->second);
    };

    const auto result = aurora::check_modrinth_updates(options);
    expect(result.updates == 3 && result.applied == 2, "apply installs available updates");
    expect(
        std::filesystem::is_regular_file(mods / "new.jar") &&
            !std::filesystem::exists(mods / "mod.jar"),
        "enabled update replaces the old JAR");
    expect(
        std::filesystem::is_regular_file(mods / "off-new.jar.disabled") &&
            !std::filesystem::exists(mods / "off.jar.disabled"),
        "disabled update keeps the disabled state");
    expect(
        std::filesystem::is_regular_file(mods / "fail.jar") &&
            !std::filesystem::exists(mods / "fail-new.jar"),
        "failed update leaves the old JAR in place");
    expect(download_calls.size() == 2, "only two files are downloaded");

    const auto read_file = [](const std::filesystem::path& path) {
        return aurora::read_text_file(path);
    };
    expect(
        read_file(mods / "new.jar") == "new content",
        "replacement content is written");
    expect(
        read_file(mods / "off-new.jar.disabled") == "replacement jar",
        "disabled replacement content is written");

    const auto find_entry = [&result](std::string_view file)
        -> const aurora::ModrinthUpdateEntry* {
        for (const auto& entry : result.entries) {
            if (entry.local_file == file) {
                return &entry;
            }
        }
        return nullptr;
    };
    const auto* failed = find_entry("fail.jar");
    expect(
        failed && !failed->applied &&
            failed->error.find("download exploded") != std::string::npos,
        "download failure is reported per entry");
    const auto* applied = find_entry("mod.jar");
    expect(
        applied && applied->applied && applied->installed_file == mods / "new.jar",
        "applied entry records the installed file");

    cleanup(game_dir);
}

void test_version_file_url() {
    expect(
        aurora::modrinth_version_file_url(
            "A9993E364706816ABA3E25717850C26C9CD0D89D") ==
            "https://api.modrinth.com/v2/version_file/"
            "a9993e364706816aba3e25717850c26c9cd0d89d?algorithm=sha1",
        "version_file URL normalizes the hash");
    bool threw = false;
    try {
        aurora::modrinth_version_file_url("not-a-hash");
    } catch (const aurora::Error&) {
        threw = true;
    }
    expect(threw, "version_file URL rejects invalid hashes");
}

} // namespace

int main() {
    test_url_builders();
    test_search_parsing();
    test_version_selection();
    test_install_with_dependencies();
    test_install_rejects_non_mod();
    test_install_rejects_unsafe_file();
    test_update_check();
    test_update_apply();
    test_version_file_url();

    if (failures != 0) {
        std::cerr << failures << " modrinth test(s) failed\n";
        return 1;
    }
    std::cout << "All Modrinth tests passed\n";
    return 0;
}
