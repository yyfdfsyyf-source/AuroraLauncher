#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace aurora {

enum class MirrorProvider {
    NoProvider,
    BmclApi,
    McimApi,
    McimCdn,
};

struct BuiltinMirror {
    MirrorProvider provider = MirrorProvider::NoProvider;
    std::string url;
};

// Returns the built-in mirror URL for a supported official URL, or an empty
// string. Authentication, entitlement and other API endpoints are never
// rewritten, and neither are hosts without a known mirror.
std::string builtin_mirror_url(std::string_view url);

BuiltinMirror builtin_mirror(std::string_view url);

// True for URLs served by a known built-in mirror route.
bool is_builtin_mirror_url(std::string_view url);

MirrorProvider builtin_mirror_provider(std::string_view url);
std::string_view mirror_provider_name(MirrorProvider provider);

enum class BuiltinMirrorHealth {
    Unknown,
    Available,
    Failed,
};

BuiltinMirrorHealth builtin_mirror_health(MirrorProvider provider);

// Marks the built-in mirror as usable. A previously failed mirror stays failed
// for the rest of the process.
void report_builtin_mirror_success(std::string_view url);

// Ordered download candidates: built-in mirror, caller-supplied mirrors, then
// the original URL. The built-in mirror is skipped once this process has seen
// it fail. The original URL is always last so callers retain an official
// fallback; an empty URL yields only the caller-supplied mirrors.
std::vector<std::string> download_url_candidates(
    std::string_view url,
    const std::vector<std::string>& extra_mirrors = {});

// Disables the built-in mirror for the rest of this process when url belongs
// to it. Failures of official or caller-supplied URLs are ignored.
void report_download_source_failure(std::string_view url);

// Clears the process-wide mirror health state. Intended for tests.
void reset_builtin_mirror_health();

} // namespace aurora
