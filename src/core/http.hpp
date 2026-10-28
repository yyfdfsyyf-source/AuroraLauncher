#pragma once

#include "core/error.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace aurora {

// Thrown by http_get after every candidate returned the same non-retryable
// HTTP status (for example 404 for an unknown API resource).
class HttpStatusError : public Error {
public:
    HttpStatusError(int status, const std::string& message)
        : Error(message), status_(status) {}

    int status() const {
        return status_;
    }

private:
    int status_;
};

using ProgressCallback = std::function<void(std::uint64_t received, std::uint64_t total)>;
using CancelCallback = std::function<bool()>;

struct HttpMetadata {
    int status = 0;
    std::uint64_t content_length = 0;
};

struct HttpDownloadOptions {
    // Additional attempts per source after the first one. Zero disables retries.
    int max_retries = 3;
    std::chrono::milliseconds initial_backoff{500};
    std::chrono::milliseconds max_backoff{8000};
    // Upper bound for a server-provided Retry-After value.
    std::chrono::milliseconds max_retry_after{30000};
    // Resume an existing .part file with a Range request when possible.
    bool resumable = true;
    // Ordered fallback URLs. They are tried after the built-in mirror and
    // before the primary URL, and are never hardcoded by the downloader.
    std::vector<std::string> mirrors;
    std::chrono::milliseconds resolve_timeout{30000};
    std::chrono::milliseconds connect_timeout{30000};
    std::chrono::milliseconds send_timeout{30000};
    std::chrono::milliseconds receive_timeout{60000};
    // Polled between attempts and while reading a response. Returning true
    // aborts the download; an existing .part file is kept for a later resume.
    CancelCallback cancelled;
};

enum class HttpFailureSource {
    Transport,
    HttpStatus,
};

enum class HttpRetryClass {
    Success,
    Retryable,
    NonRetryable,
};

// Transport failures are transient. HTTP 408, 429, and 5xx are transient;
// all other HTTP failures must not be retried against the same URL.
HttpRetryClass classify_http_failure(HttpFailureSource source, int status);
HttpRetryClass classify_http_status(int status);

// Deterministic capped exponential backoff: initial * 2^retry_index.
std::chrono::milliseconds compute_retry_backoff(
    int retry_index,
    std::chrono::milliseconds initial,
    std::chrono::milliseconds maximum);

// Parses delta-seconds Retry-After values. HTTP-date values are not handled
// here and return nullopt, in which case normal backoff is used.
std::optional<std::chrono::milliseconds> parse_retry_after(std::string_view value);

// Uses Retry-After when present (capped by max_retry_after), otherwise backoff.
std::chrono::milliseconds compute_retry_delay(
    int retry_index,
    const HttpDownloadOptions& options,
    std::optional<std::chrono::milliseconds> retry_after = std::nullopt);

// Builds the ordered source list: primary first, then mirrors. Empty entries
// are skipped and duplicate URLs are removed.
std::vector<std::string> build_download_sources(
    std::string_view primary_url,
    const std::vector<std::string>& mirrors);

std::string format_range_header(std::uint64_t offset);

struct HttpContentRange {
    std::uint64_t first = 0;
    std::uint64_t last = 0;
    std::optional<std::uint64_t> total;
};

std::optional<HttpContentRange> parse_content_range(std::string_view value);

// Validates that a Content-Range starts at offset, that the range is sane, and
// (when content_length is non-zero) that it describes the response body.
bool content_range_matches(
    const HttpContentRange& range,
    std::uint64_t offset,
    std::uint64_t content_length);

// Empty expected hashes are treated as "no checksum available".
bool checksum_matches(std::string_view expected_sha1, std::string_view actual_sha1);

HttpMetadata http_request(std::string_view url,
                          const std::function<void(const void*, std::size_t)>& sink,
                          const ProgressCallback& progress = {});

struct HttpGetOptions {
    int max_retries = 3;
    std::chrono::milliseconds resolve_timeout{30000};
    std::chrono::milliseconds connect_timeout{30000};
    std::chrono::milliseconds send_timeout{30000};
    std::chrono::milliseconds receive_timeout{60000};
};

std::string http_get(std::string_view url);
std::string http_get(std::string_view url, const HttpGetOptions& options);

void http_download(std::string_view url,
                   const std::filesystem::path& destination,
                   std::string_view expected_sha1 = {},
                   const ProgressCallback& progress = {});

void http_download(std::string_view url,
                   const std::filesystem::path& destination,
                   const HttpDownloadOptions& options,
                   std::string_view expected_sha1 = {},
                   const ProgressCallback& progress = {});

} // namespace aurora
