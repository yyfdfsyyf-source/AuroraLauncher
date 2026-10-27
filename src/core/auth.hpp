#pragma once

#include "core/http_post.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace aurora {

inline constexpr std::string_view kMicrosoftDeviceCodeUrl =
    "https://login.microsoftonline.com/consumers/oauth2/v2.0/devicecode";
inline constexpr std::string_view kMicrosoftTokenUrl =
    "https://login.microsoftonline.com/consumers/oauth2/v2.0/token";
inline constexpr std::string_view kMicrosoftAuthScope = "XboxLive.signin offline_access";
inline constexpr std::string_view kXboxUserAuthUrl =
    "https://user.auth.xboxlive.com/user/authenticate";
inline constexpr std::string_view kXstsAuthorizeUrl =
    "https://xsts.auth.xboxlive.com/xsts/authorize";
inline constexpr std::string_view kMinecraftLoginUrl =
    "https://api.minecraftservices.com/authentication/login_with_xbox";
inline constexpr std::string_view kMinecraftEntitlementsUrl =
    "https://api.minecraftservices.com/entitlements/mcstore";
inline constexpr std::string_view kMinecraftProfileUrl =
    "https://api.minecraftservices.com/minecraft/profile";

struct AuthEndpoints {
    std::string device_code_url{std::string(kMicrosoftDeviceCodeUrl)};
    std::string token_url{std::string(kMicrosoftTokenUrl)};
    std::string xbox_user_auth_url{std::string(kXboxUserAuthUrl)};
    std::string xsts_authorize_url{std::string(kXstsAuthorizeUrl)};
    std::string minecraft_login_url{std::string(kMinecraftLoginUrl)};
    std::string minecraft_entitlements_url{std::string(kMinecraftEntitlementsUrl)};
    std::string minecraft_profile_url{std::string(kMinecraftProfileUrl)};
};

struct DeviceCode {
    std::string device_code;
    std::string user_code;
    std::string verification_uri;
    std::string message;
    int expires_in = 0;
    int interval = 5;
};

struct MicrosoftTokens {
    std::string access_token;
    std::string refresh_token;
    std::string token_type;
    std::string scope;
    int expires_in = 0;
    std::chrono::system_clock::time_point expires_at{};

    bool has_refresh_token() const {
        return !refresh_token.empty();
    }
};

enum class DeviceTokenStatus {
    Success,
    AuthorizationPending,
    SlowDown,
    ExpiredToken,
    AccessDenied,
    TransientError,
};

struct DeviceTokenPoll {
    DeviceTokenStatus status = DeviceTokenStatus::TransientError;
    MicrosoftTokens tokens;
    std::string message;
};

struct DevicePollOptions {
    int max_transient_errors = 3;
    std::function<void(std::chrono::milliseconds)> sleep;
};

struct XboxUserToken {
    std::string token;
    std::string user_hash;
    std::string xuid;
};

struct XstsToken {
    std::string token;
    std::string user_hash;
    std::string xuid;
};

struct MinecraftLogin {
    std::string access_token;
    std::string token_type;
    std::string username;
    int expires_in = 0;
    std::vector<std::string> roles;
};

struct MinecraftProfile {
    std::string id;
    std::string name;
};

struct Entitlements {
    std::vector<std::string> items;
    bool owns_minecraft = false;
};

struct MinecraftSession {
    std::string username;
    std::string uuid;
    std::string access_token;
    std::string user_type = "msa";
    std::string xuid;
    std::chrono::system_clock::time_point expires_at{};

    bool expired(std::chrono::system_clock::time_point now =
                     std::chrono::system_clock::now()) const {
        return expires_at != std::chrono::system_clock::time_point{} && expires_at <= now;
    }
};

struct StoredSession {
    MinecraftSession minecraft;
    std::string refresh_token;
};

// Microsoft/Xbox/Minecraft authentication. The OAuth client ID is deliberately
// not built in: callers must supply the ID of their own Azure application.
class MicrosoftAuth {
public:
    explicit MicrosoftAuth(std::string client_id,
                           std::shared_ptr<HttpTransport> transport = {},
                           AuthEndpoints endpoints = {});

    const std::string& client_id() const {
        return client_id_;
    }

    DeviceCode request_device_code() const;

    // Polls once. This is public so callers and tests can drive the state
    // machine without sleeping.
    DeviceTokenPoll poll_device_code_once(std::string_view device_code) const;
    MicrosoftTokens wait_for_microsoft_token(const DeviceCode& device_code,
                                             const DevicePollOptions& options = {}) const;
    MicrosoftTokens refresh_microsoft_token(std::string_view refresh_token) const;

    XboxUserToken authenticate_xbox_user(std::string_view microsoft_access_token) const;
    XstsToken authorize_xsts(std::string_view xbox_user_token) const;
    MinecraftLogin login_with_xbox(std::string_view user_hash,
                                   std::string_view xsts_token) const;
    Entitlements fetch_entitlements(std::string_view minecraft_access_token) const;
    MinecraftProfile fetch_minecraft_profile(std::string_view minecraft_access_token) const;

    StoredSession complete_login(const MicrosoftTokens& tokens,
                                 bool check_entitlements = true) const;
    StoredSession authenticate_device_code(const DeviceCode& device_code,
                                           const DevicePollOptions& options = {}) const;
    StoredSession refresh_session(const StoredSession& previous,
                                  bool check_entitlements = true) const;

private:
    std::string client_id_;
    std::shared_ptr<HttpTransport> transport_;
    AuthEndpoints endpoints_;
};

// Windows DPAPI (CryptProtectData/CryptUnprotectData) in the current user's
// scope. The encrypted bytes are safe to write to disk; they are not portable
// to another Windows user or machine.
std::vector<std::uint8_t> dpapi_protect(std::string_view plaintext);
std::string dpapi_unprotect(const std::vector<std::uint8_t>& ciphertext);

// Persists the Minecraft session and Microsoft refresh token as a DPAPI
// encrypted binary file. The file path must be under the launcher app-data
// directory and is supplied by the caller.
class AuthStore {
public:
    explicit AuthStore(std::filesystem::path path);

    const std::filesystem::path& path() const {
        return path_;
    }

    bool exists() const;
    std::optional<StoredSession> load() const;
    void save(const StoredSession& session) const;
    void clear() const;

private:
    std::filesystem::path path_;
};

} // namespace aurora
