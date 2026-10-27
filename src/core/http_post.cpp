#include "core/http_post.hpp"

#include "core/error.hpp"
#include "core/strings.hpp"

#include <array>
#include <limits>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#endif

namespace aurora {

namespace {

std::string encode_form_field(std::string_view name, std::string_view value) {
    return form_url_encode(name) + "=" + form_url_encode(value);
}

#ifdef _WIN32

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

class WinHttpTransport final : public HttpTransport {
public:
    HttpResponse send(const HttpRequest& request) override {
        return http_send(request);
    }
};

#else

class UnsupportedTransport final : public HttpTransport {
public:
    HttpResponse send(const HttpRequest&) override {
        fail("HTTP is only implemented on Windows in this build");
    }
};

#endif

} // namespace

std::string form_url_encode(std::string_view value) {
    static constexpr char hexadecimal[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(value.size());
    for (const unsigned char character : value) {
        const bool unreserved =
            (character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') ||
            character == '-' || character == '_' || character == '.' || character == '~';
        if (unreserved) {
            result.push_back(static_cast<char>(character));
        } else {
            result.push_back('%');
            result.push_back(hexadecimal[(character >> 4) & 0x0F]);
            result.push_back(hexadecimal[character & 0x0F]);
        }
    }
    return result;
}

std::string form_encode(const std::vector<std::pair<std::string, std::string>>& fields) {
    std::string body;
    for (const auto& [name, value] : fields) {
        if (!body.empty()) {
            body.push_back('&');
        }
        body += encode_form_field(name, value);
    }
    return body;
}

std::shared_ptr<HttpTransport> default_http_transport() {
#ifdef _WIN32
    return std::make_shared<WinHttpTransport>();
#else
    return std::make_shared<UnsupportedTransport>();
#endif
}

HttpResponse http_send(const HttpRequest& request) {
#ifdef _WIN32
    if (request.url.empty()) {
        fail("HTTP URL is empty");
    }
    if (request.method.empty() ||
        request.method.find_first_of("\r\n") != std::string::npos) {
        fail("Invalid HTTP method");
    }

    const std::wstring wide_url = to_wide(request.url);
    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = static_cast<DWORD>(-1);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wide_url.c_str(), 0, 0, &components)) {
        fail("Invalid HTTP URL: " + request.url);
    }
    if (components.dwHostNameLength == 0 || components.lpszHostName == nullptr) {
        fail("Invalid HTTP URL: " + request.url);
    }
    if (components.nScheme != INTERNET_SCHEME_HTTPS &&
        components.nScheme != INTERNET_SCHEME_HTTP) {
        fail("Unsupported HTTP URL scheme: " + request.url);
    }

    const std::wstring host(components.lpszHostName, components.dwHostNameLength);
    std::wstring path;
    if (components.lpszUrlPath != nullptr) {
        path.append(components.lpszUrlPath, components.dwUrlPathLength);
    }
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

    std::wstring header_block;
    for (const HttpHeader& header : request.headers) {
        if (header.name.empty() ||
            header.name.find_first_of("\r\n:") != std::string::npos ||
            header.value.find_first_of("\r\n") != std::string::npos) {
            fail("Invalid HTTP header");
        }
        header_block += to_wide(header.name);
        header_block += L": ";
        header_block += to_wide(header.value);
        header_block += L"\r\n";
    }

    if (request.body.size() > std::numeric_limits<DWORD>::max()) {
        fail("HTTP request body is too large");
    }
    const auto body_size = static_cast<DWORD>(request.body.size());

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
    WinHttpSetTimeouts(session.value, 30000, 30000, 30000, 60000);

    InternetHandle connection;
    connection.value = WinHttpConnect(session.value, host.c_str(), port, 0);
    if (connection.value == nullptr) {
        fail(win_error("Unable to connect to HTTP host"));
    }

    const std::wstring wide_method = to_wide(request.method);
    InternetHandle http_request;
    http_request.value = WinHttpOpenRequest(
        connection.value,
        wide_method.c_str(),
        path.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        secure ? WINHTTP_FLAG_SECURE : 0);
    if (http_request.value == nullptr) {
        fail(win_error("Unable to create HTTP request"));
    }

    DWORD redirect_policy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    WinHttpSetOption(
        http_request.value,
        WINHTTP_OPTION_REDIRECT_POLICY,
        &redirect_policy,
        sizeof(redirect_policy));

    const void* body = request.body.empty() ? nullptr : request.body.data();
    if (!WinHttpSendRequest(
            http_request.value,
            header_block.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : header_block.c_str(),
            header_block.empty() ? 0 : static_cast<DWORD>(-1),
            const_cast<void*>(body),
            body_size,
            body_size,
            0)) {
        fail(win_error("Unable to send HTTP request"));
    }
    if (!WinHttpReceiveResponse(http_request.value, nullptr)) {
        fail(win_error("Unable to receive HTTP response"));
    }

    DWORD status = 0;
    DWORD status_size = sizeof(status);
    if (!WinHttpQueryHeaders(
            http_request.value,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &status,
            &status_size,
            WINHTTP_NO_HEADER_INDEX)) {
        fail(win_error("Unable to read HTTP status code"));
    }

    HttpResponse response;
    response.status = static_cast<int>(status);

    constexpr std::size_t maximum_body_size = 4 * 1024 * 1024;
    std::array<char, 16 * 1024> buffer{};
    while (true) {
        DWORD read = 0;
        if (!WinHttpReadData(
                http_request.value,
                buffer.data(),
                static_cast<DWORD>(buffer.size()),
                &read)) {
            fail(win_error("Unable to read HTTP response"));
        }
        if (read == 0) {
            break;
        }
        if (response.body.size() + read > maximum_body_size) {
            fail("HTTP response body is too large");
        }
        response.body.append(buffer.data(), static_cast<std::size_t>(read));
    }
    return response;
#else
    (void)request;
    fail("HTTP is only implemented on Windows in this build");
#endif
}

HttpResponse http_post_form(
    std::string_view url,
    const std::vector<std::pair<std::string, std::string>>& fields,
    const std::vector<HttpHeader>& headers) {
    HttpRequest request;
    request.method = "POST";
    request.url = std::string(url);
    request.body = form_encode(fields);
    request.headers = headers;
    request.headers.push_back({"Content-Type", "application/x-www-form-urlencoded"});
    request.headers.push_back({"Accept", "application/json"});
    return http_send(request);
}

HttpResponse http_post_json(std::string_view url,
                            std::string_view body,
                            const std::vector<HttpHeader>& headers) {
    HttpRequest request;
    request.method = "POST";
    request.url = std::string(url);
    request.body = std::string(body);
    request.headers = headers;
    request.headers.push_back({"Content-Type", "application/json"});
    request.headers.push_back({"Accept", "application/json"});
    return http_send(request);
}

HttpResponse http_get_with_headers(std::string_view url,
                                   const std::vector<HttpHeader>& headers) {
    HttpRequest request;
    request.method = "GET";
    request.url = std::string(url);
    request.headers = headers;
    return http_send(request);
}

} // namespace aurora
