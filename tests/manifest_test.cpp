#include "core/error.hpp"
#include "core/hash.hpp"
#include "core/install.hpp"
#include "core/json.hpp"
#include "core/launch.hpp"
#include "core/manifest.hpp"
#include "core/paths.hpp"
#include "core/repository.hpp"
#include "core/rules.hpp"
#include "core/strings.hpp"
#include "core/version.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
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

template <typename Function>
void expect_error(Function&& function,
                  std::string_view expected,
                  const std::string& message) {
    try {
        function();
        expect(false, message + " (no error was thrown)");
    } catch (const aurora::Error& error) {
        const std::string text = error.what();
        if (text.find(expected) == std::string::npos) {
            expect(false, message + " (unexpected error: " + text + ")");
        }
    } catch (const std::exception& error) {
        expect(false, message + " (unexpected exception: " + std::string(error.what()) + ")");
    }
}

std::filesystem::path make_temp_dir(const std::string& name) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
                      ("aurora-manifest-tests-" + name + "-" + std::to_string(stamp));
    std::filesystem::create_directories(path);
    return path;
}

std::size_t count_prefix(const std::vector<std::string>& values, std::string_view prefix) {
    return static_cast<std::size_t>(
        std::count_if(values.begin(), values.end(), [prefix](const std::string& value) {
            return value.size() >= prefix.size() &&
                   value.compare(0, prefix.size(), prefix) == 0;
        }));
}

bool contains_value(const std::vector<std::string>& values, const std::string& expected) {
    return std::find(values.begin(), values.end(), expected) != values.end();
}

aurora::Environment test_environment() {
    aurora::Environment environment;
    environment.os_name = "windows";
    environment.arch = "x86_64";
    environment.os_version = "10.0.0";
    return environment;
}

const char* kSampleManifest = R"({
  "latest": { "release": "1.20.4", "snapshot": "24w03a" },
  "versions": [
    {
      "id": "1.20.4",
      "type": "release",
      "url": "https://example.invalid/1.20.4.json",
      "sha1": "ABCDEF0123456789ABCDEF0123456789ABCDEF01",
      "releaseTime": "2023-12-07T12:00:00+00:00",
      "time": "2023-12-07T12:01:00+00:00"
    },
    {
      "id": "24w03a",
      "type": "snapshot",
      "url": "https://example.invalid/24w03a.json",
      "sha1": "0123456789abcdef0123456789abcdef01234567"
    },
    {
      "id": "b1.7.3",
      "type": "old_beta",
      "url": "https://example.invalid/b1.7.3.json",
      "sha1": "1111111111111111111111111111111111111111",
      "releaseTime": "2011-07-08T00:00:00+00:00",
      "time": "2013-05-01T00:00:00+00:00"
    }
  ]
})";

void test_manifest_parsing() {
    const aurora::VersionManifest manifest =
        aurora::parse_version_manifest(kSampleManifest, "sample manifest");
    expect(manifest.versions.size() == 3, "manifest version count");
    expect(manifest.latest.has_value(), "manifest latest present");
    expect(manifest.latest->release == "1.20.4", "manifest latest release");
    expect(manifest.latest->snapshot == "24w03a", "manifest latest snapshot");

    const aurora::VersionManifestEntry& release = manifest.latest_release();
    expect(release.id == "1.20.4", "latest release selection");
    expect(release.type == "release", "latest release type");
    expect(manifest.latest_snapshot().id == "24w03a", "latest snapshot selection");

    const aurora::VersionManifestEntry* entry = manifest.find("b1.7.3");
    expect(entry != nullptr, "lookup by id");
    if (entry != nullptr) {
        expect(entry->type == "old_beta", "lookup type");
        expect(entry->release_time.has_value(), "optional releaseTime present");
        expect(entry->time.has_value(), "optional time present");
    }
    expect(manifest.find("missing") == nullptr, "missing lookup returns null");
    expect(manifest.require("1.20.4").url == "https://example.invalid/1.20.4.json",
           "require lookup");

    const aurora::VersionManifestEntry& release_entry = manifest.require("1.20.4");
    expect(release_entry.sha1 == "abcdef0123456789abcdef0123456789abcdef01",
           "manifest SHA-1 is normalized");
    expect(release_entry.release_time.has_value() &&
               *release_entry.release_time == "2023-12-07T12:00:00+00:00",
           "manifest releaseTime value");
    expect(!manifest.require("24w03a").release_time.has_value(),
           "missing releaseTime stays empty");

    expect(aurora::version_manifest_url("http://127.0.0.1:8123/") ==
               "http://127.0.0.1:8123/mc/game/version_manifest_v2.json",
           "manifest URL from base with trailing slash");
    expect(aurora::version_manifest_url("http://127.0.0.1:8123") ==
               "http://127.0.0.1:8123/mc/game/version_manifest_v2.json",
           "manifest URL from plain base");
    expect(aurora::kOfficialVersionManifestUrl ==
               "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json",
           "official manifest URL");
    expect_error(
        [] { (void)aurora::version_manifest_url(""); },
        "empty",
        "empty manifest base URL");
}

void test_manifest_errors() {
    expect_error(
        [] {
            const auto manifest = aurora::parse_version_manifest(
                R"({"versions":[]})", "no latest");
            (void)manifest.latest_release();
        },
        "latest.release",
        "manifest without latest.release");
    expect_error(
        [] {
            const auto manifest = aurora::parse_version_manifest(
                R"({"latest":{"release":"missing","snapshot":"missing"},"versions":[
                    {"id":"other","type":"release","url":"u","sha1":"s"}]})",
                "bad latest");
            (void)manifest.latest_release();
        },
        "does not exist",
        "latest.release missing from versions");
    expect_error(
        [] { (void)aurora::parse_version_manifest(R"({})", "bad"); },
        "no versions array",
        "manifest without versions array");
    expect_error(
        [] {
            (void)aurora::parse_version_manifest(
                R"({"versions":[{"id":"a","type":"release","url":"u"}]})",
                "bad");
        },
        "no SHA-1",
        "manifest entry without SHA-1");
    expect_error(
        [] {
            (void)aurora::parse_version_manifest(
                R"({"versions":[
                    {"id":"a","type":"release","url":"u","sha1":"s"},
                    {"id":"a","type":"release","url":"u","sha1":"s"}]})",
                "bad");
        },
        "duplicate version",
        "manifest duplicate version");
    expect_error(
        [] {
            const auto manifest = aurora::parse_version_manifest(kSampleManifest, "sample");
            (void)manifest.require("missing");
        },
        "not present",
        "require unknown version");
    expect_error(
        [] { (void)aurora::parse_version_manifest("[]", "array"); },
        "must be a JSON object",
        "manifest root must be an object");
}

void test_version_json_install() {
    const auto game_dir = make_temp_dir("install");
    const std::string version_json =
        R"({"id":"1.20.4","type":"release","mainClass":"com.example.Main"})";
    const std::string version_sha = aurora::sha1_hex(version_json);

    aurora::VersionManifestEntry entry;
    entry.id = "1.20.4";
    entry.type = "release";
    entry.url = "https://example.invalid/1.20.4.json";
    entry.sha1 = version_sha;

    int calls = 0;
    aurora::VersionJsonInstallOptions options;
    options.download = [&](std::string_view url,
                           const std::filesystem::path& destination,
                           std::string_view sha1) {
        ++calls;
        expect(url == entry.url, "version JSON download URL");
        expect(sha1 == version_sha, "version JSON download SHA-1");
        aurora::write_text_file(destination, version_json);
    };

    const std::filesystem::path expected =
        game_dir / "versions" / "1.20.4" / "1.20.4.json";
    expect(aurora::version_json_path(game_dir, "1.20.4") == expected,
           "version JSON path");

    const aurora::VersionJsonInstallResult installed =
        aurora::install_version_json(entry, game_dir, options);
    expect(installed.downloaded, "version JSON was downloaded");
    expect(installed.path == expected, "installed version JSON path");
    expect(std::filesystem::is_regular_file(expected), "version JSON exists");
    expect(aurora::read_text_file(expected) == version_json, "version JSON content");

    const aurora::VersionJsonInstallResult again =
        aurora::install_version_json(entry, game_dir, options);
    expect(!again.downloaded, "existing version JSON is not downloaded again");
    expect(calls == 1, "existing version JSON did not call the downloader");

    aurora::write_text_file(expected, R"({"id":"1.19.4"})");
    expect_error(
        [&] { (void)aurora::install_version_json(entry, game_dir, options); },
        "already exists",
        "different existing version JSON is rejected");
    expect(calls == 1, "rejected version JSON did not download");
    expect(aurora::read_text_file(expected) == R"({"id":"1.19.4"})",
           "rejected version JSON was preserved");

    options.force = true;
    const aurora::VersionJsonInstallResult replaced =
        aurora::install_version_json(entry, game_dir, options);
    expect(replaced.downloaded, "force replaces version JSON");
    expect(calls == 2, "force downloads the version JSON");
    expect(aurora::read_text_file(expected) == version_json, "force replaced content");

    options.force = false;

    aurora::VersionJsonInstallOptions bad_download;
    bad_download.force = true;
    const std::string bad_json = R"({"id":"different"})";
    bad_download.download = [&](std::string_view,
                                const std::filesystem::path& destination,
                                std::string_view) {
        aurora::write_text_file(destination, bad_json);
    };
    aurora::VersionManifestEntry bad_entry = entry;
    bad_entry.sha1 = aurora::sha1_hex(bad_json);
    expect_error(
        [&] { (void)aurora::install_version_json(bad_entry, game_dir, bad_download); },
        "declares id",
        "downloaded version JSON id mismatch");
    expect(aurora::read_text_file(expected) == version_json,
           "failed forced install preserved the old version JSON");

    const auto wrong_dir = make_temp_dir("wrong-id");
    const std::string wrong_json = R"({"id":"other"})";
    aurora::VersionManifestEntry wrong = entry;
    wrong.id = "1.20.5";
    wrong.sha1 = aurora::sha1_hex(wrong_json);
    aurora::VersionJsonInstallOptions wrong_options;
    wrong_options.download = [&](std::string_view,
                                 const std::filesystem::path& destination,
                                 std::string_view) {
        aurora::write_text_file(destination, wrong_json);
    };
    expect_error(
        [&] { (void)aurora::install_version_json(wrong, wrong_dir, wrong_options); },
        "declares id",
        "downloaded version JSON has the wrong id");
    expect(!std::filesystem::exists(aurora::version_json_path(wrong_dir, "1.20.5")),
           "wrong-id version JSON was not installed");

    const auto mismatch_dir = make_temp_dir("sha-mismatch");
    aurora::VersionManifestEntry mismatch = entry;
    mismatch.id = "1.20.6";
    mismatch.sha1 = std::string(40, '0');
    aurora::VersionJsonInstallOptions mismatch_options;
    mismatch_options.download = [&](std::string_view,
                                    const std::filesystem::path& destination,
                                    std::string_view) {
        aurora::write_text_file(destination, version_json);
    };
    expect_error(
        [&] { (void)aurora::install_version_json(mismatch, mismatch_dir, mismatch_options); },
        "SHA-1 mismatch",
        "version JSON SHA-1 mismatch");
    expect(!std::filesystem::exists(aurora::version_json_path(mismatch_dir, "1.20.6")),
           "SHA-1 mismatch was not installed");

    aurora::VersionManifestEntry unsafe = entry;
    unsafe.id = "../escape";
    expect_error(
        [&] { (void)aurora::install_version_json(unsafe, game_dir, options); },
        "Unsafe",
        "unsafe version id is rejected");
    expect_error(
        [&] { (void)aurora::version_json_path(game_dir, "../escape"); },
        "Unsafe",
        "unsafe version JSON path is rejected");

    const auto manifest = aurora::parse_version_manifest(
        R"({"latest":{"release":"1.20.4","snapshot":"24w03a"},"versions":[
            {"id":"1.20.4","type":"release",
             "url":"https://example.invalid/1.20.4.json",
             "sha1":"0123456789abcdef0123456789abcdef01234567"}]})",
        "install manifest");
    const auto by_manifest = aurora::install_version_json(
        manifest, "1.20.4", game_dir, options);
    expect(!by_manifest.downloaded, "manifest lookup installs existing version JSON");
    expect_error(
        [&] { (void)aurora::install_version_json(manifest, "missing", game_dir, options); },
        "not present",
        "manifest lookup rejects unknown id");
}

void test_logging_parsing_and_merge() {
    const auto parent = aurora::VersionMeta::parse(
        aurora::parse_json(R"({
  "id": "parent",
  "type": "release",
  "mainClass": "com.example.Parent",
  "logging": {
    "client": {
      "argument": "-Dlog4j.configurationFile=${path}",
      "file": {
        "id": "parent-log4j.xml",
        "url": "https://example.invalid/parent-log4j.xml",
        "sha1": "ABCDEF0123456789ABCDEF0123456789ABCDEF01",
        "size": 512
      }
    }
  }
})", "parent"));

    expect(parent.logging_client.has_value(), "logging.client parsed");
    if (parent.logging_client) {
        expect(parent.logging_client->argument == "-Dlog4j.configurationFile=${path}",
               "logging argument parsed");
        expect(parent.logging_client->file.has_value(), "logging file parsed");
        if (parent.logging_client->file) {
            expect(parent.logging_client->file->id == "parent-log4j.xml",
                   "logging file id");
            expect(parent.logging_client->file->url ==
                       "https://example.invalid/parent-log4j.xml",
                   "logging file URL");
            expect(parent.logging_client->file->sha1 ==
                       "abcdef0123456789abcdef0123456789abcdef01",
                   "logging file SHA-1 normalized");
            expect(parent.logging_client->file->size == 512, "logging file size");
            expect(parent.logging_client->file->has_url(), "logging file has URL");
        }
        expect(parent.logging_client->allowed(test_environment()),
               "logging without rules is allowed");
    }

    const auto no_logging = aurora::VersionMeta::parse(
        aurora::parse_json(R"({"id":"plain"})", "plain"));
    expect(!no_logging.logging_client.has_value(), "logging absent is empty");

    const auto child_argument = aurora::VersionMeta::parse(
        aurora::parse_json(R"({
  "id": "child",
  "type": "release",
  "mainClass": "com.example.Child",
  "logging": { "client": { "argument": "-Dchild=${path}" } }
})", "child"));
    const auto merged_argument = aurora::merge_versions(parent, child_argument);
    expect(merged_argument.logging_client.has_value(), "merged logging client");
    if (merged_argument.logging_client) {
        expect(merged_argument.logging_client->argument == "-Dchild=${path}",
               "child logging argument overrides parent");
        expect(merged_argument.logging_client->file.has_value() &&
                   merged_argument.logging_client->file->id == "parent-log4j.xml",
               "parent logging file is inherited");
    }

    const auto child_file = aurora::VersionMeta::parse(
        aurora::parse_json(R"({
  "id": "child-file",
  "type": "release",
  "mainClass": "com.example.Child",
  "logging": { "client": { "file": {
    "id": "child-log4j.xml",
    "url": "https://example.invalid/child-log4j.xml",
    "sha1": "1111111111111111111111111111111111111111",
    "size": 256
  } } }
})", "child-file"));
    const auto merged_file = aurora::merge_versions(parent, child_file);
    expect(merged_file.logging_client.has_value(), "merged logging file");
    if (merged_file.logging_client) {
        expect(merged_file.logging_client->argument ==
                   "-Dlog4j.configurationFile=${path}",
               "parent logging argument is inherited");
        expect(merged_file.logging_client->file.has_value() &&
                   merged_file.logging_client->file->id == "child-log4j.xml",
               "child logging file overrides parent");
    }

    const auto child_plain = aurora::VersionMeta::parse(
        aurora::parse_json(R"({"id":"plain-child","type":"release"})", "plain-child"));
    const auto merged_plain = aurora::merge_versions(parent, child_plain);
    expect(merged_plain.logging_client.has_value() &&
               merged_plain.logging_client->argument == "-Dlog4j.configurationFile=${path}",
           "logging client is inherited");

    aurora::LoggingClient restricted = *parent.logging_client;
    restricted.rules.push_back(aurora::parse_rule(aurora::parse_json(
        R"({"action":"allow"})", "logging rule")));
    restricted.rules.push_back(aurora::parse_rule(aurora::parse_json(
        R"({"action":"disallow","os":{"name":"windows"}})", "logging rule")));
    expect(!restricted.allowed(test_environment()), "logging rules disable client");
    aurora::Environment linux_environment = test_environment();
    linux_environment.os_name = "linux";
    expect(restricted.allowed(linux_environment), "logging rules allow other OS");
}

void test_logging_install_and_launch() {
    const auto game_dir = make_temp_dir("logging-launch");
    const auto version_dir = game_dir / "versions" / "logged";
    std::filesystem::create_directories(version_dir);
    std::filesystem::create_directories(game_dir / "assets" / "indexes");
    aurora::write_text_file(version_dir / "logged.json", R"({
  "id": "logged",
  "type": "release",
  "mainClass": "com.example.Main",
  "assets": "legacy",
  "arguments": { "jvm": [ "-Dversion.only=1" ] },
  "logging": {
    "client": {
      "argument": "-Dlog4j.configurationFile=${path}",
      "file": {
        "id": "client-1.12.xml",
        "url": "https://example.invalid/client-1.12.xml",
        "sha1": "0000000000000000000000000000000000000000",
        "size": 128
      }
    }
  },
  "libraries": []
})");
    aurora::write_text_file(version_dir / "logged.jar", "dummy");
    aurora::write_text_file(game_dir / "assets" / "indexes" / "legacy.json", "{}");
    const auto java = game_dir / "java.exe";
    aurora::write_text_file(java, "dummy");

    const aurora::VersionRepository repository(game_dir);
    const aurora::ResolvedVersion version = repository.resolve("logged");
    const auto log_path =
        version.meta.logging_config_file(game_dir);
    const auto expected_log_path =
        game_dir / "assets" / "log_configs" / "client-1.12.xml";
    expect(log_path == expected_log_path, "logging config install path");

    const aurora::Environment environment = test_environment();
    aurora::VersionMeta offline = version.meta;
    offline.logging_client->file->url.clear();
    expect(!aurora::install_logging_config(offline, game_dir, environment, false),
           "missing logging config without URL is not fatal");
    expect(!std::filesystem::is_regular_file(expected_log_path),
           "missing logging config was not created");

    const auto make_options = [&]() {
        aurora::LaunchOptions options;
        options.java_path = java;
        options.username = "TestPlayer";
        options.width = 0;
        options.height = 0;
        return options;
    };

    expect_error(
        [&] {
            (void)aurora::build_launch_plan(version, environment, make_options());
        },
        "Logging configuration is missing",
        "launch reports a missing logging config");
    expect_error(
        [&] {
            (void)aurora::build_launch_plan(version, environment, make_options());
        },
        "install",
        "missing logging config suggests install");

    aurora::ResolvedVersion skipped = version;
    skipped.meta.logging_client->rules.push_back(aurora::parse_rule(
        aurora::parse_json(R"({"action":"allow"})", "logging rule")));
    skipped.meta.logging_client->rules.push_back(aurora::parse_rule(
        aurora::parse_json(R"({"action":"disallow","os":{"name":"windows"}})",
                           "logging rule")));
    const auto skipped_plan = aurora::build_launch_plan(
        skipped, environment, make_options());
    expect(count_prefix(skipped_plan.arguments, "-Dlog4j.configurationFile=") == 0,
           "logging rules prevent argument injection");

    aurora::write_text_file(expected_log_path, "log4j configuration");
    expect(aurora::install_logging_config(offline, game_dir, environment, false),
           "existing logging config is reported ready");

    const auto plan = aurora::build_launch_plan(version, environment, make_options());
    const std::string expected_argument =
        "-Dlog4j.configurationFile=" +
        aurora::path_to_utf8(aurora::absolute_path(expected_log_path));
    expect(contains_value(plan.arguments, expected_argument),
           "logging ${path} is substituted with the absolute config path");
    expect(count_prefix(plan.arguments, "-Dlog4j.configurationFile=") == 1,
           "logging argument is injected exactly once");

    aurora::LaunchOptions user_override = make_options();
    user_override.extra_jvm_args.push_back(
        "-Dlog4j.configurationFile=C:\\custom\\log4j.xml");
    const auto user_plan =
        aurora::build_launch_plan(version, environment, user_override);
    expect(count_prefix(user_plan.arguments, "-Dlog4j.configurationFile=") == 1,
           "user logging option prevents duplication");
    expect(contains_value(user_plan.arguments,
                          "-Dlog4j.configurationFile=C:\\custom\\log4j.xml"),
           "user logging option is preserved");

    aurora::ResolvedVersion version_override = repository.resolve("logged");
    aurora::Argument version_argument;
    version_argument.values.push_back("-Dlog4j.configurationFile=version.xml");
    version_override.meta.jvm_arguments.push_back(std::move(version_argument));
    const auto version_plan =
        aurora::build_launch_plan(version_override, environment, make_options());
    expect(count_prefix(version_plan.arguments, "-Dlog4j.configurationFile=") == 1,
           "version logging option prevents duplication");
    expect(contains_value(version_plan.arguments,
                          "-Dlog4j.configurationFile=version.xml"),
           "version logging option is preserved");

    aurora::ResolvedVersion path_override = repository.resolve("logged");
    aurora::Argument path_argument;
    path_argument.values.push_back("-Dcustom.log=${path}");
    path_override.meta.jvm_arguments.push_back(std::move(path_argument));
    const auto path_plan =
        aurora::build_launch_plan(path_override, environment, make_options());
    expect(contains_value(path_plan.arguments,
                          "-Dcustom.log=" +
                              aurora::path_to_utf8(aurora::absolute_path(expected_log_path))),
           "version JVM arguments can use the logging ${path}");

    aurora::ResolvedVersion no_file = repository.resolve("logged");
    no_file.meta.logging_client->file.reset();
    expect_error(
        [&] {
            (void)aurora::build_launch_plan(no_file, environment, make_options());
        },
        "has no logging file",
        "logging ${path} without a file fails clearly");

    aurora::VersionMeta unsafe = version.meta;
    unsafe.logging_client->file->id = "../escape.xml";
    expect_error(
        [&] { (void)unsafe.logging_config_file(game_dir); },
        "Unsafe",
        "unsafe logging config path is rejected");

    const auto install_dir = make_temp_dir("logging-install");
    const auto install_version_dir = install_dir / "versions" / "logged";
    std::filesystem::create_directories(install_version_dir);
    aurora::write_text_file(install_version_dir / "logged.jar", "dummy");
    aurora::VersionMeta install_meta = version.meta;
    install_meta.logging_client->file->url.clear();
    const aurora::ResolvedVersion install_resolved{
        install_meta, install_dir, install_version_dir / "logged.json"};
    aurora::InstallOptions install_options;
    install_options.assets = false;
    install_options.libraries = false;
    install_options.natives = false;
    aurora::install_version(install_resolved, environment, install_options);
    expect(!std::filesystem::is_regular_file(
               install_meta.logging_config_file(install_dir)),
           "install tolerates a logging config without a download URL");
}

} // namespace

int main() {
    test_manifest_parsing();
    test_manifest_errors();
    test_version_json_install();
    test_logging_parsing_and_merge();
    test_logging_install_and_launch();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All manifest tests passed\n";
    return 0;
}
