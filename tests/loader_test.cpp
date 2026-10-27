#include "core/error.hpp"
#include "core/json.hpp"
#include "core/loader.hpp"
#include "core/paths.hpp"
#include "core/repository.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
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
    static unsigned int counter = 0;
    const auto path = std::filesystem::temp_directory_path() /
                      ("aurora-loader-tests-" + name + "-" + std::to_string(stamp) + "-" +
                       std::to_string(++counter));
    std::filesystem::create_directories(path);
    return path;
}

template <typename Fn>
std::string capture_error(Fn&& fn) {
    try {
        fn();
    } catch (const aurora::Error& e) {
        return e.what();
    } catch (const std::exception& e) {
        return std::string("non-Aurora exception: ") + e.what();
    }
    return {};
}

template <typename Fn>
void expect_error_contains(Fn&& fn,
                           const std::string& needle,
                           const std::string& message) {
    const std::string error = capture_error(std::forward<Fn>(fn));
    expect(
        error.find(needle) != std::string::npos,
        message + " (error was: " + (error.empty() ? "none" : error) + ")");
}

std::string fabric_loader_list() {
    return R"([
  {
    "loader": {
      "separator": ".",
      "build": 10,
      "maven": "net.fabricmc:fabric-loader:0.16.10",
      "version": "0.16.10",
      "stable": true
    }
  },
  {
    "loader": {
      "separator": ".",
      "build": 1,
      "maven": "net.fabricmc:fabric-loader:0.19.0-beta.1",
      "version": "0.19.0-beta.1",
      "stable": false
    }
  },
  {
    "loader": {
      "separator": ".",
      "build": 11,
      "maven": "net.fabricmc:fabric-loader:0.18.0",
      "version": "0.18.0",
      "stable": true
    }
  }
])";
}

std::string quilt_loader_list() {
    return R"([
  {
    "loader": {
      "maven": "org.quiltmc:quilt-loader:0.24.0",
      "version": "0.24.0",
      "build": 0,
      "separator": "."
    }
  },
  {
    "loader": {
      "maven": "org.quiltmc:quilt-loader:0.25.0-beta.3",
      "version": "0.25.0-beta.3",
      "build": 3,
      "separator": "."
    }
  },
  {
    "loader": {
      "maven": "org.quiltmc:quilt-loader:0.23.0",
      "version": "0.23.0",
      "build": 0,
      "separator": "."
    }
  }
])";
}

std::string fabric_profile(
    const std::string& id,
    const std::string& main_class = "net.fabricmc.loader.impl.launch.knot.KnotClient") {
    std::string profile = "{\"id\":\"";
    profile += id;
    profile += "\",\"inheritsFrom\":\"1.21.1\",\"type\":\"release\",\"mainClass\":\"";
    profile += main_class;
    profile +=
        "\",\"arguments\":{\"game\":[],\"jvm\":[\"-DFabricMcEmu= net.minecraft.client.main.Main \"]},"
        "\"libraries\":["
        "{\"name\":\"net.fabricmc:sponge-mixin:0.15.4+mixin.0.8.7\",\"url\":\"https://maven.fabricmc.net/\"},"
        "{\"name\":\"net.fabricmc:fabric-loader:0.16.10\",\"url\":\"https://maven.fabricmc.net/\"}],"
        "\"custom\":{\"keep\":true}}";
    return profile;
}

std::string quilt_profile(const std::string& id) {
    std::string profile = "{\"id\":\"";
    profile += id;
    profile +=
        "\",\"inheritsFrom\":\"1.21.1\",\"type\":\"release\","
        "\"mainClass\":\"org.quiltmc.loader.impl.launch.knot.KnotClient\","
        "\"libraries\":[{\"name\":\"org.quiltmc:quilt-loader:0.24.0\","
        "\"url\":\"https://maven.quiltmc.org/repository/release/\"}]}";
    return profile;
}

std::filesystem::path version_json_path(const std::filesystem::path& game_dir,
                                        const std::string& id) {
    return aurora::absolute_path(game_dir) / "versions" / id / (id + ".json");
}

void test_game_versions() {
    const auto versions = aurora::parse_game_versions(R"([
  { "version": "1.21.1", "stable": true },
  { "version": "1.21.2-pre1", "stable": false },
  { "version": "1.21.2" }
])",
                                                      "game fixture");
    expect(versions.size() == 3, "game version count");
    expect(versions[0].id == "1.21.1" && versions[0].stable, "stable game version");
    expect(versions[1].id == "1.21.2-pre1" && !versions[1].stable, "unstable game version");
    expect(versions[2].stable, "unflagged game version without suffix is stable");

    expect_error_contains(
        [] { aurora::parse_game_versions("{}", "game fixture"); },
        "expected a JSON array",
        "game list must be an array");
    expect_error_contains(
        [] { aurora::parse_game_versions(R"([{"stable":true}])", "game fixture"); },
        "\"version\"",
        "game entry needs version");
    expect_error_contains(
        [] { aurora::parse_game_versions("not-json", "game fixture"); },
        "Invalid JSON",
        "malformed game JSON");
}

void test_loader_selection() {
    const auto fabric = aurora::parse_loader_versions(
        aurora::LoaderKind::Fabric, fabric_loader_list(), "1.21.1", "fabric fixture");
    expect(fabric.size() == 3, "fabric loader count");
    expect(fabric[0].game_version == "1.21.1", "fabric game version field");
    expect(
        fabric[0].maven == "net.fabricmc:fabric-loader:0.16.10",
        "fabric maven field");
    expect(fabric[0].separator == ".", "fabric separator field");

    const auto stable =
        aurora::select_loader_version(fabric, aurora::LoaderChannel::Stable);
    expect(stable && stable->version == "0.18.0", "fabric stable selection");
    const auto latest =
        aurora::select_loader_version(fabric, aurora::LoaderChannel::Latest);
    expect(latest && latest->version == "0.19.0-beta.1", "fabric latest selection");

    const auto quilt = aurora::parse_loader_versions(
        aurora::LoaderKind::Quilt, quilt_loader_list(), "1.21.1", "quilt fixture");
    expect(quilt.size() == 3, "quilt loader count");
    expect(quilt[0].stable, "unflagged quilt release is stable");
    expect(!quilt[1].stable, "unflagged quilt beta is unstable");
    const auto quilt_stable =
        aurora::select_loader_version(quilt, aurora::LoaderChannel::Stable);
    expect(quilt_stable && quilt_stable->version == "0.24.0", "quilt stable selection");
    const auto quilt_latest =
        aurora::select_loader_version(quilt, aurora::LoaderChannel::Latest);
    expect(
        quilt_latest && quilt_latest->version == "0.25.0-beta.3",
        "quilt latest selection");

    const auto flat = aurora::parse_loader_versions(
        aurora::LoaderKind::Fabric,
        R"([{"version":"1.0.0","stable":true}])",
        "1.21.1",
        "flat fixture");
    expect(flat.size() == 1 && flat[0].version == "1.0.0", "flat loader entry fallback");

    expect(
        !aurora::select_loader_version({}, aurora::LoaderChannel::Stable),
        "empty loader list has no selection");
    expect(
        aurora::compare_loader_versions("0.16.10", "0.16.9") > 0,
        "numeric version compare");
    expect(
        aurora::compare_loader_versions("0.17.0-beta.1", "0.17.0") < 0,
        "prerelease sorts before release");
    expect(
        aurora::compare_loader_versions("0.17.0-beta.2", "0.17.0-beta.1") > 0,
        "prerelease numeric compare");
    expect(
        aurora::compare_loader_versions("1.0", "1.0.0") == 0,
        "missing core component treated as zero");
    expect_error_contains(
        [&fabric] {
            aurora::select_loader_version(
                fabric, static_cast<aurora::LoaderChannel>(99));
        },
        "Unsupported loader channel",
        "invalid loader channel rejected");

    expect_error_contains(
        [] {
            aurora::parse_loader_versions(
                aurora::LoaderKind::Fabric, "{}", "1.21.1", "loader fixture");
        },
        "expected a JSON array",
        "loader list must be an array");
    expect_error_contains(
        [] {
            aurora::parse_loader_versions(
                aurora::LoaderKind::Fabric,
                R"([{"loader":{}}])",
                "1.21.1",
                "loader fixture");
        },
        "has no loader version",
        "loader entry needs version");
    expect_error_contains(
        [] {
            aurora::parse_loader_versions(
                aurora::LoaderKind::Fabric,
                R"([{"loader":{"version":"../1.0"}}])",
                "1.21.1",
                "loader fixture");
        },
        "unsafe loader version",
        "loader entry rejects unsafe version");
}

void test_id_generation() {
    expect(
        aurora::make_loader_version_id(
            aurora::LoaderKind::Fabric, "0.16.10", "1.21.1") ==
            "fabric-loader-0.16.10-1.21.1",
        "fabric id");
    expect(
        aurora::make_loader_version_id(
            aurora::LoaderKind::Quilt, "0.24.0", "1.21.1") ==
            "quilt-loader-0.24.0-1.21.1",
        "quilt id");
    expect(
        aurora::make_loader_version_id(
            aurora::LoaderKind::Fabric, "0.16.10", "1.21.1") ==
            aurora::make_loader_version_id(
                aurora::LoaderKind::Fabric, "0.16.10", "1.21.1"),
        "id generation is deterministic");
    expect(
        aurora::make_loader_version_id(
            aurora::LoaderKind::Fabric, "0.16.10+build.2", "1.21.1") ==
            "fabric-loader-0.16.10+build.2-1.21.1",
        "id allows build metadata");

    expect_error_contains(
        [] {
            aurora::make_loader_version_id(
                aurora::LoaderKind::Fabric, "", "1.21.1");
        },
        "loader version is empty",
        "empty loader version rejected");
    expect_error_contains(
        [] {
            aurora::make_loader_version_id(
                aurora::LoaderKind::Fabric, "0.16.10", "");
        },
        "Minecraft version is empty",
        "empty game version rejected");
    expect_error_contains(
        [] {
            aurora::make_loader_version_id(
                aurora::LoaderKind::Fabric, "0.16.10", "../1.21.1");
        },
        "Unsafe Minecraft version",
        "game traversal rejected");
    expect_error_contains(
        [] {
            aurora::make_loader_version_id(
                aurora::LoaderKind::Fabric, "0.16/10", "1.21.1");
        },
        "Unsafe loader version",
        "loader slash rejected");
    expect_error_contains(
        [] {
            aurora::make_loader_version_id(
                aurora::LoaderKind::Fabric, "0.16 10", "1.21.1");
        },
        "Unsafe loader version",
        "loader space rejected");
    expect_error_contains(
        [] {
            aurora::make_loader_version_id(
                aurora::LoaderKind::Quilt, "0.24.0", ".");
        },
        "Unsafe Minecraft version",
        "dot game version rejected");
    expect_error_contains(
        [] {
            aurora::make_loader_version_id(
                static_cast<aurora::LoaderKind>(99), "0.24.0", "1.21.1");
        },
        "Unsupported loader kind",
        "invalid loader kind rejected");
}

void test_fetch_functions() {
    aurora::LoaderEndpoints endpoints;
    endpoints.fabric_meta = "https://fixtures.invalid/fabric/";
    endpoints.quilt_meta = "https://fixtures.invalid/quilt/";

    {
        const std::string url = "https://fixtures.invalid/fabric/v2/versions/game";
        int calls = 0;
        const aurora::LoaderFetcher fetch = [&url, &calls](
                                                std::string_view requested) -> std::string {
            ++calls;
            expect(requested == url, "fabric game versions fetch URL");
            return R"([{"version":"1.21.1","stable":true}])";
        };
        const auto versions = aurora::fetch_fabric_game_versions(endpoints, fetch);
        expect(calls == 1, "fabric game versions fetched once");
        expect(versions.size() == 1 && versions[0].id == "1.21.1",
               "fabric game versions parsed");
    }

    {
        const std::string url =
            "https://fixtures.invalid/quilt/v3/versions/loader/1.21.1";
        int calls = 0;
        const aurora::LoaderFetcher fetch = [&url, &calls](
                                                std::string_view requested) -> std::string {
            ++calls;
            expect(requested == url, "quilt loader versions fetch URL");
            return quilt_loader_list();
        };
        const auto versions = aurora::fetch_loader_versions(
            aurora::LoaderKind::Quilt, "1.21.1", endpoints, fetch);
        expect(calls == 1, "quilt loader versions fetched once");
        const auto stable =
            aurora::select_loader_version(versions, aurora::LoaderChannel::Stable);
        expect(stable && stable->version == "0.24.0",
               "quilt fetched versions can be selected");
    }
}

void test_endpoint_urls() {
    const aurora::LoaderEndpoints defaults;
    expect(
        defaults.fabric_meta == "https://meta.fabricmc.net",
        "default Fabric meta endpoint");
    expect(
        defaults.quilt_meta == "https://meta.quiltmc.org",
        "default Quilt meta endpoint");

    aurora::LoaderEndpoints endpoints;
    endpoints.fabric_meta = "https://fixtures.invalid/fabric/";
    endpoints.quilt_meta = "http://127.0.0.1:9/quilt";

    expect(
        aurora::fabric_game_versions_url(endpoints) ==
            "https://fixtures.invalid/fabric/v2/versions/game",
        "fabric game URL");
    expect(
        aurora::loader_versions_url(
            aurora::LoaderKind::Fabric, "1.21.1", endpoints) ==
            "https://fixtures.invalid/fabric/v2/versions/loader/1.21.1",
        "fabric loader list URL");
    expect(
        aurora::loader_profile_url(
            aurora::LoaderKind::Fabric, "1.21.1", "0.16.10", endpoints) ==
            "https://fixtures.invalid/fabric/v2/versions/loader/1.21.1/0.16.10/profile/json",
        "fabric profile URL");
    expect(
        aurora::loader_versions_url(
            aurora::LoaderKind::Quilt, "1.21.1", endpoints) ==
            "http://127.0.0.1:9/quilt/v3/versions/loader/1.21.1",
        "quilt loader list URL");
    expect(
        aurora::loader_profile_url(
            aurora::LoaderKind::Quilt, "1.21.1", "0.24.0", endpoints) ==
            "http://127.0.0.1:9/quilt/v3/versions/loader/1.21.1/0.24.0/profile/json",
        "quilt profile URL");

    expect_error_contains(
        [&endpoints] {
            aurora::loader_versions_url(
                aurora::LoaderKind::Fabric, "../1.21.1", endpoints);
        },
        "Unsafe Minecraft version",
        "URL builder rejects traversal");
    expect_error_contains(
        [&endpoints] {
            aurora::loader_profile_url(
                aurora::LoaderKind::Quilt, "1.21.1", "0.24/0", endpoints);
        },
        "Unsafe loader version",
        "profile URL rejects slash");
}

void test_profile_rewrite() {
    const std::string generated = "fabric-loader-0.16.10-1.21.1";
    const auto profile = aurora::parse_loader_profile(
        aurora::LoaderKind::Fabric,
        fabric_profile("fabric-loader-0.16.9-1.21.1"),
        generated,
        "profile fixture");
    expect(profile["id"] == generated, "profile id rewritten");
    expect(profile["inheritsFrom"] == "1.21.1", "profile inheritsFrom kept");
    expect(
        profile["mainClass"] == "net.fabricmc.loader.impl.launch.knot.KnotClient",
        "profile mainClass kept");
    expect(profile["libraries"].size() == 2, "profile libraries kept");
    expect(profile["custom"]["keep"] == true, "profile unknown fields kept");

    std::string different_parent = fabric_profile("fabric-loader-0.16.9-1.21.1");
    const std::string from = "\"inheritsFrom\":\"1.21.1\"";
    const auto position = different_parent.find(from);
    expect(position != std::string::npos, "fixture contains inheritsFrom");
    if (position != std::string::npos) {
        different_parent.replace(
            position, from.size(), "\"inheritsFrom\":\"1.20.6\"");
    }
    const auto rewritten = aurora::parse_loader_profile(
        aurora::LoaderKind::Fabric,
        different_parent,
        generated,
        "profile fixture");
    expect(
        rewritten["inheritsFrom"] == "1.20.6",
        "profile inheritsFrom is not normalized");

    expect_error_contains(
        [] {
            aurora::parse_loader_profile(
                aurora::LoaderKind::Fabric,
                R"({"inheritsFrom":"1.21.1","mainClass":"x","libraries":[]})",
                "fabric-loader-0.16.10-1.21.1",
                "profile fixture");
        },
        "\"id\"",
        "profile requires id");
    expect_error_contains(
        [] {
            aurora::parse_loader_profile(
                aurora::LoaderKind::Fabric,
                R"({"id":"","inheritsFrom":"1.21.1","mainClass":"x","libraries":[]})",
                "fabric-loader-0.16.10-1.21.1",
                "profile fixture");
        },
        "\"id\"",
        "profile rejects empty id");
    expect_error_contains(
        [] {
            aurora::parse_loader_profile(
                aurora::LoaderKind::Fabric,
                R"({"id":"x","mainClass":"x","libraries":[]})",
                "fabric-loader-0.16.10-1.21.1",
                "profile fixture");
        },
        "\"inheritsFrom\"",
        "profile requires inheritsFrom");
    expect_error_contains(
        [] {
            aurora::parse_loader_profile(
                aurora::LoaderKind::Fabric,
                R"({"id":"x","inheritsFrom":"1.21.1","libraries":[]})",
                "fabric-loader-0.16.10-1.21.1",
                "profile fixture");
        },
        "\"mainClass\"",
        "profile requires mainClass");
    expect_error_contains(
        [] {
            aurora::parse_loader_profile(
                aurora::LoaderKind::Fabric,
                R"({"id":"x","inheritsFrom":"1.21.1","mainClass":"x"})",
                "fabric-loader-0.16.10-1.21.1",
                "profile fixture");
        },
        "libraries",
        "profile requires libraries");
    expect_error_contains(
        [] {
            aurora::parse_loader_profile(
                aurora::LoaderKind::Fabric,
                R"({"id":"x","inheritsFrom":"1.21.1","mainClass":"x","libraries":{}})",
                "fabric-loader-0.16.10-1.21.1",
                "profile fixture");
        },
        "libraries",
        "profile rejects non-array libraries");
    expect_error_contains(
        [] {
            aurora::parse_loader_profile(
                aurora::LoaderKind::Fabric,
                R"({"id":"x","inheritsFrom":"1.21.1","mainClass":"x","libraries":[1]})",
                "fabric-loader-0.16.10-1.21.1",
                "profile fixture");
        },
        "not an object",
        "profile rejects non-object library");
    expect_error_contains(
        [] {
            aurora::parse_loader_profile(
                aurora::LoaderKind::Fabric,
                R"({"id":"x","inheritsFrom":"1.21.1","mainClass":"x","libraries":[{}]})",
                "fabric-loader-0.16.10-1.21.1",
                "profile fixture");
        },
        "\"name\"",
        "profile rejects library without name");
    expect_error_contains(
        [] {
            aurora::parse_loader_profile(
                aurora::LoaderKind::Fabric,
                "[]",
                "fabric-loader-0.16.10-1.21.1",
                "profile fixture");
        },
        "expected a JSON object",
        "profile must be an object");
    expect_error_contains(
        [] {
            aurora::parse_loader_profile(
                aurora::LoaderKind::Fabric,
                "not-json",
                "fabric-loader-0.16.10-1.21.1",
                "profile fixture");
        },
        "Invalid JSON",
        "malformed profile JSON");
    expect_error_contains(
        [] {
            aurora::parse_loader_profile(
                aurora::LoaderKind::Fabric,
                fabric_profile("fabric-loader-0.16.9-1.21.1"),
                "../escape",
                "profile fixture");
        },
        "Unsafe generated loader version id",
        "unsafe generated id rejected");
}

void test_install_writes_profile() {
    const auto game_dir = make_temp_dir("fabric-install");
    const std::string id = "fabric-loader-0.16.10-1.21.1";
    const std::string profile_url =
        "https://fixtures.invalid/fabric/v2/versions/loader/1.21.1/0.16.10/profile/json";

    std::vector<std::string> urls;
    aurora::LoaderInstallOptions options;
    options.kind = aurora::LoaderKind::Fabric;
    options.game_version = "1.21.1";
    options.loader_version = "0.16.10";
    options.endpoints.fabric_meta = "https://fixtures.invalid/fabric";
    options.fetch = [&urls, &profile_url](std::string_view url) -> std::string {
        urls.emplace_back(url);
        if (url == profile_url) {
            return fabric_profile("fabric-loader-0.16.9-1.21.1");
        }
        throw std::runtime_error("unexpected URL: " + std::string(url));
    };

    const auto result = aurora::install_loader(game_dir, options);
    expect(result.kind == aurora::LoaderKind::Fabric, "fabric result kind");
    expect(result.id == id, "fabric result id");
    expect(result.game_version == "1.21.1", "fabric result game version");
    expect(result.loader_version == "0.16.10", "fabric result loader version");
    expect(result.written, "fabric result written");
    expect(result.json_path == version_json_path(game_dir, id), "fabric result path");
    expect(urls.size() == 1, "fabric single profile fetch");
    if (!urls.empty()) {
        expect(urls.front() == profile_url, "fabric profile URL requested");
    }
    expect(
        std::filesystem::is_regular_file(result.json_path),
        "fabric profile file exists");

    const auto written = aurora::read_json_file(result.json_path);
    expect(written["id"] == id, "written profile id normalized");
    expect(written["inheritsFrom"] == "1.21.1", "written inheritsFrom kept");
    expect(
        written["mainClass"] == "net.fabricmc.loader.impl.launch.knot.KnotClient",
        "written mainClass");
    expect(written["libraries"].size() == 2, "written libraries");
    expect(written["custom"]["keep"] == true, "written unknown fields");
    expect(
        aurora::VersionRepository(game_dir).contains(id),
        "repository sees installed loader version");
}

void test_quilt_install() {
    const auto game_dir = make_temp_dir("quilt-install");
    const std::string id = "quilt-loader-0.24.0-1.21.1";
    const std::string profile_url =
        "https://fixtures.invalid/quilt/v3/versions/loader/1.21.1/0.24.0/profile/json";

    std::vector<std::string> urls;
    aurora::LoaderInstallOptions options;
    options.kind = aurora::LoaderKind::Quilt;
    options.game_version = "1.21.1";
    options.loader_version = "0.24.0";
    options.endpoints.quilt_meta = "https://fixtures.invalid/quilt/";
    options.fetch = [&urls, &profile_url](std::string_view url) -> std::string {
        urls.emplace_back(url);
        if (url == profile_url) {
            return quilt_profile("quilt-loader-0.23.0-1.21.1");
        }
        throw std::runtime_error("unexpected URL: " + std::string(url));
    };

    const auto result = aurora::install_loader(game_dir, options);
    expect(result.kind == aurora::LoaderKind::Quilt, "quilt result kind");
    expect(result.id == id, "quilt result id");
    expect(result.loader_version == "0.24.0", "quilt result loader version");
    expect(result.written, "quilt result written");
    expect(urls.size() == 1, "quilt single profile fetch");
    if (!urls.empty()) {
        expect(urls.front() == profile_url, "quilt profile URL requested");
    }

    const auto written = aurora::read_json_file(result.json_path);
    expect(written["id"] == id, "quilt profile id normalized");
    expect(written["inheritsFrom"] == "1.21.1", "quilt inheritsFrom kept");
    expect(
        aurora::VersionRepository(game_dir).contains(id),
        "repository sees installed quilt version");
}

void test_no_overwrite_and_force() {
    const auto game_dir = make_temp_dir("force");
    const std::string id = "fabric-loader-0.16.10-1.21.1";
    const auto path = version_json_path(game_dir, id);

    int calls = 0;
    std::string next_profile = fabric_profile(id);
    aurora::LoaderInstallOptions options;
    options.game_version = "1.21.1";
    options.loader_version = "0.16.10";
    options.endpoints.fabric_meta = "https://fixtures.invalid/fabric";
    options.fetch = [&calls, &next_profile](std::string_view) -> std::string {
        ++calls;
        return next_profile;
    };

    const auto first = aurora::install_loader(game_dir, options);
    expect(first.written, "first install writes");
    expect(calls == 1, "first install fetches once");

    aurora::write_text_file(path, R"({"sentinel":true})");
    next_profile = fabric_profile(id, "com.example.Updated");
    const auto second = aurora::install_loader(game_dir, options);
    expect(!second.written, "second install keeps existing file");
    expect(calls == 1, "second install does not fetch");
    const auto kept = aurora::read_json_file(path);
    expect(kept["sentinel"] == true, "existing file preserved");

    options.force = true;
    next_profile = "not-json";
    expect_error_contains(
        [&game_dir, &options] { aurora::install_loader(game_dir, options); },
        "Invalid JSON",
        "forced invalid profile fails");
    expect(calls == 2, "force fetches invalid profile");
    const auto still_kept = aurora::read_json_file(path);
    expect(still_kept["sentinel"] == true, "failed force keeps existing file");

    next_profile = fabric_profile(id, "com.example.Updated");
    const auto forced = aurora::install_loader(game_dir, options);
    expect(forced.written, "forced install writes");
    expect(calls == 3, "forced install fetches");
    const auto updated = aurora::read_json_file(path);
    expect(
        updated["mainClass"] == "com.example.Updated",
        "forced install replaces profile");
    expect(updated["id"] == id, "forced install keeps generated id");
}

void test_install_selection() {
    const std::string list_url =
        "https://fixtures.invalid/fabric/v2/versions/loader/1.21.1";
    const std::string stable_profile_url =
        "https://fixtures.invalid/fabric/v2/versions/loader/1.21.1/0.18.0/profile/json";
    const std::string latest_profile_url =
        "https://fixtures.invalid/fabric/v2/versions/loader/1.21.1/0.19.0-beta.1/profile/json";

    {
        const auto game_dir = make_temp_dir("stable-select");
        std::vector<std::string> urls;
        aurora::LoaderInstallOptions options;
        options.game_version = "1.21.1";
        options.endpoints.fabric_meta = "https://fixtures.invalid/fabric";
        options.fetch = [&urls, &list_url, &stable_profile_url](
                            std::string_view url) -> std::string {
            urls.emplace_back(url);
            if (url == list_url) {
                return fabric_loader_list();
            }
            if (url == stable_profile_url) {
                return fabric_profile("fabric-loader-0.18.0-1.21.1");
            }
            throw std::runtime_error("unexpected URL: " + std::string(url));
        };

        const auto result = aurora::install_loader(game_dir, options);
        expect(result.loader_version == "0.18.0", "install selects stable loader");
        expect(
            result.id == "fabric-loader-0.18.0-1.21.1",
            "stable install id");
        expect(urls.size() == 2, "stable install fetches list and profile");
        if (urls.size() == 2) {
            expect(urls[0] == list_url, "stable list URL");
            expect(urls[1] == stable_profile_url, "stable profile URL");
        }
    }

    {
        const auto game_dir = make_temp_dir("latest-select");
        std::vector<std::string> urls;
        aurora::LoaderInstallOptions options;
        options.game_version = "1.21.1";
        options.channel = aurora::LoaderChannel::Latest;
        options.endpoints.fabric_meta = "https://fixtures.invalid/fabric";
        options.fetch = [&urls, &list_url, &latest_profile_url](
                            std::string_view url) -> std::string {
            urls.emplace_back(url);
            if (url == list_url) {
                return fabric_loader_list();
            }
            if (url == latest_profile_url) {
                return fabric_profile("fabric-loader-0.19.0-beta.1-1.21.1");
            }
            throw std::runtime_error("unexpected URL: " + std::string(url));
        };

        const auto result = aurora::install_loader(game_dir, options);
        expect(
            result.loader_version == "0.19.0-beta.1",
            "install selects latest loader");
        expect(
            result.id == "fabric-loader-0.19.0-beta.1-1.21.1",
            "latest install id");
        if (urls.size() == 2) {
            expect(urls[1] == latest_profile_url, "latest profile URL");
        }
    }
}

void test_install_errors() {
    const std::string id = "fabric-loader-0.16.10-1.21.1";
    const auto path_for = [&id](const std::filesystem::path& game_dir) {
        return version_json_path(game_dir, id);
    };

    {
        const auto game_dir = make_temp_dir("bad-profile");
        aurora::LoaderInstallOptions options;
        options.game_version = "1.21.1";
        options.loader_version = "0.16.10";
        options.endpoints.fabric_meta = "https://fixtures.invalid/fabric";
        options.fetch = [](std::string_view) -> std::string {
            return "not-json";
        };
        expect_error_contains(
            [&game_dir, &options] { aurora::install_loader(game_dir, options); },
            "Invalid JSON",
            "invalid profile JSON fails");
        expect(
            !std::filesystem::exists(path_for(game_dir)),
            "invalid profile writes nothing");
    }

    {
        const auto game_dir = make_temp_dir("missing-main");
        aurora::LoaderInstallOptions options;
        options.game_version = "1.21.1";
        options.loader_version = "0.16.10";
        options.endpoints.fabric_meta = "https://fixtures.invalid/fabric";
        options.fetch = [](std::string_view) -> std::string {
            return R"({"id":"x","inheritsFrom":"1.21.1","libraries":[]})";
        };
        expect_error_contains(
            [&game_dir, &options] { aurora::install_loader(game_dir, options); },
            "mainClass",
            "profile without mainClass fails");
        expect(
            !std::filesystem::exists(path_for(game_dir)),
            "invalid mainClass writes nothing");
    }

    {
        const auto game_dir = make_temp_dir("missing-libraries");
        aurora::LoaderInstallOptions options;
        options.game_version = "1.21.1";
        options.loader_version = "0.16.10";
        options.endpoints.fabric_meta = "https://fixtures.invalid/fabric";
        options.fetch = [](std::string_view) -> std::string {
            return R"({"id":"x","inheritsFrom":"1.21.1","mainClass":"x"})";
        };
        expect_error_contains(
            [&game_dir, &options] { aurora::install_loader(game_dir, options); },
            "libraries",
            "profile without libraries fails");
        expect(
            !std::filesystem::exists(path_for(game_dir)),
            "missing libraries writes nothing");
    }

    {
        const auto game_dir = make_temp_dir("bad-list");
        aurora::LoaderInstallOptions options;
        options.game_version = "1.21.1";
        options.endpoints.fabric_meta = "https://fixtures.invalid/fabric";
        options.fetch = [](std::string_view) -> std::string {
            return "{}";
        };
        expect_error_contains(
            [&game_dir, &options] { aurora::install_loader(game_dir, options); },
            "expected a JSON array",
            "loader list must be an array");
        expect(
            !std::filesystem::exists(path_for(game_dir)),
            "invalid loader list writes nothing");
    }

    {
        const auto game_dir = make_temp_dir("all-unstable");
        int calls = 0;
        aurora::LoaderInstallOptions options;
        options.game_version = "1.21.1";
        options.endpoints.fabric_meta = "https://fixtures.invalid/fabric";
        options.fetch = [&calls](std::string_view) -> std::string {
            ++calls;
            return R"([{"loader":{"version":"0.19.0-beta.1","stable":false}}])";
        };
        expect_error_contains(
            [&game_dir, &options] { aurora::install_loader(game_dir, options); },
            "No stable Fabric loader version",
            "stable selection rejects only unstable versions");
        expect(calls == 1, "only loader list fetched for missing stable");
        expect(
            !std::filesystem::exists(path_for(game_dir)),
            "missing stable writes nothing");
    }

    {
        const auto game_dir = make_temp_dir("unsafe-game");
        int calls = 0;
        aurora::LoaderInstallOptions options;
        options.game_version = "../1.21.1";
        options.loader_version = "0.16.10";
        options.fetch = [&calls, &id](std::string_view) -> std::string {
            ++calls;
            return fabric_profile(id);
        };
        expect_error_contains(
            [&game_dir, &options] { aurora::install_loader(game_dir, options); },
            "Unsafe Minecraft version",
            "install rejects traversal game version");
        expect(calls == 0, "unsafe game version performs no network access");
    }

    {
        const auto game_dir = make_temp_dir("empty-game");
        int calls = 0;
        aurora::LoaderInstallOptions options;
        options.loader_version = "0.16.10";
        options.fetch = [&calls, &id](std::string_view) -> std::string {
            ++calls;
            return fabric_profile(id);
        };
        expect_error_contains(
            [&game_dir, &options] { aurora::install_loader(game_dir, options); },
            "Minecraft version is empty",
            "install rejects empty game version");
        expect(calls == 0, "empty game version performs no network access");
    }
}

} // namespace

int main() {
    test_game_versions();
    test_loader_selection();
    test_id_generation();
    test_endpoint_urls();
    test_fetch_functions();
    test_profile_rewrite();
    test_install_writes_profile();
    test_quilt_install();
    test_no_overwrite_and_force();
    test_install_selection();
    test_install_errors();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All loader tests passed\n";
    return 0;
}
