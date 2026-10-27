#include "core/http.hpp"

#include "core/branding.hpp"
#include "core/error.hpp"
#include "core/hash.hpp"
#include "core/log.hpp"
#include "core/paths.hpp"
#include "core/proxy.hpp"
#include "core/strings.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <fstream>
#include <limits>
#include <thread>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#endif

namespace aurora {

namespace {

bool parse_unsigned(std::string_view text, std::uint64_t& value) {
    if (text.empty()) {
        return false;
    }
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto [position, error] = std::from_chars(begin, end, value);
    return error == std::errc{} && position == end;
}

} // namespace

HttpRetryClass classify_http_status(int status) {
    if (status >= 200 && status < 300) {
        return HttpRetryClass::Success;
    }
    if (status == 408 || status == 429 || (status >= 500 && status < 600)) {
        return HttpRetryClass::Retryable;
    }
    return HttpRetryClass::NonRetryable;
}

HttpRetryClass classify_http_failure(HttpFailureSource source, int status) {
    if (source == HttpFailureSource::Transport) {
        return HttpRetryClass::Retryable;
    }
    return classify_http_status(status);
}

std::chrono::milliseconds compute_retry_backoff(
    int retry_index,
    std::chrono::milliseconds initial,
    std::chrono::milliseconds maximum) {
    if (retry_index < 0) {
        retry_index = 0;
    }
    if (initial < std::chrono::milliseconds::zero()) {
        initial = std::chrono::milliseconds::zero();
    }
    if (maximum < std::chrono::milliseconds::zero()) {
        maximum = std::chrono::milliseconds::zero();
    }

    auto delay = initial;
    if (delay >= maximum) {
        return maximum;
    }
    for (int index = 0; index < retry_index && delay > std::chrono::milliseconds::zero();
         ++index) {
        if (delay > maximum - delay) {
            return maximum;
        }
        delay += delay;
    }
    return std::min(delay, maximum);
}

std::optional<std::chrono::milliseconds> parse_retry_after(std::string_view value) {
    const std::string text = trim(value);
    std::uint64_t seconds = 0;
    if (!parse_unsigned(text, seconds)) {
        return std::nullopt;
    }

    constexpr auto max_milliseconds = std::chrono::milliseconds::max().count();
    const auto max_seconds = static_cast<std::uint64_t>(max_milliseconds / 1000);
    if (seconds > max_seconds) {
        return std::chrono::milliseconds::max();
    }
    return std::chrono::milliseconds(
        static_cast<std::chrono::milliseconds::rep>(seconds) * 1000);
}

std::chrono::milliseconds compute_retry_delay(
    int retry_index,
    const HttpDownloadOptions& options,
    std::optional<std::chrono::milliseconds> retry_after) {
    auto delay = compute_retry_backoff(
        retry_index, options.initial_backoff, options.max_backoff);
    if (retry_after) {
        auto server_delay = *retry_after;
        if (server_delay < std::chrono::milliseconds::zero()) {
            server_delay = std::chrono::milliseconds::zero();
        }
        auto maximum = options.max_retry_after;
        if (maximum < std::chrono::milliseconds::zero()) {
            maximum = std::chrono::milliseconds::zero();
        }
        delay = std::min(server_delay, maximum);
    }
    return delay;
}

std::vector<std::string> build_download_sources(
    std::string_view primary_url,
    const std::vector<std::string>& mirrors) {
    std::vector<std::string> sources;
    const auto add = [&sources](std::string_view value) {
        std::string candidate = trim(value);
        if (candidate.empty()) {
            return;
        }
        if (std::find(sources.begin(), sources.end(), candidate) == sources.end()) {
            sources.push_back(std::move(candidate));
        }
    };

    add(primary_url);
    for (const std::string& mirror : mirrors) {
        add(mirror);
    }
    return sources;
}

std::string format_range_header(std::uint64_t offset) {
    return "bytes=" + std::to_string(offset) + "-";
}

std::optional<HttpContentRange> parse_content_range(std::string_view value) {
    const std::string text = trim(value);
    const std::size_t space = text.find(' ');
    if (space == std::string::npos || !iequals(text.substr(0, space), "bytes")) {
        return std::nullopt;
    }

    const std::string_view body(text.data() + space + 1, text.size() - space - 1);
    const std::size_t dash = body.find('-');
    if (dash == std::string_view::npos) {
        return std::nullopt;
    }
    const std::size_t slash = body.find('/', dash + 1);
    if (slash == std::string_view::npos ||
        body.find('/', slash + 1) != std::string_view::npos) {
        return std::nullopt;
    }

    HttpContentRange range;
    if (!parse_unsigned(body.substr(0, dash), range.first) ||
        !parse_unsigned(body.substr(dash + 1, slash - dash - 1), range.last) ||
        range.first > range.last) {
        return std::nullopt;
    }

    const std::string_view total = body.substr(slash + 1);
    if (total != "*") {
        std::uint64_t parsed_total = 0;
        if (!parse_unsigned(total, parsed_total) || parsed_total <= range.last) {
            return std::nullopt;
        }
        range.total = parsed_total;
    }
    return range;
}

bool content_range_matches(
    const HttpContentRange& range,
    std::uint64_t offset,
    std::uint64_t content_length) {
    if (range.first != offset || range.last < range.first) {
        return false;
    }
    if (range.total && range.last >= *range.total) {
        return false;
    }
    if (content_length > 0 && range.last - range.first != content_length - 1) {
        return false;
    }
    return true;
}

bool checksum_matches(std::string_view expected_sha1, std::string_view actual_sha1) {
    if (expected_sha1.empty()) {
        return true;
    }
    return to_lower_ascii(trim(expected_sha1)) == to_lower_ascii(trim(actual_sha1));
}

#ifdef _WIN32
namespace {

struct InternetHandle {
    HINTERNET value = nullptr;

    ~InternetHandle() {
        if (value != nullptr) {
            WinHttpCloseHandle(value);
        }
    }
};

std::string win_error(const char* context) {
    return std::string(context) + " (WinHTTP error " + std::to_string(GetLastError()) + ")";
}

int timeout_milliseconds(std::chrono::milliseconds value) {
    if (value.count() <= 0) {
        return 1;
    }
    const auto maximum = static_cast<std::chrono::milliseconds::rep>(
        std::numeric_limits<int>::max());
    if (value.count() > maximum) {
        return std::numeric_limits<int>::max();
    }
    return static_cast<int>(value.count());
}

std::string query_header(HINTERNET request, DWORD query) {
    DWORD bytes = 0;
    if (WinHttpQueryHeaders(
            request,
            query,
            WINHTTP_HEADER_NAME_BY_INDEX,
            WINHTTP_NO_OUTPUT_BUFFER,
            &bytes,
            WINHTTP_NO_HEADER_INDEX)) {
        return {};
    }
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes == 0) {
        return {};
    }

    std::wstring buffer(bytes / sizeof(wchar_t) + 1, L'\0');
    DWORD size = static_cast<DWORD>(buffer.size() * sizeof(wchar_t));
    if (!WinHttpQueryHeaders(
            request,
            query,
            WINHTTP_HEADER_NAME_BY_INDEX,
            buffer.data(),
            &size,
            WINHTTP_NO_HEADER_INDEX)) {
        return {};
    }
    buffer.resize(size / sizeof(wchar_t));
    while (!buffer.empty() && buffer.back() == L'\0') {
        buffer.pop_back();
    }
    return to_utf8(buffer);
}

std::string read_error_body(HINTERNET request) {
    std::string body;
    std::array<char, 4096> buffer{};
    while (body.size() < 8192) {
        DWORD read = 0;
        if (!WinHttpReadData(request, buffer.data(), static_cast<DWORD>(buffer.size()), &read) ||
            read == 0) {
            break;
        }
        body.append(buffer.data(), read);
    }
    return trim(body);
}

// Never put query strings (which can contain signed tokens) or credentials
// into messages.
std::string display_url(std::string_view url) {
    std::string result(url.substr(0, url.find_first_of("?#")));
    const std::size_t scheme = result.find("://");
    if (scheme != std::string::npos) {
        const std::size_t authority = scheme + 3;
        const std::size_t path = result.find('/', authority);
        const std::size_t credentials = result.find('@', authority);
        if (credentials != std::string::npos &&
            (path == std::string::npos || credentials < path)) {
            result.erase(authority, credentials - authority + 1);
        }
    }
    return result;
}

struct RequestTimeouts {
    std::chrono::milliseconds resolve{30000};
    std::chrono::milliseconds connect{30000};
    std::chrono::milliseconds send{30000};
    std::chrono::milliseconds receive{60000};
};

struct ResponseInfo {
    HttpMetadata metadata;
    std::string content_range;
    std::optional<std::chrono::milliseconds> retry_after;
};

enum class RequestFailure {
    Transport,
    HttpStatus,
    Cancelled,
    Source,
};

class RequestError : public Error {
public:
    RequestError(RequestFailure failure,
                 int status,
                 std::optional<std::chrono::milliseconds> retry_after,
                 const std::string& message)
        : Error(message),
          failure_(failure),
          status_(status),
          retry_after_(std::move(retry_after)) {}

    RequestFailure failure() const {
        return failure_;
    }

    int status() const {
        return status_;
    }

    const std::optional<std::chrono::milliseconds>& retry_after() const {
        return retry_after_;
    }

private:
    RequestFailure failure_;
    int status_;
    std::optional<std::chrono::milliseconds> retry_after_;
};

enum class DownloadFailure {
    Transient,
    Source,
    Checksum,
    RangeRestart,
    Local,
    Cancelled,
};

class DownloadError : public Error {
public:
    DownloadError(DownloadFailure failure,
                  const std::string& message,
                  std::optional<std::chrono::milliseconds> retry_after = std::nullopt)
        : Error(message), failure_(failure), retry_after_(std::move(retry_after)) {}

    DownloadFailure failure() const {
        return failure_;
    }

    const std::optional<std::chrono::milliseconds>& retry_after() const {
        return retry_after_;
    }

private:
    DownloadFailure failure_;
    std::optional<std::chrono::milliseconds> retry_after_;
};

HttpMetadata perform_request(
    std::string_view url,
    const std::vector<std::string>& headers,
    const std::function<void(const ResponseInfo&)>& on_response,
    const std::function<void(const void*, std::size_t)>& sink,
    const ProgressCallback& progress,
    const CancelCallback& cancelled,
    const RequestTimeouts& timeouts) {
    if (url.empty()) {
        throw RequestError(RequestFailure::Source, 0, std::nullopt, "HTTP URL is empty");
    }
    if (!sink) {
        throw RequestError(
            RequestFailure::Source, 0, std::nullopt, "HTTP request has no response sink");
    }
    if (cancelled && cancelled()) {
        throw RequestError(RequestFailure::Cancelled, 0, std::nullopt, "Download cancelled");
    }

    const std::wstring wide_url = to_wide(url);
    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = static_cast<DWORD>(-1);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wide_url.c_str(), 0, 0, &components)) {
        throw RequestError(
            RequestFailure::Source,
            0,
            std::nullopt,
            "Invalid HTTP URL: " + display_url(url));
    }

    const std::wstring host(components.lpszHostName, components.dwHostNameLength);
    std::wstring path(components.lpszUrlPath, components.dwUrlPathLength);
    if (components.lpszExtraInfo != nullptr) {
        path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
    }
    if (path.empty()) {
        path = L"/";
    }
    const bool secure = components.nScheme == INTERNET_SCHEME_HTTPS;
    const INTERNET_PORT port = components.nPort != 0
        ? components.nPort
        : (secure ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT);

    InternetHandle session;
    const std::wstring user_agent = to_wide(kLauncherUserAgent);
    const auto proxy = system_proxy_for_url(url);
    if (proxy) {
        const std::wstring proxy_server = to_wide(proxy->server);
        const std::wstring proxy_bypass = to_wide(proxy->bypass);
        session.value = WinHttpOpen(
            user_agent.c_str(),
            WINHTTP_ACCESS_TYPE_NAMED_PROXY,
            proxy_server.c_str(),
            proxy_bypass.empty() ? WINHTTP_NO_PROXY_BYPASS : proxy_bypass.c_str(),
            0);
    } else {
        session.value = WinHttpOpen(
            user_agent.c_str(),
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS,
            0);
    }
    if (session.value == nullptr) {
        throw RequestError(
            RequestFailure::Transport, 0, std::nullopt, win_error("Unable to initialize WinHTTP"));
    }
    WinHttpSetTimeouts(
        session.value,
        timeout_milliseconds(timeouts.resolve),
        timeout_milliseconds(timeouts.connect),
        timeout_milliseconds(timeouts.send),
        timeout_milliseconds(timeouts.receive));

    InternetHandle connection;
    connection.value = WinHttpConnect(session.value, host.c_str(), port, 0);
    if (connection.value == nullptr) {
        throw RequestError(
            RequestFailure::Transport,
            0,
            std::nullopt,
            win_error("Unable to connect to HTTP host"));
    }

    InternetHandle request;
    request.value = WinHttpOpenRequest(
        connection.value,
        L"GET",
        path.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        secure ? WINHTTP_FLAG_SECURE : 0);
    if (request.value == nullptr) {
        throw RequestError(
            RequestFailure::Transport,
            0,
            std::nullopt,
            win_error("Unable to create HTTP request"));
    }

    DWORD redirect_policy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    WinHttpSetOption(
        request.value, WINHTTP_OPTION_REDIRECT_POLICY, &redirect_policy,
        sizeof(redirect_policy));

    for (const std::string& header : headers) {
        const std::wstring wide_header = to_wide(header);
        if (!WinHttpAddRequestHeaders(
                request.value,
                wide_header.c_str(),
                static_cast<DWORD>(-1),
                WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE)) {
            throw RequestError(
                RequestFailure::Transport,
                0,
                std::nullopt,
                win_error("Unable to add HTTP request header"));
        }
    }

    if (!WinHttpSendRequest(
            request.value,
            WINHTTP_NO_ADDITIONAL_HEADERS,
            0,
            WINHTTP_NO_REQUEST_DATA,
            0,
            0,
            0)) {
        throw RequestError(
            RequestFailure::Transport, 0, std::nullopt, win_error("Unable to send HTTP request"));
    }
    if (!WinHttpReceiveResponse(request.value, nullptr)) {
        throw RequestError(
            RequestFailure::Transport,
            0,
            std::nullopt,
            win_error("Unable to receive HTTP response"));
    }

    DWORD status = 0;
    DWORD status_size = sizeof(status);
    if (!WinHttpQueryHeaders(
            request.value,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &status,
            &status_size,
            WINHTTP_NO_HEADER_INDEX)) {
        throw RequestError(
            RequestFailure::Transport,
            0,
            std::nullopt,
            win_error("Unable to read HTTP status code"));
    }

    HttpMetadata metadata;
    metadata.status = static_cast<int>(status);
    DWORD length = 0;
    DWORD length_size = sizeof(length);
    if (WinHttpQueryHeaders(
            request.value,
            WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &length,
            &length_size,
            WINHTTP_NO_HEADER_INDEX)) {
        metadata.content_length = length;
    }

    const std::string content_range = query_header(request.value, WINHTTP_QUERY_CONTENT_RANGE);
    const std::optional<std::chrono::milliseconds> retry_after =
        parse_retry_after(query_header(request.value, WINHTTP_QUERY_RETRY_AFTER));

    if (status < 200 || status >= 300) {
        const std::string error_body = read_error_body(request.value);
        std::string message =
            "HTTP " + std::to_string(status) + " for " + display_url(url);
        if (!error_body.empty()) {
            message += ": " + error_body;
        }
        throw RequestError(
            RequestFailure::HttpStatus, static_cast<int>(status), retry_after, message);
    }

    if (on_response) {
        on_response(ResponseInfo{metadata, content_range, retry_after});
    }

    std::array<char, 64 * 1024> buffer{};
    std::uint64_t received = 0;
    while (true) {
        if (cancelled && cancelled()) {
            throw RequestError(
                RequestFailure::Cancelled, 0, std::nullopt, "Download cancelled");
        }
        DWORD read = 0;
        if (!WinHttpReadData(
                request.value, buffer.data(), static_cast<DWORD>(buffer.size()), &read)) {
            throw RequestError(
                RequestFailure::Transport,
                0,
                std::nullopt,
                win_error("Unable to read HTTP response"));
        }
        if (read == 0) {
            break;
        }
        sink(buffer.data(), static_cast<std::size_t>(read));
        received += read;
        if (progress) {
            progress(received, metadata.content_length);
        }
    }
    return metadata;
}

bool replace_file(const std::filesystem::path& source, const std::filesystem::path& destination) {
#ifdef _WIN32
    if (MoveFileExW(
            source.c_str(),
            destination.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return true;
    }
    return false;
#else
    std::error_code error;
    std::filesystem::rename(source, destination, error);
    return !error;
#endif
}

void sleep_with_cancel(
    std::chrono::milliseconds delay,
    const CancelCallback& cancelled) {
    const auto deadline = std::chrono::steady_clock::now() + delay;
    while (true) {
        if (cancelled && cancelled()) {
            fail("Download cancelled");
        }
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            break;
        }
        auto slice = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        slice = std::min(slice, std::chrono::milliseconds(100));
        if (slice <= std::chrono::milliseconds::zero()) {
            slice = std::chrono::milliseconds(1);
        }
        std::this_thread::sleep_for(slice);
    }
}

void fetch_to_partial(
    std::string_view url,
    const std::filesystem::path& partial,
    const HttpDownloadOptions& options,
    const ProgressCallback& progress) {
    std::error_code error;
    std::uint64_t offset = 0;
    if (options.resumable) {
        if (std::filesystem::is_regular_file(partial, error)) {
            const auto size = std::filesystem::file_size(partial, error);
            if (!error) {
                offset = static_cast<std::uint64_t>(size);
            }
        }
    } else {
        std::filesystem::remove(partial, error);
    }
    error.clear();

    const bool requested_range = offset > 0;
    std::vector<std::string> headers;
    if (requested_range) {
        headers.push_back("Range: " + format_range_header(offset));
    }

    std::ofstream output;
    std::uint64_t base = 0;
    std::uint64_t total = 0;
    bool total_known = false;
    std::uint64_t written = 0;

    const auto open_output =
        [&](std::uint64_t append_offset, std::uint64_t expected_total, bool known) {
            if (append_offset == 0) {
                output.open(partial, std::ios::binary | std::ios::trunc);
            } else {
                const auto size = std::filesystem::file_size(partial, error);
                if (error || static_cast<std::uint64_t>(size) != append_offset) {
                    throw DownloadError(
                        DownloadFailure::RangeRestart,
                        "Partial download changed while resuming: " + path_to_utf8(partial));
                }
                error.clear();
                output.open(partial, std::ios::binary | std::ios::app);
            }
            if (!output) {
                throw DownloadError(
                    DownloadFailure::Local,
                    "Unable to open download file: " + path_to_utf8(partial));
            }
            base = append_offset;
            total = expected_total;
            total_known = known;
            if (progress) {
                progress(base, total);
            }
        };

    const auto on_response = [&](const ResponseInfo& response) {
        const int status = response.metadata.status;
        if (requested_range && status == 206) {
            const auto range = parse_content_range(response.content_range);
            if (!range ||
                !content_range_matches(*range, offset, response.metadata.content_length)) {
                throw DownloadError(
                    DownloadFailure::RangeRestart,
                    "Invalid Content-Range in resumed download: " + response.content_range);
            }
            std::uint64_t expected_total = 0;
            bool known = false;
            if (range->total) {
                expected_total = *range->total;
                known = true;
            } else if (response.metadata.content_length > 0) {
                expected_total = offset + response.metadata.content_length;
                known = true;
            }
            open_output(offset, expected_total, known);
            return;
        }

        if (status == 206) {
            const auto range = parse_content_range(response.content_range);
            if (!range || range->first != 0 ||
                !content_range_matches(*range, 0, response.metadata.content_length)) {
                throw DownloadError(
                    DownloadFailure::RangeRestart,
                    "Unexpected Content-Range in download: " + response.content_range);
            }
            std::uint64_t expected_total = 0;
            bool known = false;
            if (range->total) {
                expected_total = *range->total;
                known = true;
            } else if (response.metadata.content_length > 0) {
                expected_total = response.metadata.content_length;
                known = true;
            }
            open_output(0, expected_total, known);
            return;
        }

        // A 200 response means the server ignored our Range header, or that no
        // range was requested. Either way the file must start from byte zero.
        const bool known = response.metadata.content_length > 0;
        open_output(0, response.metadata.content_length, known);
    };

    const auto sink = [&](const void* data, std::size_t size) {
        output.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
        if (!output) {
            throw DownloadError(
                DownloadFailure::Local,
                "Unable to write download file: " + path_to_utf8(partial));
        }
        written += size;
        if (progress) {
            progress(base + written, total);
        }
    };

    const RequestTimeouts timeouts{
        options.resolve_timeout,
        options.connect_timeout,
        options.send_timeout,
        options.receive_timeout,
    };

    try {
        perform_request(url, headers, on_response, sink, {}, options.cancelled, timeouts);
    } catch (const RequestError& request_error) {
        if (output.is_open()) {
            output.close();
        }
        switch (request_error.failure()) {
        case RequestFailure::Cancelled:
            throw DownloadError(DownloadFailure::Cancelled, request_error.what());
        case RequestFailure::Transport:
            throw DownloadError(DownloadFailure::Transient, request_error.what());
        case RequestFailure::Source:
            throw DownloadError(DownloadFailure::Source, request_error.what());
        case RequestFailure::HttpStatus:
            if (request_error.status() == 416) {
                throw DownloadError(DownloadFailure::RangeRestart, request_error.what());
            }
            if (classify_http_status(request_error.status()) == HttpRetryClass::Retryable) {
                throw DownloadError(
                    DownloadFailure::Transient,
                    request_error.what(),
                    request_error.retry_after());
            }
            throw DownloadError(DownloadFailure::Source, request_error.what());
        }
        throw DownloadError(DownloadFailure::Source, request_error.what());
    } catch (...) {
        if (output.is_open()) {
            output.close();
        }
        throw;
    }

    output.flush();
    if (!output) {
        output.close();
        throw DownloadError(
            DownloadFailure::Local,
            "Unable to flush download file: " + path_to_utf8(partial));
    }
    output.close();
    if (!output) {
        throw DownloadError(
            DownloadFailure::Local,
            "Unable to close download file: " + path_to_utf8(partial));
    }

    const auto file_size = std::filesystem::file_size(partial, error);
    if (error) {
        throw DownloadError(
            DownloadFailure::Local,
            "Unable to inspect download file: " + path_to_utf8(partial));
    }
    const auto size = static_cast<std::uint64_t>(file_size);
    if (total_known) {
        if (size < total) {
            throw DownloadError(
                DownloadFailure::Transient,
                "Incomplete download for " + display_url(url) + " (" +
                    std::to_string(size) + " of " + std::to_string(total) + " bytes)");
        }
        if (size > total) {
            throw DownloadError(
                DownloadFailure::RangeRestart,
                "Download exceeds the advertised size for " + display_url(url));
        }
    }
}

void download_impl(
    std::string_view url,
    const std::filesystem::path& destination,
    const HttpDownloadOptions& options,
    std::string_view expected_sha1,
    const ProgressCallback& progress) {
    const std::string expected = to_lower_ascii(trim(expected_sha1));
    std::error_code error;

    if (expected.empty()) {
        if (std::filesystem::is_regular_file(destination, error)) {
            return;
        }
    } else if (
        std::filesystem::is_regular_file(destination, error) &&
        checksum_matches(expected, sha1_file(destination))) {
        const auto size = std::filesystem::file_size(destination, error);
        if (progress) {
            progress(error ? 0 : size, error ? 0 : size);
        }
        return;
    }

    ensure_directory(destination.parent_path());
    std::filesystem::path partial = destination;
    partial += ".part";

    if (!expected.empty() && std::filesystem::is_regular_file(partial, error) &&
        checksum_matches(expected, sha1_file(partial))) {
        if (!replace_file(partial, destination)) {
            fail("Unable to finalize download: " + path_to_utf8(destination));
        }
        const auto size = std::filesystem::file_size(destination, error);
        if (progress) {
            progress(error ? 0 : size, error ? 0 : size);
        }
        return;
    }
    if (!options.resumable) {
        std::filesystem::remove(partial, error);
    }

    const std::vector<std::string> sources = build_download_sources(url, options.mirrors);
    if (sources.empty()) {
        fail("No download URL was provided");
    }

    const int max_retries = std::max(0, options.max_retries);
    std::string last_error;
    for (const std::string& source : sources) {
        bool range_restarted = false;
        int retries_used = 0;
        while (true) {
            if (options.cancelled && options.cancelled()) {
                fail("Download cancelled: " + display_url(source));
            }
            try {
                fetch_to_partial(source, partial, options, progress);
                if (!expected.empty() && !checksum_matches(expected, sha1_file(partial))) {
                    std::filesystem::remove(partial, error);
                    throw DownloadError(
                        DownloadFailure::Checksum,
                        "SHA-1 mismatch for " + display_url(source));
                }
                if (!replace_file(partial, destination)) {
                    throw DownloadError(
                        DownloadFailure::Local,
                        "Unable to finalize download: " + path_to_utf8(destination));
                }
                return;
            } catch (const DownloadError& download_error) {
                last_error = download_error.what();
                if (download_error.failure() == DownloadFailure::Local) {
                    std::filesystem::remove(partial, error);
                    fail(download_error.what());
                }
                if (download_error.failure() == DownloadFailure::Cancelled) {
                    fail("Download cancelled: " + display_url(source));
                }
                if (download_error.failure() == DownloadFailure::RangeRestart) {
                    std::filesystem::remove(partial, error);
                    if (!range_restarted) {
                        range_restarted = true;
                        continue;
                    }
                    break;
                }

                const bool retryable =
                    download_error.failure() == DownloadFailure::Transient;
                if (retryable && retries_used < max_retries) {
                    const auto delay = compute_retry_delay(
                        retries_used, options, download_error.retry_after());
                    ++retries_used;
                    sleep_with_cancel(delay, options.cancelled);
                    continue;
                }
                break;
            }
        }
    }

    std::string message = "Unable to download " + display_url(url);
    if (!last_error.empty()) {
        message += ": " + last_error;
    }
    fail(message);
}

} // namespace
#endif

HttpMetadata http_request(std::string_view url,
                          const std::function<void(const void*, std::size_t)>& sink,
                          const ProgressCallback& progress) {
#ifdef _WIN32
    const RequestTimeouts timeouts;
    return perform_request(
        url, {}, {}, sink, progress, {}, timeouts);
#else
    (void)url;
    (void)sink;
    (void)progress;
    fail("HTTP is only implemented on Windows in this build");
#endif
}

std::string http_get(std::string_view url) {
    constexpr int kMaxRetries = 3;
    const HttpDownloadOptions retry_policy;
    for (int retry = 0;; ++retry) {
        std::string body;
        try {
            http_request(url, [&body](const void* data, std::size_t size) {
                body.append(static_cast<const char*>(data), size);
            });
            return body;
        } catch (const RequestError& error) {
            const bool retryable =
                error.failure() == RequestFailure::Transport ||
                (error.failure() == RequestFailure::HttpStatus &&
                 classify_http_status(error.status()) == HttpRetryClass::Retryable);
            if (!retryable || retry >= kMaxRetries) {
                throw;
            }
            const auto delay =
                compute_retry_delay(retry, retry_policy, error.retry_after());
            log_warn("HTTP GET failed, retrying in " +
                     std::to_string(delay.count()) + " ms: " + display_url(url));
            std::this_thread::sleep_for(delay);
        }
    }
}

void http_download(std::string_view url,
                   const std::filesystem::path& destination,
                   std::string_view expected_sha1,
                   const ProgressCallback& progress) {
    http_download(url, destination, HttpDownloadOptions{}, expected_sha1, progress);
}

void http_download(std::string_view url,
                   const std::filesystem::path& destination,
                   const HttpDownloadOptions& options,
                   std::string_view expected_sha1,
                   const ProgressCallback& progress) {
#ifdef _WIN32
    download_impl(url, destination, options, expected_sha1, progress);
#else
    (void)url;
    (void)destination;
    (void)options;
    (void)expected_sha1;
    (void)progress;
    fail("HTTP is only implemented on Windows in this build");
#endif
}

} // namespace aurora
