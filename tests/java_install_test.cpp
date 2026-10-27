#include "core/error.hpp"
#include "core/hash.hpp"
#include "core/java.hpp"
#include "core/java_install.hpp"
#include "core/json.hpp"
#include "core/paths.hpp"
#include "core/strings.hpp"

#include <miniz.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
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

template <typename Function>
bool throws_error(Function&& function) {
    try {
        function();
    } catch (const aurora::Error&) {
        return true;
    } catch (const std::exception&) {
        return false;
    } catch (...) {
        return false;
    }
    return false;
}

std::filesystem::path make_temp_dir(const std::string& name) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
                      ("aurora-java-install-tests-" + name + "-" +
                       std::to_string(stamp));
    std::filesystem::create_directories(path);
    return path;
}

void write_zip(const std::filesystem::path& archive,
               const std::vector<std::pair<std::string, std::string>>& entries) {
    mz_zip_archive zip{};
    if (!mz_zip_writer_init_heap(&zip, 0, 0)) {
        aurora::fail("Unable to initialize the ZIP fixture writer");
    }

    bool ok = true;
    for (const auto& [name, content] : entries) {
        if (!mz_zip_writer_add_mem(
                &zip, name.c_str(), content.data(), content.size(),
                static_cast<mz_uint>(MZ_DEFAULT_COMPRESSION))) {
            ok = false;
            break;
        }
    }

    void* buffer = nullptr;
    std::size_t size = 0;
    if (ok) {
        ok = mz_zip_writer_finalize_heap_archive(&zip, &buffer, &size) != 0;
    }
    if (ok) {
        std::ofstream output(archive, std::ios::binary | std::ios::trunc);
        if (!output) {
            ok = false;
        } else {
            output.write(static_cast<const char*>(buffer),
                         static_cast<std::streamsize>(size));
            output.flush();
            if (!output) {
                ok = false;
            }
        }
    }
    mz_zip_writer_end(&zip);
    if (buffer != nullptr) {
        mz_free(buffer);
    }
    if (!ok) {
        aurora::fail("Unable to create the ZIP fixture");
    }
}

void write_fake_java_home(const std::filesystem::path& home,
                          const std::string& version = "21.0.1") {
    aurora::ensure_directory(home / "bin");
    aurora::write_text_file(home / "bin" / "java.exe", "fake java");
    aurora::write_text_file(home / "bin" / "java", "fake java");
    aurora::write_text_file(
        home / "release",
        "JAVA_VERSION=\"" + version +
            "\"\nIMPLEMENTOR=\"Eclipse Adoptium\"\nJAVA_VENDOR=\"Eclipse Adoptium\"\n");
}

std::string package_metadata(const std::string& checksum,
                             std::uint64_t size,
                             const std::string& release_name,
                             const std::string& package_name,
                             const std::string& link) {
    aurora::Json binary = aurora::Json::object();
    binary["os"] = "windows";
    binary["architecture"] = "x64";
    binary["image_type"] = "jdk";
    binary["jvm_impl"] = "hotspot";

    aurora::Json package = aurora::Json::object();
    package["name"] = package_name;
    package["link"] = link;
    package["checksum"] = checksum;
    package["size"] = static_cast<std::int64_t>(size);
    binary["package"] = std::move(package);

    aurora::Json version = aurora::Json::object();
    version["semver"] = "21.0.1+12";
    version["major"] = 21;
    version["openjdk_version"] = "21.0.1+12-LTS";

    aurora::Json asset = aurora::Json::object();
    asset["binary"] = std::move(binary);
    asset["release_name"] = release_name;
    asset["vendor"] = "eclipse";
    asset["version"] = std::move(version);

    aurora::Json root = aurora::Json::array();
    root.push_back(std::move(asset));
    return root.dump();
}

void test_sha256() {
    expect(
        aurora::sha256_hex(std::string("")) ==
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "SHA-256 empty vector");
    expect(
        aurora::sha256_hex(std::string("abc")) ==
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "SHA-256 abc vector");
    expect(
        aurora::sha256_hex(std::string("The quick brown fox jumps over the lazy dog")) ==
            "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592",
        "SHA-256 quick brown fox vector");

    const auto dir = make_temp_dir("sha256-file");
    const auto file = dir / "abc.txt";
    aurora::write_text_file(file, "abc");
    expect(
        aurora::sha256_file(file) ==
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "SHA-256 file vector");
}

void test_adoptium_parsing() {
    const std::filesystem::path fixture =
        aurora::path_from_utf8(AURORA_TEST_SOURCE_DIR) / "tests" / "fixtures" /
        "adoptium_latest_21.json";
    const std::string text = aurora::read_text_file(fixture);
    const auto packages = aurora::parse_adoptium_packages(text);
    expect(packages.size() == 4, "Adoptium fixture parses valid packages");
    expect(
        aurora::adoptium_asset_url(21) ==
            "https://api.adoptium.net/v3/assets/latest/21/hotspot?architecture=x64&"
            "image_type=jdk&os=windows&vendor=eclipse",
        "Adoptium endpoint construction");
    expect(
        aurora::adoptium_asset_url(17, "https://mirror.invalid/api/") ==
            "https://mirror.invalid/api/17/hotspot?architecture=x64&image_type=jdk&"
            "os=windows&vendor=eclipse",
        "Adoptium endpoint override");

    const auto selected = aurora::select_adoptium_package(packages, 21);
    expect(selected.has_value(), "Adoptium package selected");
    if (selected) {
        expect(
            selected->name ==
                "OpenJDK21U-jdk_x64_windows_hotspot_21.0.1_12.zip",
            "JDK ZIP selected over JRE and debug image");
        expect(selected->release_name == "jdk-21.0.1+12", "release_name parsed");
        expect(selected->version == "21.0.1+12", "version parsed");
        expect(selected->major == 21, "major parsed");
        expect(selected->image_type == "jdk", "image type parsed");
        expect(selected->os == "windows", "OS parsed");
        expect(selected->arch == "x64", "architecture parsed");
        expect(selected->vendor == "eclipse", "vendor parsed");
        expect(selected->size == 205073461, "size parsed");
        expect(
            selected->checksum ==
                "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc",
            "checksum parsed");
        expect(selected->checksum_link.ends_with("jdk.zip.sha256.txt"), "checksum link parsed");
    }

    const std::string alternate = R"({
  "release_name": "jdk-17.0.10+7",
  "vendor": "eclipse",
  "version_data": { "semver": "17.0.10+7", "major": 17 },
  "binaries": [
    {
      "os": "windows",
      "architecture": "x64",
      "image_type": "jdk",
      "jvm_impl": "hotspot",
      "package": {
        "name": "OpenJDK17U-jdk_x64_windows_hotspot_17.0.10_7.zip",
        "link": "https://example.invalid/OpenJDK17U-jdk_x64_windows_hotspot_17.0.10_7.zip",
        "checksum": "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee",
        "size": 180000000
      }
    }
  ]
})";
    const auto alternate_selected =
        aurora::parse_and_select_adoptium_package(alternate, 17);
    expect(alternate_selected.has_value(), "alternate binaries shape parsed");
    if (alternate_selected) {
        expect(alternate_selected->release_name == "jdk-17.0.10+7", "alternate release name");
        expect(alternate_selected->major == 17, "alternate major");
        expect(alternate_selected->version == "17.0.10+7", "alternate version");
    }
    expect(
        !aurora::parse_and_select_adoptium_package("[]", 21).has_value(),
        "empty Adoptium response has no package");
    expect(
        throws_error([] { aurora::parse_adoptium_packages("not json"); }),
        "invalid Adoptium JSON rejected");
}

void test_paths() {
    const auto base = make_temp_dir("paths");
    const auto game = base / "game";
    aurora::ensure_directory(game);
    const auto inside = game / "runtime" / "java";

    expect(aurora::is_path_within(game, inside), "install path is inside game dir");
    expect(aurora::is_path_within(game, game), "root is within itself");
    expect(!aurora::is_path_within(inside, game), "parent is not inside child");
    expect(!aurora::is_path_within(game, base / "escape"), "sibling is outside");
    expect(
        !aurora::is_path_within(game, game / ".." / "escape"),
        "parent traversal is outside");
    expect(!aurora::is_path_within({}, game), "empty root rejected");
    expect(!aurora::is_path_within(game, {}), "empty candidate rejected");

    const auto install_dir = aurora::default_java_install_dir(game);
    expect(install_dir.filename() == "java", "default install dir leaf");
    expect(install_dir.parent_path().filename() == "runtime", "default install dir parent");
}

void test_probe() {
    const auto home = make_temp_dir("probe") / "jdk-21";
    write_fake_java_home(home);
    const auto runtime = aurora::probe_java_home(home, "downloaded");
    expect(runtime.has_value(), "fake Java home probes");
    if (runtime) {
        expect(runtime->home == aurora::absolute_path(home), "probe home path");
#ifdef _WIN32
        expect(runtime->executable.filename() == "java.exe", "probe java.exe path");
#else
        expect(runtime->executable.filename() == "java", "probe java path");
#endif
        expect(runtime->major == 21, "probe major");
        expect(runtime->version == "21.0.1", "probe version");
        expect(runtime->vendor == "Eclipse Adoptium", "probe vendor");
        expect(runtime->source == "downloaded", "probe source");
    }

    const auto no_release = make_temp_dir("probe-no-release") / "jdk";
    aurora::ensure_directory(no_release / "bin");
    aurora::write_text_file(no_release / "bin" / "java.exe", "fake java");
    aurora::write_text_file(no_release / "bin" / "java", "fake java");
    expect(!aurora::probe_java_home(no_release), "probe requires a release file");
}

void test_install_and_no_overwrite() {
    const auto game = make_temp_dir("install-game");
    const auto archive_dir = make_temp_dir("install-archive");
    const auto archive = archive_dir / "jdk-21.0.1+12.zip";
    const std::string release_name = "jdk-21.0.1+12";
    const std::string package_name =
        "OpenJDK21U-jdk_x64_windows_hotspot_21.0.1_12.zip";
    const std::string link = "https://example.invalid/" + package_name;

    write_zip(
        archive,
        {
            {release_name + "/bin/java.exe", "fake java"},
            {release_name + "/bin/java", "fake java"},
            {release_name + "/release",
             "JAVA_VERSION=\"21.0.1\"\nIMPLEMENTOR=\"Eclipse Adoptium\"\n"},
            {release_name + "/lib/modules", "fake modules"},
        });

    const std::string checksum = aurora::sha256_file(archive);
    const auto file_size = std::filesystem::file_size(archive);
    const std::string metadata =
        package_metadata(checksum, file_size, release_name, package_name, link);

    int fetches = 0;
    int downloads = 0;
    aurora::JavaInstallHooks hooks;
    hooks.fetch = [&](std::string_view) {
        ++fetches;
        return metadata;
    };
    hooks.download = [&](std::string_view, const std::filesystem::path& destination) {
        ++downloads;
        std::filesystem::copy_file(
            archive, destination, std::filesystem::copy_options::overwrite_existing);
    };

    aurora::JavaInstallOptions options;
    options.feature_version = 21;
    options.api_url = "https://api.adoptium.test/v3/assets/latest/21/hotspot";

    const auto runtime = aurora::install_java_runtime(game, options, hooks);
    const auto expected_home =
        aurora::absolute_path(game / "runtime" / "java" / release_name);
    expect(runtime.home == expected_home, "installed runtime home");
    expect(runtime.source == "downloaded", "installed runtime source");
    expect(runtime.major == 21, "installed runtime major");
    expect(runtime.version == "21.0.1", "installed runtime version");
    expect(runtime.vendor == "Eclipse Adoptium", "installed runtime vendor");
#ifdef _WIN32
    expect(runtime.executable.filename() == "java.exe", "installed java.exe path");
#else
    expect(runtime.executable.filename() == "java", "installed java path");
#endif
    expect(fetches == 1, "metadata fetched once");
    expect(downloads == 1, "archive downloaded once");

    aurora::write_text_file(runtime.home / "stale.txt", "keep");
    const auto cached = aurora::install_java_runtime(game, options, hooks);
    expect(cached.home == expected_home, "cached runtime home");
    expect(fetches == 1, "valid runtime avoids metadata request");
    expect(downloads == 1, "valid runtime avoids download");
    expect(
        std::filesystem::is_regular_file(runtime.home / "stale.txt"),
        "valid runtime is not overwritten");

    options.force = true;
    const auto reinstalled = aurora::install_java_runtime(game, options, hooks);
    expect(reinstalled.home == expected_home, "forced runtime home");
    expect(fetches == 2, "force fetches metadata again");
    expect(downloads == 2, "force downloads again");
    expect(
        !std::filesystem::exists(runtime.home / "stale.txt"),
        "force replaces the runtime directory");
    options.force = false;

    std::filesystem::remove(runtime.home / "release");
    expect(
        throws_error([&] { aurora::install_java_runtime(game, options, hooks); }),
        "invalid existing runtime requires force");
    expect(fetches == 3, "invalid existing runtime fetched metadata");
    expect(downloads == 2, "invalid existing runtime did not download");

    options.force = true;
    const auto repaired = aurora::install_java_runtime(game, options, hooks);
    expect(repaired.major == 21, "forced repair installs a valid runtime");
    expect(fetches == 4, "repair fetched metadata");
    expect(downloads == 3, "repair downloaded the archive");
    expect(
        std::filesystem::is_regular_file(repaired.home / "release"),
        "repair restored the release file");
}

void test_install_checksum_and_path_guards() {
    const auto game = make_temp_dir("guard-game");
    const auto archive_dir = make_temp_dir("guard-archive");
    const auto archive = archive_dir / "jdk.zip";
    const std::string release_name = "jdk-21.0.1+12";
    write_zip(
        archive,
        {
            {release_name + "/bin/java.exe", "fake java"},
            {release_name + "/bin/java", "fake java"},
            {release_name + "/release", "JAVA_VERSION=\"21.0.1\"\n"},
        });
    const auto file_size = std::filesystem::file_size(archive);
    const std::string archive_checksum = aurora::sha256_file(archive);
    const std::string package_name = "jdk.zip";
    const std::string link = "https://example.invalid/jdk.zip";

    int downloads = 0;
    aurora::JavaInstallHooks hooks;
    hooks.download = [&](std::string_view, const std::filesystem::path& destination) {
        ++downloads;
        std::filesystem::copy_file(
            archive, destination, std::filesystem::copy_options::overwrite_existing);
    };

    const std::string bad_checksum(64, '0');
    hooks.fetch = [&](std::string_view) {
        return package_metadata(bad_checksum, file_size, release_name, package_name, link);
    };
    aurora::JavaInstallOptions options;
    options.feature_version = 21;
    options.api_url = "https://api.adoptium.test/v3/assets/latest/21/hotspot";
    expect(
        throws_error([&] { aurora::install_java_runtime(game, options, hooks); }),
        "SHA-256 mismatch rejects the archive");
    expect(downloads == 1, "bad checksum still downloaded once");
    const auto install_root = game / "runtime" / "java";
    expect(
        !std::filesystem::exists(install_root / release_name),
        "bad checksum leaves no installed runtime");
    for (const auto& entry : std::filesystem::directory_iterator(install_root)) {
        expect(
            entry.path().filename().string().rfind(".staging-", 0) != 0,
            "staging directory cleaned after checksum failure");
    }

    aurora::Json fallback_root = aurora::parse_json(
        package_metadata("", file_size, release_name, package_name, link),
        "checksum fallback fixture");
    fallback_root[0]["binary"]["package"]["checksum_link"] =
        "https://example.invalid/jdk.zip.sha256.txt";
    const std::string fallback_metadata = fallback_root.dump();
    int fallback_fetches = 0;
    int fallback_downloads = 0;
    hooks.fetch = [&](std::string_view url) {
        ++fallback_fetches;
        if (url == "https://example.invalid/jdk.zip.sha256.txt") {
            return archive_checksum + "  jdk.zip\n";
        }
        return fallback_metadata;
    };
    hooks.download = [&](std::string_view, const std::filesystem::path& destination) {
        ++fallback_downloads;
        std::filesystem::copy_file(
            archive, destination, std::filesystem::copy_options::overwrite_existing);
    };
    const auto fallback_game = make_temp_dir("guard-checksum-link");
    const auto fallback_runtime =
        aurora::install_java_runtime(fallback_game, options, hooks);
    expect(fallback_runtime.major == 21, "checksum link fallback installs");
    expect(fallback_fetches == 2, "checksum link fallback fetched metadata and checksum");
    expect(fallback_downloads == 1, "checksum link fallback downloaded once");

    const auto outside = make_temp_dir("guard-outside");
    int fetches = 0;
    hooks.fetch = [&](std::string_view) {
        ++fetches;
        return std::string("[]");
    };
    aurora::JavaInstallOptions outside_options = options;
    outside_options.install_dir = outside;
    expect(
        throws_error([&] { aurora::install_java_runtime(game, outside_options, hooks); }),
        "install directory outside game dir rejected");
    expect(fetches == 0, "outside install dir rejected before network");
    expect(std::filesystem::is_empty(outside), "outside install dir untouched");

    const std::string malicious = package_metadata(
        std::string(64, 'c'), file_size, "../escape", package_name, link);
    int malicious_downloads = 0;
    hooks.fetch = [&](std::string_view) { return malicious; };
    hooks.download = [&](std::string_view, const std::filesystem::path&) {
        ++malicious_downloads;
    };
    const auto malicious_game = make_temp_dir("guard-malicious");
    expect(
        throws_error([&] {
            aurora::install_java_runtime(malicious_game, options, hooks);
        }),
        "unsafe release name rejected");
    expect(malicious_downloads == 0, "unsafe release name rejected before download");
    expect(
        !std::filesystem::exists(malicious_game / "runtime" / "escape"),
        "unsafe release name does not escape install root");

    const auto zip_slip_archive = archive_dir / "zip-slip.zip";
    write_zip(
        zip_slip_archive,
        {
            {release_name + "/bin/java.exe", "fake java"},
            {release_name + "/bin/java", "fake java"},
            {release_name + "/release", "JAVA_VERSION=\"21.0.1\"\n"},
            {"../escaped-java.txt", "escape"},
        });
    const auto zip_slip_metadata = package_metadata(
        aurora::sha256_file(zip_slip_archive),
        std::filesystem::file_size(zip_slip_archive),
        release_name,
        "zip-slip.zip",
        "https://example.invalid/zip-slip.zip");
    const auto zip_slip_game = make_temp_dir("guard-zip-slip");
    hooks.fetch = [&](std::string_view) { return zip_slip_metadata; };
    int zip_slip_downloads = 0;
    hooks.download = [&](std::string_view, const std::filesystem::path& destination) {
        ++zip_slip_downloads;
        std::filesystem::copy_file(
            zip_slip_archive, destination,
            std::filesystem::copy_options::overwrite_existing);
    };
    expect(
        throws_error([&] {
            aurora::install_java_runtime(zip_slip_game, options, hooks);
        }),
        "ZIP entry traversal rejected");
    expect(zip_slip_downloads == 1, "ZIP traversal archive downloaded once");
    expect(
        !std::filesystem::exists(zip_slip_game / "runtime" / "java" / release_name),
        "ZIP traversal leaves no installed runtime");
    for (const auto& entry :
         std::filesystem::directory_iterator(zip_slip_game / "runtime" / "java")) {
        expect(
            entry.path().filename().string().rfind(".staging-", 0) != 0,
            "staging directory cleaned after ZIP traversal failure");
    }
}

void test_detection_integration() {
    const auto game = make_temp_dir("detect-game");
    const auto home = game / "runtime" / "java" / "jdk-21.0.1+12";
    write_fake_java_home(home);

    const auto runtimes = aurora::detect_java_runtimes(game);
    const auto expected_home = aurora::absolute_path(home);
    const bool found = std::any_of(
        runtimes.begin(), runtimes.end(), [&](const aurora::JavaRuntime& runtime) {
            return runtime.source == "managed" && runtime.home == expected_home;
        });
    expect(found, "managed runtime appears in detection");
}

} // namespace

int main() {
    test_sha256();
    test_adoptium_parsing();
    test_paths();
    test_probe();
    test_install_and_no_overwrite();
    test_install_checksum_and_path_guards();
    test_detection_integration();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All tests passed\n";
    return 0;
}
