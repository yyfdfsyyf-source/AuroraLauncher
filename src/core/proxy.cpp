#include "core/proxy.hpp"

#include "core/strings.hpp"

#include <algorithm>
#include <cstdlib>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#endif

namespace aurora {

namespace {

std::string environment_value(const char* name) {
#ifdef _MSC_VER
    char* buffer = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&buffer, &size, name) != 0 || buffer == nullptr) {
        return {};
    }
    std::string value(buffer);
    free(buffer);
    return value;
#else
    const char* value = std::getenv(name);
    return value == nullptr ? std::string{} : std::string(value);
#endif
}

bool is_winhttp_bypass_entry(const std::string& entry) {
    // WinHTTP rejects host:port entries and bare IPv6 literals such as "::1"
    // with ERROR_INVALID_PARAMETER from WinHttpOpen. Bracketed IPv6 is the
    // form it understands.
    if (entry.find(':') != std::string::npos) {
        return entry.size() > 2 && entry.front() == '[' && entry.back() == ']';
    }
    return true;
}

std::string normalize_bypass_list(std::string value, bool comma_separated) {
    if (value.empty()) {
        return {};
    }
    if (comma_separated) {
        std::replace(value.begin(), value.end(), ',', ';');
    }
    std::vector<std::string> entries;
    for (const std::string& part : split(value, ';')) {
        const std::string entry = trim(part);
        if (!entry.empty() && is_winhttp_bypass_entry(entry)) {
            entries.push_back(entry);
        }
    }
    return join(entries, ";");
}

std::string select_proxy_for_scheme(std::string_view value, std::string_view scheme) {
    const std::string text = trim(value);
    if (text.find('=') == std::string::npos) {
        return text;
    }
    for (const std::string& part : split(text, ';')) {
        const std::size_t equals = part.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        if (to_lower_ascii(trim(part.substr(0, equals))) == scheme) {
            return trim(part.substr(equals + 1));
        }
    }
    return {};
}

std::string normalize_proxy_server(std::string value) {
    value = trim(value);
    if (value.empty() || iequals(value, "direct")) {
        return {};
    }
    const std::size_t scheme_end = value.find("://");
    if (scheme_end != std::string::npos) {
        // WinHTTP named proxies speak HTTP; SOCKS and other schemes are not
        // silently treated as something else.
        if (!iequals(value.substr(0, scheme_end), "http")) {
            return {};
        }
        value.erase(0, scheme_end + 3);
    }
    const std::size_t path = value.find_first_of("/?#");
    if (path != std::string::npos) {
        value.resize(path);
    }
    return trim(value);
}

std::optional<SystemProxy> environment_proxy(std::string_view scheme) {
    std::string proxy;
    if (scheme == "https") {
        proxy = environment_value("HTTPS_PROXY");
        if (proxy.empty()) {
            proxy = environment_value("https_proxy");
        }
    } else {
        proxy = environment_value("HTTP_PROXY");
        if (proxy.empty()) {
            proxy = environment_value("http_proxy");
        }
    }
    if (proxy.empty()) {
        proxy = environment_value("ALL_PROXY");
    }
    if (proxy.empty()) {
        proxy = environment_value("all_proxy");
    }
    const std::string server = normalize_proxy_server(proxy);
    if (server.empty()) {
        return std::nullopt;
    }
    std::string bypass = environment_value("NO_PROXY");
    if (bypass.empty()) {
        bypass = environment_value("no_proxy");
    }
    return SystemProxy{server, normalize_bypass_list(bypass, true)};
}

#ifdef _WIN32

class WinHttpHandle {
public:
    ~WinHttpHandle() {
        if (value_ != nullptr) {
            WinHttpCloseHandle(value_);
        }
    }

    WinHttpHandle(const WinHttpHandle&) = delete;
    WinHttpHandle& operator=(const WinHttpHandle&) = delete;
    WinHttpHandle() = default;

    HINTERNET* address() { return &value_; }
    HINTERNET get() const { return value_; }

private:
    HINTERNET value_ = nullptr;
};

std::optional<SystemProxy> query_pac_proxy(
    std::string_view url,
    const WINHTTP_CURRENT_USER_IE_PROXY_CONFIG& config,
    std::string_view scheme) {
    WinHttpHandle session;
    *session.address() = WinHttpOpen(
        L"AuroraLauncher proxy resolver",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);
    if (session.get() == nullptr) {
        return std::nullopt;
    }

    WINHTTP_AUTOPROXY_OPTIONS options{};
    if (config.lpszAutoConfigUrl != nullptr) {
        options.dwFlags = WINHTTP_AUTOPROXY_CONFIG_URL;
        options.lpszAutoConfigUrl = config.lpszAutoConfigUrl;
    } else {
        options.dwFlags = WINHTTP_AUTOPROXY_AUTO_DETECT;
        options.dwAutoDetectFlags =
            WINHTTP_AUTO_DETECT_TYPE_DHCP | WINHTTP_AUTO_DETECT_TYPE_DNS_A;
    }
    options.fAutoLogonIfChallenged = TRUE;

    WINHTTP_PROXY_INFO info{};
    const std::wstring wide_url = to_wide(url);
    const BOOL resolved =
        WinHttpGetProxyForUrl(session.get(), wide_url.c_str(), &options, &info);
    if (!resolved) {
        return std::nullopt;
    }

    std::string server;
    if (info.lpszProxy != nullptr) {
        server = normalize_proxy_server(
            select_proxy_for_scheme(to_utf8(info.lpszProxy), scheme));
    }
    std::string bypass;
    if (info.lpszProxyBypass != nullptr) {
        bypass = normalize_bypass_list(to_utf8(info.lpszProxyBypass), false);
    }
    if (info.lpszProxy != nullptr) {
        GlobalFree(info.lpszProxy);
    }
    if (info.lpszProxyBypass != nullptr) {
        GlobalFree(info.lpszProxyBypass);
    }
    if (server.empty()) {
        return std::nullopt;
    }
    return SystemProxy{server, bypass};
}

#endif

} // namespace

std::optional<SystemProxy> system_proxy_for_url(std::string_view url) {
    const std::size_t colon = url.find(':');
    const std::string scheme = colon == std::string::npos
        ? "https"
        : to_lower_ascii(std::string(url.substr(0, colon)));
    if (auto proxy = environment_proxy(scheme)) {
        return proxy;
    }
#ifdef _WIN32
    WINHTTP_CURRENT_USER_IE_PROXY_CONFIG config{};
    if (!WinHttpGetIEProxyConfigForCurrentUser(&config)) {
        return std::nullopt;
    }

    std::optional<SystemProxy> result;
    if (config.lpszProxy != nullptr) {
        const std::string server = normalize_proxy_server(
            select_proxy_for_scheme(to_utf8(config.lpszProxy), scheme));
        if (!server.empty()) {
            const std::string bypass = config.lpszProxyBypass == nullptr
                ? std::string{}
                : normalize_bypass_list(to_utf8(config.lpszProxyBypass), false);
            result = SystemProxy{server, bypass};
        }
    }
    if (!result && (config.lpszAutoConfigUrl != nullptr || config.fAutoDetect)) {
        result = query_pac_proxy(url, config, scheme);
    }

    if (config.lpszProxy != nullptr) {
        GlobalFree(config.lpszProxy);
    }
    if (config.lpszProxyBypass != nullptr) {
        GlobalFree(config.lpszProxyBypass);
    }
    if (config.lpszAutoConfigUrl != nullptr) {
        GlobalFree(config.lpszAutoConfigUrl);
    }
    return result;
#else
    return std::nullopt;
#endif
}

} // namespace aurora
