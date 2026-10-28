#include "core/hash.hpp"
#include "core/http.hpp"
#include "core/mirrors.hpp"
#include "core/paths.hpp"
#include "core/proxy.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
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
                      ("aurora-http-tests-" + name + "-" + std::to_string(stamp));
    std::filesystem::create_directories(path);
    return path;
}

std::chrono::milliseconds ms(int value) {
    return std::chrono::milliseconds(value);
}

void test_system_proxy_environment() {
#ifdef _WIN32
    _putenv_s("HTTPS_PROXY", "http://127.0.0.1:31181");
    _putenv_s("HTTP_PROXY", "http://127.0.0.1:31182");
    _putenv_s("ALL_PROXY", "");
    _putenv_s("NO_PROXY", "localhost, .example.com");

    const auto https_proxy = aurora::system_proxy_for_url("https://example.org/data");
    expect(https_proxy.has_value(), "HTTPS proxy is read from the environment");
    expect(
        https_proxy && https_proxy->server == "127.0.0.1:31181",
        "HTTPS proxy server is normalized");
    expect(
        https_proxy && https_proxy->bypass == "localhost;.example.com",
        "NO_PROXY is converted to a bypass list");

    const auto http_proxy = aurora::system_proxy_for_url("http://example.org/data");
    expect(http_proxy.has_value(), "HTTP proxy is read from the environment");
    expect(
        http_proxy && http_proxy->server == "127.0.0.1:31182",
        "HTTP proxy uses HTTP_PROXY");

    _putenv_s("HTTPS_PROXY", "");
    _putenv_s("HTTP_PROXY", "");
    _putenv_s("NO_PROXY", "");
#endif
}

void test_retry_classification() {
    using aurora::HttpFailureSource;
    using aurora::HttpRetryClass;

    const auto status_class = [](int status) {
        return aurora::classify_http_failure(HttpFailureSource::HttpStatus, status);
    };

    expect(
        aurora::classify_http_failure(HttpFailureSource::Transport, 0) ==
            HttpRetryClass::Retryable,
        "transport failures are retryable");
    expect(
        aurora::classify_http_failure(HttpFailureSource::Transport, 404) ==
            HttpRetryClass::Retryable,
        "transport classification ignores a stale status");

    for (const int status : {408, 429, 500, 502, 503, 504, 599}) {
        expect(
            status_class(status) == HttpRetryClass::Retryable,
            "transient HTTP status is retryable: " + std::to_string(status));
    }
    for (const int status : {400, 401, 403, 404, 410, 416, 301}) {
        expect(
            status_class(status) == HttpRetryClass::NonRetryable,
            "non-transient HTTP status is not retryable: " + std::to_string(status));
    }
    for (const int status : {200, 204, 206, 299}) {
        expect(
            status_class(status) == HttpRetryClass::Success,
            "success HTTP status is not a failure: " + std::to_string(status));
    }

    expect(
        aurora::classify_http_status(408) == HttpRetryClass::Retryable,
        "classify_http_status matches 408");
    expect(
        aurora::classify_http_status(404) == HttpRetryClass::NonRetryable,
        "classify_http_status matches 404");
    expect(
        aurora::classify_http_status(200) == HttpRetryClass::Success,
        "classify_http_status matches 200");
}

void test_backoff() {
    expect(aurora::compute_retry_backoff(0, ms(500), ms(8000)) == ms(500), "backoff attempt 0");
    expect(aurora::compute_retry_backoff(1, ms(500), ms(8000)) == ms(1000), "backoff attempt 1");
    expect(aurora::compute_retry_backoff(2, ms(500), ms(8000)) == ms(2000), "backoff attempt 2");
    expect(aurora::compute_retry_backoff(3, ms(500), ms(8000)) == ms(4000), "backoff attempt 3");
    expect(aurora::compute_retry_backoff(4, ms(500), ms(8000)) == ms(8000), "backoff cap");
    expect(aurora::compute_retry_backoff(100, ms(500), ms(8000)) == ms(8000), "backoff stays capped");
    expect(aurora::compute_retry_backoff(0, ms(9000), ms(8000)) == ms(8000), "backoff initial above cap");
    expect(aurora::compute_retry_backoff(-2, ms(500), ms(8000)) == ms(500), "backoff negative index");
    expect(aurora::compute_retry_backoff(5, ms(0), ms(8000)) == ms(0), "backoff zero initial");
    expect(aurora::compute_retry_backoff(5, ms(500), ms(0)) == ms(0), "backoff zero cap");
    expect(
        aurora::compute_retry_backoff(1000000, ms(500), ms(8000)) == ms(8000),
        "backoff remains bounded for huge retry indexes");
}

void test_retry_after() {
    expect(aurora::parse_retry_after("120") == std::optional(ms(120000)), "Retry-After seconds");
    expect(aurora::parse_retry_after(" 5 ") == std::optional(ms(5000)), "Retry-After whitespace");
    expect(aurora::parse_retry_after("0") == std::optional(ms(0)), "Retry-After zero");
    expect(!aurora::parse_retry_after("-1").has_value(), "Retry-After rejects negative");
    expect(!aurora::parse_retry_after("").has_value(), "Retry-After rejects empty");
    expect(
        !aurora::parse_retry_after("Wed, 21 Oct 2015 07:28:00 GMT").has_value(),
        "Retry-After rejects HTTP dates");

    aurora::HttpDownloadOptions options;
    options.initial_backoff = ms(500);
    options.max_backoff = ms(8000);
    options.max_retry_after = ms(30000);
    expect(aurora::compute_retry_delay(0, options) == ms(500), "retry delay uses backoff");
    expect(aurora::compute_retry_delay(2, options) == ms(2000), "retry delay grows");
    expect(
        aurora::compute_retry_delay(0, options, ms(1200)) == ms(1200),
        "retry delay honors Retry-After");
    expect(
        aurora::compute_retry_delay(0, options, ms(60000)) == ms(30000),
        "retry delay caps Retry-After");
    expect(
        aurora::compute_retry_delay(0, options, ms(0)) == ms(0),
        "retry delay honors zero Retry-After");
}

void test_mirror_ordering() {
    const std::vector<std::string> sources = aurora::build_download_sources(
        " https://primary.example/a.jar ",
        {
            "https://mirror-one.example/a.jar",
            "",
            " https://mirror-two.example/a.jar ",
            "https://mirror-one.example/a.jar",
            "https://primary.example/a.jar",
        });

    expect(sources.size() == 3, "mirror list deduplicates and drops empties");
    if (sources.size() == 3) {
        expect(sources[0] == "https://primary.example/a.jar", "primary URL is first");
        expect(
            sources[1] == "https://mirror-one.example/a.jar",
            "first mirror keeps its order");
        expect(
            sources[2] == "https://mirror-two.example/a.jar",
            "second mirror keeps its order");
    }

    expect(aurora::build_download_sources("", {}).empty(), "no sources for empty primary");
    const auto only_mirror =
        aurora::build_download_sources("", {"https://mirror.example/a.jar"});
    expect(
        only_mirror.size() == 1 && only_mirror[0] == "https://mirror.example/a.jar",
        "mirror can be the first usable source");
}

void test_builtin_mirror_mapping() {
    using aurora::builtin_mirror_url;

    expect(
        builtin_mirror_url(
            "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json") ==
            "https://bmclapi2.bangbang93.com/mc/game/version_manifest_v2.json",
        "piston-meta URL maps to BMCLAPI");
    expect(
        builtin_mirror_url(
            "https://piston-data.mojang.com/v1/objects/abc/client.jar") ==
            "https://bmclapi2.bangbang93.com/v1/objects/abc/client.jar",
        "piston-data URL maps to BMCLAPI");
    expect(
        builtin_mirror_url(
            "https://libraries.minecraft.net/com/google/gson/2.10.1/gson-2.10.1.jar") ==
            "https://bmclapi2.bangbang93.com/maven/com/google/gson/2.10.1/gson-2.10.1.jar",
        "library URLs map to the BMCLAPI Maven root");
    expect(
        builtin_mirror_url("https://resources.download.minecraft.net/ab/abcdef") ==
            "https://bmclapi2.bangbang93.com/assets/ab/abcdef",
        "asset objects map to the BMCLAPI asset root");
    expect(
        builtin_mirror_url("https://meta.fabricmc.net/v2/versions/loader/1.21.1?x=1") ==
            "https://bmclapi2.bangbang93.com/fabric-meta/v2/versions/loader/1.21.1?x=1",
        "Fabric metadata maps to fabric-meta and keeps the query string");
    expect(
        builtin_mirror_url(
            "https://maven.fabricmc.net/net/fabricmc/fabric-loader/0.16.10/"
            "fabric-loader-0.16.10.jar") ==
            "https://bmclapi2.bangbang93.com/maven/net/fabricmc/fabric-loader/0.16.10/"
            "fabric-loader-0.16.10.jar",
        "Fabric Maven URLs map to the BMCLAPI Maven root");
    expect(
        builtin_mirror_url(
            "https://maven.minecraftforge.net/net/minecraftforge/forge/1.21.1-52.0.1/"
            "forge-1.21.1-52.0.1-installer.jar") ==
            "https://bmclapi2.bangbang93.com/maven/net/minecraftforge/forge/1.21.1-52.0.1/"
            "forge-1.21.1-52.0.1-installer.jar",
        "Forge Maven URLs map to the BMCLAPI Maven root");
    expect(
        builtin_mirror_url(
            "https://maven.neoforged.net/releases/net/neoforged/neoforge/21.1.80/"
            "neoforge-21.1.80-installer.jar") ==
            "https://bmclapi2.bangbang93.com/maven/net/neoforged/neoforge/21.1.80/"
            "neoforge-21.1.80-installer.jar",
        "NeoForge release URLs map to the BMCLAPI Maven root");
    expect(
        builtin_mirror_url(
            "https://files.minecraftforge.net/maven/net/minecraftforge/forge/"
            "1.12.2-14.23.5.2860/forge-1.12.2-14.23.5.2860-universal.jar") ==
            "https://bmclapi2.bangbang93.com/maven/net/minecraftforge/forge/"
            "1.12.2-14.23.5.2860/forge-1.12.2-14.23.5.2860-universal.jar",
        "legacy Forge Maven URLs map to the BMCLAPI Maven root");
    expect(
        builtin_mirror_url("https://api.modrinth.com/v2/search?query=sodium") ==
            "https://mod.mcimirror.top/modrinth/v2/search?query=sodium",
        "Modrinth API URLs map to MCIM and keep the query string");
    expect(
        builtin_mirror_url(
            "https://cdn.modrinth.com/data/AANobbMI/versions/SMxNOGZ6/"
            "sodium-fabric-0.8.13%2Bmc1.21.1.jar") ==
            "https://mod.mcimirror.top/data/AANobbMI/versions/SMxNOGZ6/"
            "sodium-fabric-0.8.13%2Bmc1.21.1.jar",
        "Modrinth CDN URLs map to the MCIM alternate route");

    expect(
        builtin_mirror_url("https://meta.quiltmc.org/v3/versions/loader/1.21.1").empty(),
        "Quilt metadata has no built-in mirror");
    expect(
        builtin_mirror_url("https://api.minecraftservices.com/minecraft/profile").empty(),
        "Minecraft services are never mirrored");
    expect(
        builtin_mirror_url(
            "https://login.microsoftonline.com/consumers/oauth2/v2.0/token")
            .empty(),
        "Microsoft login is never mirrored");
    expect(
        builtin_mirror_url("https://api.adoptium.net/v3/assets/latest/21/hotspot").empty(),
        "Adoptium metadata has no built-in mirror");
    expect(
        builtin_mirror_url(
            "https://maven.neoforged.net/api/maven/versions/releases/net/neoforged/"
            "neoforge")
            .empty(),
        "NeoForge metadata API is not treated as a Maven artifact");
    expect(
        builtin_mirror_url(
            "https://files.minecraftforge.net/net/minecraftforge/forge/"
            "promotions_slim.json")
            .empty(),
        "Forge promotions use a different mirror schema and stay official");
    expect(
        builtin_mirror_url("http://libraries.minecraft.net/x").empty(),
        "plain HTTP URLs are never rewritten");
    expect(
        builtin_mirror_url("https://libraries.minecraft.net.evil.example/x").empty(),
        "lookalike hosts are not mirrored");
    expect(
        builtin_mirror_url("https://libraries.minecraft.net@evil.example/x").empty(),
        "userinfo hosts are not mirrored");
}

void test_download_candidates() {
    using aurora::download_url_candidates;
    using aurora::MirrorProvider;

    aurora::reset_builtin_mirror_health();
    expect(
        aurora::builtin_mirror_health(MirrorProvider::BmclApi) ==
            aurora::BuiltinMirrorHealth::Unknown,
        "mirror health starts unknown");
    expect(
        aurora::builtin_mirror_provider(
            "https://mod.mcimirror.top/modrinth/v2/search") ==
            MirrorProvider::McimApi,
        "MCIM Modrinth API routes use the API health slot");
    expect(
        aurora::builtin_mirror_provider(
            "https://mod.mcimirror.top/data/AANobbMI/versions/x/file.jar") ==
            MirrorProvider::McimCdn,
        "MCIM Modrinth data routes use the CDN health slot");
    expect(
        aurora::builtin_mirror_provider("https://mod.mcimirror.top/other") ==
            MirrorProvider::NoProvider,
        "unknown MCIM routes are not treated as mirrors");
    const std::string official =
        "https://libraries.minecraft.net/com/google/gson/2.10.1/gson-2.10.1.jar";
    const std::string mirror =
        "https://bmclapi2.bangbang93.com/maven/com/google/gson/2.10.1/gson-2.10.1.jar";

    auto candidates = download_url_candidates(official);
    expect(
        candidates.size() == 2 && candidates[0] == mirror && candidates[1] == official,
        "built-in mirror comes before the official URL");

    candidates = download_url_candidates(
        official, {" https://mirror.example/gson.jar ", "", mirror});
    expect(
        candidates.size() == 3 &&
            candidates[0] == mirror &&
            candidates[1] == "https://mirror.example/gson.jar" &&
            candidates[2] == official,
        "extra mirrors sit between the built-in mirror and the official URL");

    candidates = download_url_candidates(mirror);
    expect(
        candidates.size() == 1 && candidates[0] == mirror,
        "already mirrored URLs are not rewritten twice");

    const std::string auth = "https://api.minecraftservices.com/minecraft/profile";
    candidates = download_url_candidates(auth);
    expect(
        candidates.size() == 1 && candidates[0] == auth,
        "auth endpoints have a single candidate");

    candidates = download_url_candidates("", {"https://mirror.example/a.jar"});
    expect(
        candidates.size() == 1 && candidates[0] == "https://mirror.example/a.jar",
        "an empty primary keeps caller-supplied mirrors");

    aurora::report_builtin_mirror_success(official);
    expect(
        aurora::builtin_mirror_health(MirrorProvider::BmclApi) == aurora::BuiltinMirrorHealth::Unknown,
        "non-mirror successes do not change mirror health");
    aurora::report_builtin_mirror_success(mirror);
    expect(
        aurora::builtin_mirror_health(MirrorProvider::BmclApi) == aurora::BuiltinMirrorHealth::Available,
        "mirror success marks the mirror available");

    aurora::report_download_source_failure(mirror);
    expect(
        aurora::builtin_mirror_health(MirrorProvider::BmclApi) == aurora::BuiltinMirrorHealth::Failed,
        "mirror failure marks the mirror failed");
    candidates = download_url_candidates(official);
    expect(
        candidates.size() == 1 && candidates[0] == official,
        "a failed built-in mirror is skipped for the rest of the process");

    aurora::report_builtin_mirror_success(mirror);
    expect(
        aurora::builtin_mirror_health(MirrorProvider::BmclApi) == aurora::BuiltinMirrorHealth::Failed,
        "a failed mirror stays failed for the rest of the process");

    candidates = download_url_candidates(official, {"https://mirror.example/gson.jar"});
    expect(
        candidates.size() == 2 &&
            candidates[0] == "https://mirror.example/gson.jar" &&
            candidates[1] == official,
        "caller-supplied mirrors still work after the built-in mirror fails");

    const std::string modrinth_official =
        "https://api.modrinth.com/v2/search?query=sodium";
    const std::string modrinth_mirror =
        "https://mod.mcimirror.top/modrinth/v2/search?query=sodium";
    candidates = download_url_candidates(modrinth_official);
    expect(
        candidates.size() == 2 && candidates[0] == modrinth_mirror,
        "MCIM API mirror works while BMCL is failed");
    aurora::report_download_source_failure(modrinth_mirror);
    expect(
        aurora::builtin_mirror_health(MirrorProvider::McimApi) ==
            aurora::BuiltinMirrorHealth::Failed,
        "MCIM API failure is recorded separately");

    aurora::reset_builtin_mirror_health();
    aurora::report_builtin_mirror_success(modrinth_mirror);
    expect(
        aurora::builtin_mirror_health(MirrorProvider::McimApi) ==
            aurora::BuiltinMirrorHealth::Available,
        "MCIM API success marks only its own slot");
    expect(
        aurora::builtin_mirror_health(MirrorProvider::McimCdn) ==
            aurora::BuiltinMirrorHealth::Unknown,
        "MCIM API success does not mark the CDN slot");
    expect(
        aurora::builtin_mirror_health(MirrorProvider::BmclApi) ==
            aurora::BuiltinMirrorHealth::Unknown,
        "MCIM success does not mark BMCL");
    aurora::report_download_source_failure(
        "https://mod.mcimirror.top/data/AANobbMI/versions/x/file.jar");
    expect(
        aurora::builtin_mirror_health(MirrorProvider::McimCdn) ==
            aurora::BuiltinMirrorHealth::Failed,
        "MCIM CDN failure is recorded separately");
    candidates = download_url_candidates(modrinth_official);
    expect(
        candidates.size() == 2 && candidates[0] == modrinth_mirror,
        "MCIM CDN failure does not disable the API mirror");

    aurora::reset_builtin_mirror_health();
    aurora::report_download_source_failure(official);
    candidates = download_url_candidates(official);
    expect(
        candidates.size() == 2 && candidates[0] == mirror,
        "official failures do not disable the built-in mirror");
    aurora::reset_builtin_mirror_health();
}

void test_range_header() {
    expect(aurora::format_range_header(0) == "bytes=0-", "Range offset 0");
    expect(aurora::format_range_header(1234) == "bytes=1234-", "Range offset 1234");
    expect(
        aurora::format_range_header(std::numeric_limits<std::uint64_t>::max()) ==
            "bytes=18446744073709551615-",
        "Range offset max uint64");
}

void test_content_range() {
    const auto range = aurora::parse_content_range("bytes 1024-2047/4096");
    expect(range.has_value(), "parse Content-Range");
    if (range) {
        expect(range->first == 1024, "Content-Range first");
        expect(range->last == 2047, "Content-Range last");
        expect(range->total == std::optional<std::uint64_t>(4096), "Content-Range total");
        expect(aurora::content_range_matches(*range, 1024, 1024), "Content-Range matches");
        expect(!aurora::content_range_matches(*range, 0, 1024), "Content-Range wrong offset");
        expect(
            !aurora::content_range_matches(*range, 1024, 1023),
            "Content-Range wrong body length");
        expect(
            aurora::content_range_matches(*range, 1024, 0),
            "Content-Range accepts unknown body length");
    }

    const auto open_ended = aurora::parse_content_range(" bytes 0-99/* ");
    expect(open_ended.has_value(), "parse Content-Range with unknown total");
    if (open_ended) {
        expect(!open_ended->total.has_value(), "unknown total stays unknown");
        expect(aurora::content_range_matches(*open_ended, 0, 100), "open Content-Range matches");
    }

    expect(
        aurora::parse_content_range("BYTES 0-99/100").has_value(),
        "Content-Range unit is case insensitive");
    expect(!aurora::parse_content_range("items 0-99/100").has_value(), "Content-Range unit");
    expect(!aurora::parse_content_range("bytes 99-0/100").has_value(), "Content-Range order");
    expect(!aurora::parse_content_range("bytes 0-99/99").has_value(), "Content-Range total range");
    expect(!aurora::parse_content_range("bytes 0-99/100x").has_value(), "Content-Range junk");
    expect(!aurora::parse_content_range("bytes 0-99").has_value(), "Content-Range missing total");
    expect(!aurora::parse_content_range("bytes */100").has_value(), "Content-Range unsatisfied");
    expect(!aurora::parse_content_range("").has_value(), "Content-Range empty");
}

void test_checksum_policy() {
    const std::string expected = "a9993e364706816aba3e25717850c26c9cd0d89d";
    expect(aurora::checksum_matches(expected, expected), "SHA-1 exact match");
    expect(
        aurora::checksum_matches("A9993E364706816ABA3E25717850C26C9CD0D89D", expected),
        "SHA-1 is case insensitive");
    expect(
        aurora::checksum_matches(" " + expected + " ", expected),
        "SHA-1 ignores surrounding whitespace");
    expect(
        !aurora::checksum_matches(expected, "b9993e364706816aba3e25717850c26c9cd0d89d"),
        "SHA-1 mismatch");
    expect(aurora::checksum_matches("", expected), "missing expected checksum is best effort");

    const auto directory = make_temp_dir("sha1");
    const auto file = directory / "payload.bin";
    aurora::write_text_file(file, "abc");
    expect(
        aurora::checksum_matches(expected, aurora::sha1_file(file)),
        "SHA-1 file success");
    aurora::write_text_file(file, "abd");
    expect(
        !aurora::checksum_matches(expected, aurora::sha1_file(file)),
        "SHA-1 file mismatch");
    std::error_code error;
    std::filesystem::remove_all(directory, error);
}

} // namespace

int main() {
    test_system_proxy_environment();
    test_retry_classification();
    test_backoff();
    test_retry_after();
    test_mirror_ordering();
    test_builtin_mirror_mapping();
    test_download_candidates();
    test_range_header();
    test_content_range();
    test_checksum_policy();

    if (failures != 0) {
        std::cerr << failures << " http test(s) failed\n";
        return 1;
    }
    std::cout << "All HTTP tests passed\n";
    return 0;
}
