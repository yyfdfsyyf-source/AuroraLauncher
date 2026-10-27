#include "core/http.hpp"

#include "core/error.hpp"
#include "core/hash.hpp"
#include "core/log.hpp"
#include "core/paths.hpp"
#include "core/strings.hpp"

#include <array>
#include <fstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#endif

namespace aurora {

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

} // namespace
#endif

HttpMetadata http_request(std::string_view url,
                          const std::function<void(const void*, std::size_t)>& sink,
                          const ProgressCallback& progress) {
#ifdef _WIN32
    if (url.empty()) {
        fail("HTTP URL is empty");
    }
    const std::wstring wide_url = to_wide(url);
    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = static_cast<DWORD>(-1);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wide_url.c_str(), 0, 0, &components)) {
        fail("Invalid HTTP URL: " + std::string(url));
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
    session.value = WinHttpOpen(
        L"AuroraLauncher/0.2",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);
    if (session.value == nullptr) {
        fail(win_error("Unable to initialize WinHTTP"));
    }
    WinHttpSetTimeouts(session.value, 30000, 30000, 60000, 60000);

    InternetHandle connection;
    connection.value = WinHttpConnect(session.value, host.c_str(), port, 0);
    if (connection.value == nullptr) {
        fail(win_error("Unable to connect to HTTP host"));
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
        fail(win_error("Unable to create HTTP request"));
    }

    DWORD redirect_policy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    WinHttpSetOption(
        request.value, WINHTTP_OPTION_REDIRECT_POLICY, &redirect_policy,
        sizeof(redirect_policy));

    if (!WinHttpSendRequest(
            request.value,
            WINHTTP_NO_ADDITIONAL_HEADERS,
            0,
            WINHTTP_NO_REQUEST_DATA,
            0,
            0,
            0)) {
        fail(win_error("Unable to send HTTP request"));
    }
    if (!WinHttpReceiveResponse(request.value, nullptr)) {
        fail(win_error("Unable to receive HTTP response"));
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
        fail(win_error("Unable to read HTTP status code"));
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

    if (status < 200 || status >= 300) {
        const std::string error_body = read_error_body(request.value);
        std::string message = "HTTP " + std::to_string(status) + " for " + std::string(url);
        if (!error_body.empty()) {
            message += ": " + error_body;
        }
        fail(message);
    }

    std::array<char, 64 * 1024> buffer{};
    std::uint64_t received = 0;
    while (true) {
        DWORD read = 0;
        if (!WinHttpReadData(
                request.value, buffer.data(), static_cast<DWORD>(buffer.size()), &read)) {
            fail(win_error("Unable to read HTTP response"));
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
#else
    (void)url;
    (void)sink;
    (void)progress;
    fail("HTTP is only implemented on Windows in this build");
#endif
}

std::string http_get(std::string_view url) {
    std::string body;
    http_request(url, [&body](const void* data, std::size_t size) {
        body.append(static_cast<const char*>(data), size);
    });
    return body;
}

void http_download(std::string_view url,
                   const std::filesystem::path& destination,
                   std::string_view expected_sha1,
                   const ProgressCallback& progress) {
    if (expected_sha1.empty()) {
        std::error_code ec;
        if (std::filesystem::is_regular_file(destination, ec)) {
            return;
        }
    } else {
        std::error_code ec;
        if (std::filesystem::is_regular_file(destination, ec) &&
            sha1_file(destination) == to_lower_ascii(std::string(expected_sha1))) {
            const auto size = std::filesystem::file_size(destination, ec);
            if (progress) {
                progress(ec ? 0 : size, ec ? 0 : size);
            }
            return;
        }
    }

    ensure_directory(destination.parent_path());
    std::filesystem::path partial = destination;
    partial += ".part";
    std::error_code ec;
    std::filesystem::remove(partial, ec);

    {
        std::ofstream output(partial, std::ios::binary | std::ios::trunc);
        if (!output) {
            fail("Unable to open download file: " + path_to_utf8(partial));
        }
        try {
            http_request(url, [&output](const void* data, std::size_t size) {
                output.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
                if (!output) {
                    fail("Unable to write download file");
                }
            }, progress);
        } catch (...) {
            output.close();
            std::filesystem::remove(partial, ec);
            throw;
        }
        output.flush();
        if (!output) {
            output.close();
            std::filesystem::remove(partial, ec);
            fail("Unable to flush download file: " + path_to_utf8(partial));
        }
    }

    if (!expected_sha1.empty()) {
        const std::string actual = sha1_file(partial);
        if (actual != to_lower_ascii(std::string(expected_sha1))) {
            std::filesystem::remove(partial, ec);
            fail("SHA-1 mismatch for " + std::string(url) + " (expected " +
                 std::string(expected_sha1) + ", got " + actual + ")");
        }
    }

    std::filesystem::remove(destination, ec);
    std::filesystem::rename(partial, destination, ec);
    if (ec) {
        std::filesystem::remove(partial, ec);
        fail("Unable to finalize download: " + path_to_utf8(destination));
    }
}

} // namespace aurora
