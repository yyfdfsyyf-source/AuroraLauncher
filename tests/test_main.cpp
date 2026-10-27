#include "core/hash.hpp"
#include "core/json.hpp"
#include "core/launch.hpp"
#include "core/maven.hpp"
#include "core/paths.hpp"
#include "core/repository.hpp"
#include "core/rules.hpp"
#include "core/strings.hpp"
#include "core/zip.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
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
                      ("aurora-tests-" + name + "-" + std::to_string(stamp));
    std::filesystem::create_directories(path);
    return path;
}

void test_strings() {
    expect(aurora::split("a;b;;c", ';') == std::vector<std::string>{"a", "b", "c"}, "split");
    expect(aurora::join({"a", "b", "c"}, "/") == "a/b/c", "join");
    expect(aurora::quote_windows_arg("hello world") == "\"hello world\"", "quote simple");
    expect(aurora::replace_all("a-b-c", "-", "+") == "a+b+c", "replace_all");
}

void test_maven() {
    const auto coordinate = aurora::MavenCoordinate::parse("org.lwjgl:lwjgl:3.3.1:natives-windows");
    expect(
        coordinate.path() == "org/lwjgl/lwjgl/3.3.1/lwjgl-3.3.1-natives-windows.jar",
        "Maven classifier path");
    const auto archive = aurora::MavenCoordinate::parse("com.example:data:1.0@zip");
    expect(archive.extension == "zip", "Maven extension");
    expect(aurora::maven_path("a.b:c:1.2") == "a/b/c/1.2/c-1.2.jar", "Maven path");
}

void test_hash() {
    expect(
        aurora::sha1_hex(std::string("abc")) ==
            "a9993e364706816aba3e25717850c26c9cd0d89d",
        "SHA-1 known vector");
    expect(
        aurora::offline_uuid("Notch") == "b50ad385-829d-3141-a216-7e7d7539ba7f",
        "offline UUID known vector");

    const auto dir = make_temp_dir("hash");
    const auto file = dir / "abc.txt";
    aurora::write_text_file(file, "abc");
    expect(
        aurora::sha1_file(file) == "a9993e364706816aba3e25717850c26c9cd0d89d",
        "SHA-1 file");
}

void test_rules() {
    aurora::Environment environment = aurora::detect_environment();
    environment.os_name = "windows";
    environment.arch = "x86_64";

    const auto allow_windows = aurora::parse_rule(
        aurora::parse_json(R"({"action":"allow","os":{"name":"windows"}})", "rule"));
    const auto disallow_windows = aurora::parse_rule(
        aurora::parse_json(R"({"action":"disallow","os":{"name":"windows"}})", "rule"));
    const auto allow_linux = aurora::parse_rule(
        aurora::parse_json(R"({"action":"allow","os":{"name":"linux"}})", "rule"));
    expect(aurora::rules_allow({allow_windows}, environment), "allow windows");
    expect(!aurora::rules_allow({disallow_windows}, environment), "disallow windows");
    expect(!aurora::rules_allow({allow_linux}, environment), "allow linux");
    expect(
        !aurora::rules_allow({allow_windows, disallow_windows}, environment),
        "later rule wins");

    const auto feature_rule = aurora::parse_rule(
        aurora::parse_json(R"({"action":"allow","features":{"has_custom_resolution":true}})", "rule"));
    expect(!aurora::rules_allow({feature_rule}, environment), "feature disabled");
    environment.features["has_custom_resolution"] = true;
    expect(aurora::rules_allow({feature_rule}, environment), "feature enabled");
}

void test_version_inheritance() {
    const auto game_dir = make_temp_dir("versions");
    const auto parent_dir = game_dir / "versions" / "parent";
    const auto child_dir = game_dir / "versions" / "child";
    std::filesystem::create_directories(parent_dir);
    std::filesystem::create_directories(child_dir);

    aurora::write_text_file(
        parent_dir / "parent.json",
        R"({
  "id": "parent",
  "type": "release",
  "mainClass": "com.example.Parent",
  "assets": "parent-assets",
  "javaVersion": { "majorVersion": 17 },
  "assetIndex": { "id": "parent-index", "sha1": "", "size": 0, "url": "" },
  "libraries": [
    { "name": "g:a:1.0", "downloads": { "artifact": { "path": "g/a/1.0/a-1.0.jar" } } }
  ],
  "arguments": {
    "game": [ { "rules": [ { "action": "allow", "os": { "name": "windows" } } ], "value": ["--parent", "${auth_player_name}"] } ],
    "jvm": [ "-Dparent=1" ]
  }
})");
    aurora::write_text_file(
        child_dir / "child.json",
        R"({
  "id": "child",
  "inheritsFrom": "parent",
  "mainClass": "com.example.Child",
  "libraries": [ { "name": "g:b:2.0" } ],
  "arguments": { "game": [ "--child" ], "jvm": [ "-Dchild=2" ] }
})");

    const aurora::VersionRepository repository(game_dir);
    const auto ids = repository.list_ids();
    expect(std::find(ids.begin(), ids.end(), "child") != ids.end(), "repository lists child");

    const aurora::ResolvedVersion resolved = repository.resolve("child");
    expect(resolved.meta.id == "child", "resolved id");
    expect(resolved.meta.main_class == "com.example.Child", "child main class");
    expect(resolved.meta.assets == "parent-assets", "inherited assets");
    expect(resolved.meta.java_major && *resolved.meta.java_major == 17, "inherited java");
    expect(resolved.meta.libraries.size() == 2, "merged libraries");
    expect(resolved.meta.game_arguments.size() == 2, "merged game arguments");
    expect(resolved.meta.jvm_arguments.size() == 2, "merged jvm arguments");
    expect(
        resolved.meta.asset_index && resolved.meta.asset_index->id == "parent-index",
        "inherited asset index");
}

void test_launch_plan() {
    const auto game_dir = make_temp_dir("launch");
    const auto version_dir = game_dir / "versions" / "test";
    std::filesystem::create_directories(version_dir);
    std::filesystem::create_directories(game_dir / "assets" / "indexes");
    aurora::write_text_file(version_dir / "test.json",
                            R"({
  "id": "test",
  "type": "release",
  "mainClass": "com.example.Main",
  "assets": "legacy",
  "minecraftArguments": "${auth_player_name} ${auth_session} --gameDir ${game_directory}",
  "libraries": []
})");
    aurora::write_text_file(version_dir / "test.jar", "dummy");
    aurora::write_text_file(game_dir / "assets" / "indexes" / "legacy.json", "{}");
    const auto java = game_dir / "java.exe";
    aurora::write_text_file(java, "dummy");

    const aurora::VersionRepository repository(game_dir);
    const auto version = repository.resolve("test");
    aurora::LaunchOptions options;
    options.java_path = java;
    options.memory_mb = 4096;
    options.width = 1280;
    options.height = 720;
    options.username = "TestPlayer";
    const auto plan = aurora::build_launch_plan(
        version, aurora::detect_environment(), options);

    const auto contains = [&plan](const std::string& value) {
        return std::find(plan.arguments.begin(), plan.arguments.end(), value) !=
               plan.arguments.end();
    };
    expect(contains("com.example.Main"), "main class in plan");
    expect(contains("-Xmx4096M"), "memory in plan");
    expect(contains("--username") && contains("TestPlayer"), "username in plan");
    expect(contains("--uuid") && contains("bb77495a-a740-3169-a238-69654c8bd2c1"),
           "offline UUID in plan");
    expect(contains("--width") && contains("1280"), "width in plan");
    expect(contains("--height") && contains("720"), "height in plan");
    expect(
        std::any_of(plan.arguments.begin(), plan.arguments.end(), [](const std::string& value) {
            return value.size() >= 8 && value.substr(value.size() - 8) == "test.jar";
        }),
        "client jar in classpath");
    expect(plan.command_line().find("com.example.Main") != std::string::npos, "command line");
}

void test_path_safety() {
    expect(aurora::is_safe_relative_path("g/a/1.0/a.jar"), "normal relative path");
    expect(aurora::is_safe_relative_path("foo//bar"), "empty path segment");
    expect(!aurora::is_safe_relative_path("../outside"), "parent traversal");
    expect(!aurora::is_safe_relative_path("foo/../bar"), "nested parent traversal");
    expect(!aurora::is_safe_relative_path("foo\\..\\bar"), "backslash parent traversal");
    expect(!aurora::is_safe_relative_path("/absolute"), "absolute path");
    expect(!aurora::is_safe_relative_path("C:\\absolute"), "drive path");
    expect(!aurora::is_safe_relative_path("C:relative"), "drive-relative path");
    expect(!aurora::is_safe_relative_path("foo:bar"), "colon/ADS path");
    expect(!aurora::is_safe_relative_path("foo./bar"), "trailing dot path");
    expect(!aurora::is_safe_relative_path("foo /bar"), "trailing space path");
    expect(!aurora::is_safe_relative_path("..."), "dot-only path");
    expect(!aurora::is_safe_relative_path(""), "empty path");
}

void test_modern_launch_plan() {
    const auto game_dir = make_temp_dir("modern-launch");
    const auto version_dir = game_dir / "versions" / "modern";
    std::filesystem::create_directories(version_dir);
    std::filesystem::create_directories(game_dir / "assets" / "indexes");
    aurora::write_text_file(version_dir / "modern.json",
                            R"({
  "id": "modern",
  "type": "release",
  "mainClass": "com.example.Main",
  "assets": "legacy",
  "arguments": {
    "game": [
      "--clientId", "${clientid}",
      "--xuid", "${auth_xuid}",
      "--username", "${auth_player_name}",
      "--version", "${version_name}"
    ]
  },
  "libraries": []
})");
    aurora::write_text_file(version_dir / "modern.jar", "dummy");
    aurora::write_text_file(game_dir / "assets" / "indexes" / "legacy.json", "{}");
    const auto java = game_dir / "java.exe";
    aurora::write_text_file(java, "dummy");

    aurora::LaunchOptions options;
    options.java_path = java;
    options.username = "Player${version_name}";
    const auto version = aurora::VersionRepository(game_dir).resolve("modern");
    const auto plan = aurora::build_launch_plan(
        version, aurora::detect_environment(), options);

    const auto index_of = [&plan](const std::string& value) {
        const auto it = std::find(plan.arguments.begin(), plan.arguments.end(), value);
        return it == plan.arguments.end()
                   ? std::string::npos
                   : static_cast<std::size_t>(it - plan.arguments.begin());
    };
    const auto client_id = index_of("--clientId");
    expect(client_id != std::string::npos && client_id + 1 < plan.arguments.size() &&
               plan.arguments[client_id + 1].empty(),
           "empty client id is preserved");
    const auto xuid = index_of("--xuid");
    expect(xuid != std::string::npos && xuid + 1 < plan.arguments.size() &&
               plan.arguments[xuid + 1].empty(),
           "empty xuid is preserved");
    expect(index_of("Player${version_name}") != std::string::npos,
           "substitution is single-pass");
}

void test_zip() {
    const std::filesystem::path fixture =
        aurora::path_from_utf8(AURORA_TEST_SOURCE_DIR) / "tests" / "fixtures" / "sample.zip";
    const auto destination = make_temp_dir("zip");
    aurora::extract_zip(fixture, destination, [](std::string_view entry) {
        return entry.find("META-INF") == std::string_view::npos;
    });
    expect(std::filesystem::is_regular_file(destination / "libtest.dll"), "zip extracted native");
    expect(
        !std::filesystem::exists(destination / "META-INF" / "MANIFEST.MF"),
        "zip filter skipped manifest");
}

} // namespace

int main() {
    test_strings();
    test_maven();
    test_hash();
    test_rules();
    test_version_inheritance();
    test_launch_plan();
    test_modern_launch_plan();
    test_path_safety();
    test_zip();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All tests passed\n";
    return 0;
}
