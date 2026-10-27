#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace aurora {

struct SystemProxy {
    // Host and optional port understood by WinHTTP, without a URL scheme.
    std::string server;
    // Semicolon-separated WinHTTP bypass list.
    std::string bypass;
};

// Reads HTTPS_PROXY/HTTP_PROXY first, then the current user's WinINET
// configuration (including a PAC URL). Returns nullopt for direct access.
std::optional<SystemProxy> system_proxy_for_url(std::string_view url);

} // namespace aurora
