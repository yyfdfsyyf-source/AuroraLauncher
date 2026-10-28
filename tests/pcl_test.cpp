#include "core/error.hpp"
#include "core/paths.hpp"
#include "core/pcl.hpp"
#include "core/repository.hpp"

#include <chrono>
#include <exception>
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

std::filesystem::path make_temp_dir(const std::string& name) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
                      ("aurora-pcl-tests-" + name + "-" + std::to_string(stamp));
    std::filesystem::create_directories(path);
    return path;
}

void cleanup(const std::filesystem::path& directory) {
    std::error_code error;
    std::filesystem::remove_all(directory, error);
}

void write(const std::filesystem::path& path, std::string_view text) {
    aurora::write_text_file(path, text);
}

void test_legacy_setup_ini() {
    const auto version_dir = make_temp_dir("legacy");
    aurora::ensure_directory(version_dir / "PCL");
    write(version_dir / "PCL" / "Setup.ini",
          "VersionArgumentIndie:1\n"
          "VersionArgumentIndieV2:True\n"
          "VersionRamType:1\n"
          "VersionRamCustom:25\n"
          "VersionAdvanceJvm:-Xss1M -Dfoo=\"two words\"\n"
          "VersionAdvanceGame:--width 1280 --height 720\n"
          "VersionArgumentJavaSelect:C:\\fake\\java.exe\n");

    const auto settings = aurora::read_pcl_instance_settings(version_dir);
    expect(settings.detected && settings.isolated, "legacy PCL isolation");
    expect(
        settings.config_path.filename() == "Setup.ini",
        "legacy setup.ini is detected");
    expect(settings.memory_mb == 8192, "PCL memory tick maps to 8 GB");
    expect(
        settings.jvm_args.size() == 2 &&
            settings.jvm_args[1] == "-Dfoo=two words",
        "PCL JVM arguments split quotes");
    expect(
        settings.game_args.size() == 4 && settings.game_args[0] == "--width",
        "PCL game arguments are split");
    expect(
        settings.java_path == "C:\\fake\\java.exe",
        "PCL selected Java path is read");
    cleanup(version_dir);
}

void test_long_java_path() {
    const auto version_dir = make_temp_dir("long-java");
    aurora::ensure_directory(version_dir / "PCL");
    const std::string java = "C:\\Users\\YYF\\AppData\\Local\\Temp\\"
                             "aurora-pcl-smoke-fosfz465\\java.exe";
    write(version_dir / "PCL" / "Setup.ini",
          "VersionArgumentIndie:1\r\n"
          "VersionRamType:1\r\n"
          "VersionRamCustom:25\r\n"
          "VersionAdvanceJvm:-Xss1M\r\n"
          "VersionAdvanceGame:--width 1280\r\n"
          "VersionArgumentJavaSelect:" + java + "\r\n");
    const auto settings = aurora::read_pcl_instance_settings(version_dir);
    expect(
        settings.java_path == java,
        "PCL long Java path with CRLF is read: got '" + settings.java_path + "'");
    expect(settings.memory_mb == 8192, "long-path fixture keeps PCL memory");
    cleanup(version_dir);
}

void test_java_preference_json() {
    const auto game_dir = make_temp_dir("java-json");
    const auto version_dir = game_dir / "versions" / "ce";
    aurora::ensure_directory(version_dir / "PCL");
    write(version_dir / "PCL" / "Setup.ini",
          "VersionArgumentJavaSelect:{\"kind\":\"exist\","
          "\"JavaExePath\":\"C:\\\\fake\\\\java.exe\"}\n");
    expect(
        aurora::read_pcl_instance_settings(version_dir).java_path ==
            "C:\\fake\\java.exe",
        "PCL CE exists-kind JSON Java preference is read");

    write(version_dir / "PCL" / "Setup.ini",
          "VersionArgumentJavaSelect:{\"kind\":\"auto\"}\n");
    expect(
        aurora::read_pcl_instance_settings(version_dir).java_path.empty(),
        "PCL CE auto Java preference is not treated as a path");

    aurora::ensure_directory(game_dir / "tools");
    write(game_dir / "tools" / "java.exe", "x");
    write(version_dir / "PCL" / "Setup.ini",
          "VersionArgumentJavaSelect:{\"kind\":\"relative\","
          "\"RelativePath\":\"tools\\\\java.exe\"}\n");
    expect(
        aurora::read_pcl_instance_settings(version_dir).java_path ==
            aurora::path_to_utf8(game_dir / "tools" / "java.exe"),
        "PCL CE relative Java preference resolves against the game folder");
    cleanup(game_dir);
}

void test_pcl_ce_config() {
    const auto version_dir = make_temp_dir("ce");
    aurora::ensure_directory(version_dir / "PCL");
    write(version_dir / "PCL" / "config.v1.yml",
          "VersionArgumentIndieV2: true\n"
          "VersionArgumentIndie: 1\n"
          "VersionRamType: 2\n"
          "VersionRamCustom: 20\n");
    const auto settings = aurora::read_pcl_instance_settings(version_dir);
    expect(
        settings.detected && settings.isolated && settings.memory_mb == 0,
        "PCL CE config is read and global memory is not overridden");

    write(version_dir / "PCL" / "config.v1.yml",
          "VersionRamType: 0\n"
          "VersionRamCustom: 20\n");
    expect(
        aurora::read_pcl_instance_settings(version_dir).memory_mb == 0,
        "PCL automatic memory does not become a fixed instance override");

    write(version_dir / "PCL" / "config.v1.yml",
          "VersionRamType: 1\n"
          "VersionRamCustom: 20\n");
    expect(
        aurora::read_pcl_instance_settings(version_dir).memory_mb == 5632,
        "PCL manual memory tick 20 maps to 5.5 GB");

    write(version_dir / "PCL" / "config.v1.yml", "VersionRamType: 1\n");
    expect(
        aurora::read_pcl_instance_settings(version_dir).memory_mb == 3072,
        "PCL manual memory without a tick uses the default slider value");
    cleanup(version_dir);
}

void test_auto_isolation() {
    const auto mods_dir = make_temp_dir("mods");
    aurora::ensure_directory(mods_dir / "mods");
    write(mods_dir / "mods" / "a.jar", "x");
    const auto mods_settings = aurora::read_pcl_instance_settings(mods_dir);
    expect(
        !mods_settings.detected && mods_settings.auto_isolated &&
            mods_settings.isolated,
        "a non-empty mods folder isolates a version like PCL");

    const auto saves_dir = make_temp_dir("saves");
    aurora::ensure_directory(saves_dir / "saves" / "world");
    const auto saves_settings = aurora::read_pcl_instance_settings(saves_dir);
    expect(
        !saves_settings.detected && saves_settings.isolated,
        "a saves world isolates a version like PCL");

    const auto empty_dir = make_temp_dir("empty");
    aurora::ensure_directory(empty_dir / "mods");
    expect(
        !aurora::read_pcl_instance_settings(empty_dir).isolated,
        "an empty mods folder does not isolate a version");

    const auto disabled = make_temp_dir("disabled");
    aurora::ensure_directory(disabled / "PCL");
    aurora::ensure_directory(disabled / "mods");
    write(disabled / "mods" / "a.jar", "x");
    write(disabled / "PCL" / "Setup.ini", "VersionArgumentIndieV2:False\n");
    const auto disabled_settings = aurora::read_pcl_instance_settings(disabled);
    expect(
        !disabled_settings.isolated && disabled_settings.auto_isolated,
        "an explicit PCL isolation=false wins over the mods fallback");

    cleanup(mods_dir);
    cleanup(saves_dir);
    cleanup(empty_dir);
    cleanup(disabled);
}

void test_repository_run_dir() {
    const auto game_dir = make_temp_dir("repo");
    const auto versions = game_dir / "versions";
    aurora::ensure_directory(versions / "pcl");
    aurora::ensure_directory(versions / "vanilla");
    write(versions / "pcl" / "pcl.json",
          R"({"id":"pcl","mainClass":"net.minecraft.client.main.Main"})");
    write(versions / "vanilla" / "vanilla.json",
          R"({"id":"vanilla","mainClass":"net.minecraft.client.main.Main"})");
    aurora::ensure_directory(versions / "pcl" / "PCL");
    write(versions / "pcl" / "PCL" / "Setup.ini", "VersionArgumentIndie:1\n");
    aurora::ensure_directory(versions / "vanilla" / "mods");
    write(versions / "vanilla" / "mods" / "a.jar", "x");

    const aurora::VersionRepository repository(game_dir);
    expect(
        repository.resolve("pcl").run_dir == versions / "pcl" &&
            repository.run_dir_for("pcl") == versions / "pcl",
        "PCL isolation switches the launch directory to versions/<id>");

    aurora::ensure_directory(versions / "root");
    write(versions / "root" / "root.json",
          R"({"id":"root","mainClass":"net.minecraft.client.main.Main"})");
    expect(
        repository.resolve("root").run_dir == game_dir &&
            repository.run_dir_for("root") == game_dir,
        "versions without PCL isolation keep the shared game directory");
    cleanup(game_dir);
}

void test_argument_split() {
    const auto args = aurora::split_pcl_arguments(
        "-Xmx2G -Dname='hello world' -Dtext=\"a \\\"b\\\" c\"");
    expect(
        args.size() == 3 && args[1] == "-Dname=hello world" &&
            args[2] == "-Dtext=a \"b\" c",
        "quoted PCL arguments keep spaces and escaped quotes");
    expect(aurora::split_pcl_arguments("  ").empty(),
           "empty PCL arguments produce no tokens");
}

} // namespace

int main() {
    try {
        test_legacy_setup_ini();
        test_long_java_path();
        test_java_preference_json();
        test_pcl_ce_config();
        test_auto_isolation();
        test_repository_run_dir();
        test_argument_split();
    } catch (const std::exception& error) {
        std::cerr << "Unexpected exception: " << error.what() << '\n';
        return 1;
    }

    if (failures != 0) {
        std::cerr << failures << " PCL test(s) failed\n";
        return 1;
    }
    std::cout << "All PCL tests passed\n";
    return 0;
}
