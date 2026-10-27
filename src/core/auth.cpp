#include "core/auth.hpp"

#include "core/error.hpp"
#include "core/json.hpp"
#include "core/paths.hpp"
#include "core/strings.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <limits>
#include <thread>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dpapi.h>
#endif

namespace aurora {

namespace {

using FormFields = std::vector<std::pair<std::string, std::string>>;
using Clock = std::chrono::system_clock;

struct ErrorParts {
    std::string code;
    std::string description;
};

std::string clip(std::string value, std::size_t limit = 240) {
    value = trim(value);
    if (value.size() > limit) {
        value.resize(limit);
        value += "...";
    }
    return value;
}

std::optional<Json> try_parse_object(std::string_view text) {
    if (text.empty()) {
        return std::nullopt;
    }
    try {
        Json value = Json::parse(text.begin(), text.end());
        if (!value.is_object()) {
            return std::nullopt;
        }
        return value;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

Json parse_object(std::string_view text, std::string_view context) {
    Json value = parse_json(text, context);
    if (!value.is_object()) {
        fail("Invalid JSON object in " + std::string(context));
    }
    return value;
}

ErrorParts error_parts(const HttpResponse& response) {
    const auto value = try_parse_object(response.body);
    if (!value) {
        return {};
    }
    ErrorParts parts;
    parts.code = json_string(*value, "error");
    if (parts.code.empty()) {
        parts.code = json_string(*value, "errorType");
    }
    parts.description = json_string(*value, "error_description");
    if (parts.description.empty()) {
        parts.description = json_string(*value, "errorMessage");
    }
    if (parts.description.empty()) {
        parts.description = json_string(*value, "Message");
    }
    return parts;
}

[[noreturn]] void fail_http(std::string_view context, const HttpResponse& response) {
    const ErrorParts parts = error_parts(response);
    std::string message(context);
    message += " failed (HTTP " + std::to_string(response.status) + ")";
    if (!parts.code.empty() || !parts.description.empty()) {
        message += ": ";
        if (!parts.code.empty()) {
            message += parts.code;
        }
        if (!parts.description.empty()) {
            if (!parts.code.empty()) {
                message += " - ";
            }
            message += clip(parts.description);
        }
    }
    fail(message);
}

[[noreturn]] void fail_oauth(std::string_view context,
                             const HttpResponse& response,
                             bool refreshing) {
    const ErrorParts parts = error_parts(response);
    std::string message(context);
    message += " failed (HTTP " + std::to_string(response.status) + ")";
    if (refreshing && parts.code == "invalid_grant") {
        message += ": the Microsoft refresh token is no longer valid; sign in again";
    } else if (parts.code == "invalid_client" || parts.code == "unauthorized_client") {
        message +=
            ": the configured Azure application client ID was rejected; verify that it is a "
            "public client and allows device-code sign-in";
    } else if (parts.code == "invalid_scope") {
        message +=
            ": the Azure application is not allowed to request XboxLive.signin and offline_access";
    } else if (!parts.code.empty() || !parts.description.empty()) {
        message += ": ";
        if (!parts.code.empty()) {
            message += parts.code;
        }
        if (!parts.description.empty()) {
            if (!parts.code.empty()) {
                message += " - ";
            }
            message += clip(parts.description);
        }
    }
    fail(message);
}

[[noreturn]] void fail_xbox(std::string_view context, const HttpResponse& response) {
    const auto value = try_parse_object(response.body);
    if (value) {
        const std::int64_t xerr = json_integer(*value, "XErr");
        if (xerr != 0) {
            std::string message;
            switch (xerr) {
            case 2148916233:
                message =
                    "This Microsoft account has no Xbox profile. Create one at "
                    "https://www.xbox.com/live, then sign in again.";
                break;
            case 2148916235:
                message = "Xbox Live is not available in this account's country or region.";
                break;
            case 2148916238:
                message =
                    "This is a child account. An adult must add it to a Microsoft family "
                    "group at https://account.microsoft.com/family, then sign in again.";
                break;
            default:
                message = "Xbox Live authorization failed (XErr " + std::to_string(xerr) + ")";
                break;
            }
            const std::string detail = json_string(*value, "Message");
            if (!detail.empty()) {
                message += " (" + clip(detail) + ")";
            }
            fail(message);
        }
    }
    fail_http(context, response);
}

MicrosoftTokens parse_microsoft_tokens(std::string_view body) {
    const Json value = parse_object(body, "Microsoft token response");
    MicrosoftTokens tokens;
    tokens.access_token = json_string(value, "access_token");
    if (tokens.access_token.empty()) {
        fail("Microsoft token response did not include an access token");
    }
    tokens.refresh_token = json_string(value, "refresh_token");
    tokens.token_type = json_string(value, "token_type");
    tokens.scope = json_string(value, "scope");
    const std::int64_t expires_in = json_integer(value, "expires_in");
    if (expires_in > 0) {
        const std::int64_t bounded = std::min<std::int64_t>(
            expires_in, 365LL * 24 * 60 * 60);
        tokens.expires_in = static_cast<int>(bounded);
        tokens.expires_at = Clock::now() + std::chrono::seconds(bounded);
    }
    return tokens;
}

struct XboxClaims {
    std::string token;
    std::string user_hash;
    std::string xuid;
};

XboxClaims parse_xbox_claims(std::string_view body, std::string_view context) {
    const Json value = parse_object(body, context);
    XboxClaims claims;
    claims.token = json_string(value, "Token");
    if (const Json* display_claims = json_object(value, "DisplayClaims")) {
        if (const Json* xui = json_array(*display_claims, "xui");
            xui != nullptr && !xui->empty() && (*xui)[0].is_object()) {
            claims.user_hash = json_string((*xui)[0], "uhs");
            claims.xuid = json_string((*xui)[0], "xid");
        }
    }
    if (claims.token.empty() || claims.user_hash.empty()) {
        fail(std::string(context) + " did not include an Xbox token and user hash");
    }
    return claims;
}

std::vector<HttpHeader> json_headers() {
    return {
        {"Content-Type", "application/json"},
        {"Accept", "application/json"},
    };
}

std::vector<HttpHeader> xbox_headers() {
    std::vector<HttpHeader> headers = json_headers();
    headers.push_back({"x-xbl-contract-version", "1"});
    return headers;
}

std::vector<HttpHeader> form_headers() {
    return {
        {"Content-Type", "application/x-www-form-urlencoded"},
        {"Accept", "application/json"},
    };
}

std::vector<HttpHeader> bearer_headers(std::string_view access_token) {
    return {
        {"Authorization", "Bearer " + std::string(access_token)},
        {"Accept", "application/json"},
    };
}

HttpRequest json_post(std::string_view url,
                      std::string body,
                      std::vector<HttpHeader> headers) {
    HttpRequest request;
    request.method = "POST";
    request.url = std::string(url);
    request.body = std::move(body);
    request.headers = std::move(headers);
    return request;
}

HttpRequest form_post(std::string_view url, const FormFields& fields) {
    HttpRequest request;
    request.method = "POST";
    request.url = std::string(url);
    request.body = form_encode(fields);
    request.headers = form_headers();
    return request;
}

HttpRequest get_json(std::string_view url, std::string_view access_token) {
    HttpRequest request;
    request.method = "GET";
    request.url = std::string(url);
    request.headers = bearer_headers(access_token);
    return request;
}

std::int64_t to_unix_seconds(Clock::time_point value) {
    if (value == Clock::time_point{}) {
        return 0;
    }
    return std::chrono::duration_cast<std::chrono::seconds>(value.time_since_epoch()).count();
}

Clock::time_point from_unix_seconds(std::int64_t value) {
    if (value <= 0) {
        return Clock::time_point{};
    }
    return Clock::time_point{} + std::chrono::seconds(value);
}

std::vector<std::uint8_t> read_binary_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        fail("Unable to open authentication data: " + path_to_utf8(path));
    }
    const std::istreambuf_iterator<char> begin(input);
    const std::istreambuf_iterator<char> end;
    std::vector<std::uint8_t> data(begin, end);
    if (input.bad()) {
        fail("Unable to read authentication data: " + path_to_utf8(path));
    }
    return data;
}

void write_binary_file(const std::filesystem::path& path,
                       const std::vector<std::uint8_t>& data) {
    ensure_directory(path.parent_path());
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            fail("Unable to open authentication data for writing: " + path_to_utf8(path));
        }
        output.write(
            reinterpret_cast<const char*>(data.data()),
            static_cast<std::streamsize>(data.size()));
        output.flush();
        if (!output) {
            output.close();
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            fail("Unable to write authentication data: " + path_to_utf8(path));
        }
    }

    std::error_code ec;
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        std::filesystem::remove(path, ec);
        ec.clear();
        std::filesystem::rename(temporary, path, ec);
        if (ec) {
            std::filesystem::remove(temporary);
            fail("Unable to replace authentication data: " + path_to_utf8(path) +
                 " (" + ec.message() + ")");
        }
    }
}

#ifdef _WIN32
constexpr std::string_view kDpapiEntropy = "AuroraLauncher.Authentication.v1";
#endif

} // namespace

MicrosoftAuth::MicrosoftAuth(std::string client_id,
                             std::shared_ptr<HttpTransport> transport,
                             AuthEndpoints endpoints)
    : client_id_(trim(client_id)),
      transport_(std::move(transport)),
      endpoints_(std::move(endpoints)) {
    if (client_id_.empty()) {
        fail(
            "Microsoft OAuth client ID is not configured; supply the client ID of your own "
            "Azure application");
    }
    if (endpoints_.device_code_url.empty() || endpoints_.token_url.empty() ||
        endpoints_.xbox_user_auth_url.empty() || endpoints_.xsts_authorize_url.empty() ||
        endpoints_.minecraft_login_url.empty() ||
        endpoints_.minecraft_entitlements_url.empty() ||
        endpoints_.minecraft_profile_url.empty()) {
        fail("Microsoft authentication endpoint configuration is incomplete");
    }
    if (!transport_) {
        transport_ = default_http_transport();
    }
}

DeviceCode MicrosoftAuth::request_device_code() const {
    const HttpResponse response = transport_->send(form_post(
        endpoints_.device_code_url,
        {
            {"client_id", client_id_},
            {"scope", std::string(kMicrosoftAuthScope)},
        }));
    if (response.status != 200) {
        fail_oauth("Microsoft device-code request", response, false);
    }

    const Json value = parse_object(response.body, "Microsoft device-code response");
    DeviceCode code;
    code.device_code = json_string(value, "device_code");
    code.user_code = json_string(value, "user_code");
    code.verification_uri = json_string(value, "verification_uri");
    if (code.verification_uri.empty()) {
        code.verification_uri = json_string(value, "verification_url");
    }
    code.message = json_string(value, "message");

    const std::int64_t expires_in = json_integer(value, "expires_in");
    const std::int64_t interval = json_integer(value, "interval", 5);
    if (code.device_code.empty() || code.user_code.empty() ||
        code.verification_uri.empty() || expires_in <= 0) {
        fail("Microsoft device-code response is missing required fields");
    }
    code.expires_in = static_cast<int>(
        std::min<std::int64_t>(expires_in, 7 * 24 * 60 * 60));
    code.interval = interval > 0
        ? static_cast<int>(std::min<std::int64_t>(interval, 3600))
        : 5;
    return code;
}

DeviceTokenPoll MicrosoftAuth::poll_device_code_once(std::string_view device_code) const {
    if (device_code.empty()) {
        fail("Microsoft device code is empty");
    }

    HttpResponse response;
    try {
        response = transport_->send(form_post(
            endpoints_.token_url,
            {
                {"grant_type", "urn:ietf:params:oauth:grant-type:device_code"},
                {"client_id", client_id_},
                {"device_code", std::string(device_code)},
            }));
    } catch (const std::exception& error) {
        DeviceTokenPoll poll;
        poll.status = DeviceTokenStatus::TransientError;
        poll.message = clip(error.what());
        return poll;
    }

    if (response.status == 200) {
        DeviceTokenPoll poll;
        poll.status = DeviceTokenStatus::Success;
        poll.tokens = parse_microsoft_tokens(response.body);
        return poll;
    }

    const ErrorParts parts = error_parts(response);
    DeviceTokenStatus status = DeviceTokenStatus::TransientError;
    if (parts.code == "authorization_pending") {
        status = DeviceTokenStatus::AuthorizationPending;
    } else if (parts.code == "slow_down") {
        status = DeviceTokenStatus::SlowDown;
    } else if (parts.code == "expired_token") {
        status = DeviceTokenStatus::ExpiredToken;
    } else if (parts.code == "access_denied") {
        status = DeviceTokenStatus::AccessDenied;
    } else if (response.status == 0) {
        status = DeviceTokenStatus::TransientError;
    } else if (response.status == 408 || response.status == 429 ||
               response.status >= 500) {
        status = DeviceTokenStatus::TransientError;
    } else {
        fail_oauth("Microsoft device-code token polling", response, false);
    }

    DeviceTokenPoll poll;
    poll.status = status;
    if (status == DeviceTokenStatus::TransientError) {
        poll.message = "HTTP " + std::to_string(response.status);
    }
    return poll;
}

MicrosoftTokens MicrosoftAuth::wait_for_microsoft_token(
    const DeviceCode& device_code,
    const DevicePollOptions& options) const {
    if (device_code.device_code.empty()) {
        fail("Microsoft device code is empty");
    }

    const int expires_in = device_code.expires_in > 0 ? device_code.expires_in : 900;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(expires_in);
    auto interval = std::chrono::milliseconds(
        static_cast<long long>(std::max(1, device_code.interval)) * 1000);
    const int maximum_transient_errors = std::max(0, options.max_transient_errors);
    int transient_errors = 0;

    std::function<void(std::chrono::milliseconds)> sleep = options.sleep;
    if (!sleep) {
        sleep = [](std::chrono::milliseconds duration) {
            std::this_thread::sleep_for(duration);
        };
    }

    while (true) {
        if (std::chrono::steady_clock::now() >= deadline) {
            fail("Microsoft device code expired before sign-in completed; start again");
        }

        const DeviceTokenPoll poll = poll_device_code_once(device_code.device_code);
        switch (poll.status) {
        case DeviceTokenStatus::Success:
            return poll.tokens;
        case DeviceTokenStatus::AuthorizationPending:
            transient_errors = 0;
            break;
        case DeviceTokenStatus::SlowDown:
            transient_errors = 0;
            interval += std::chrono::seconds(5);
            break;
        case DeviceTokenStatus::ExpiredToken:
            fail("Microsoft device code expired; start sign-in again");
        case DeviceTokenStatus::AccessDenied:
            fail("Microsoft sign-in was cancelled or access was denied");
        case DeviceTokenStatus::TransientError:
            ++transient_errors;
            if (transient_errors > maximum_transient_errors) {
                std::string message =
                    "Microsoft sign-in polling failed after repeated network or server errors";
                if (!poll.message.empty()) {
                    message += ": " + poll.message;
                }
                fail(message);
            }
            break;
        }
        sleep(interval);
    }
}

MicrosoftTokens MicrosoftAuth::refresh_microsoft_token(std::string_view refresh_token) const {
    if (refresh_token.empty()) {
        fail("No Microsoft refresh token is available; sign in again");
    }

    const HttpResponse response = transport_->send(form_post(
        endpoints_.token_url,
        {
            {"grant_type", "refresh_token"},
            {"client_id", client_id_},
            {"refresh_token", std::string(refresh_token)},
            {"scope", std::string(kMicrosoftAuthScope)},
        }));
    if (response.status != 200) {
        fail_oauth("Microsoft token refresh", response, true);
    }
    return parse_microsoft_tokens(response.body);
}

XboxUserToken MicrosoftAuth::authenticate_xbox_user(
    std::string_view microsoft_access_token) const {
    if (microsoft_access_token.empty()) {
        fail("Microsoft access token is empty; sign in again");
    }

    Json properties = Json::object();
    properties["AuthMethod"] = "RPS";
    properties["SiteName"] = "user.auth.xboxlive.com";
    properties["RpsTicket"] = "d=" + std::string(microsoft_access_token);

    Json body = Json::object();
    body["Properties"] = std::move(properties);
    body["RelyingParty"] = "http://auth.xboxlive.com";
    body["TokenType"] = "JWT";

    const HttpResponse response = transport_->send(json_post(
        endpoints_.xbox_user_auth_url, body.dump(), xbox_headers()));
    if (response.status != 200) {
        fail_xbox("Xbox Live user authentication", response);
    }

    const XboxClaims claims =
        parse_xbox_claims(response.body, "Xbox Live user authentication");
    return XboxUserToken{claims.token, claims.user_hash, claims.xuid};
}

XstsToken MicrosoftAuth::authorize_xsts(std::string_view xbox_user_token) const {
    if (xbox_user_token.empty()) {
        fail("Xbox Live user token is empty; sign in again");
    }

    Json properties = Json::object();
    properties["SandboxId"] = "RETAIL";
    properties["UserTokens"] = Json::array({std::string(xbox_user_token)});

    Json body = Json::object();
    body["Properties"] = std::move(properties);
    body["RelyingParty"] = "rp://api.minecraftservices.com/";
    body["TokenType"] = "JWT";

    const HttpResponse response = transport_->send(json_post(
        endpoints_.xsts_authorize_url, body.dump(), xbox_headers()));
    if (response.status != 200) {
        fail_xbox("Xbox Live XSTS authorization", response);
    }

    const XboxClaims claims =
        parse_xbox_claims(response.body, "Xbox Live XSTS authorization");
    return XstsToken{claims.token, claims.user_hash, claims.xuid};
}

MinecraftLogin MicrosoftAuth::login_with_xbox(std::string_view user_hash,
                                              std::string_view xsts_token) const {
    if (user_hash.empty() || xsts_token.empty()) {
        fail("Xbox user hash or XSTS token is empty; sign in again");
    }

    Json body = Json::object();
    body["identityToken"] =
        "XBL3.0 x=" + std::string(user_hash) + ";" + std::string(xsts_token);

    const HttpResponse response = transport_->send(json_post(
        endpoints_.minecraft_login_url, body.dump(), json_headers()));
    if (response.status == 401) {
        fail("Minecraft login was rejected (HTTP 401); sign in again");
    }
    if (response.status != 200) {
        fail_http("Minecraft login", response);
    }

    const Json value = parse_object(response.body, "Minecraft login response");
    MinecraftLogin login;
    login.access_token = json_string(value, "access_token");
    if (login.access_token.empty()) {
        fail("Minecraft login response did not include an access token");
    }
    login.token_type = json_string(value, "token_type");
    login.username = json_string(value, "username");
    const std::int64_t expires_in = json_integer(value, "expires_in");
    if (expires_in > 0) {
        login.expires_in = static_cast<int>(
            std::min<std::int64_t>(expires_in, std::numeric_limits<int>::max()));
    }
    if (const Json* roles = json_array(value, "roles"); roles != nullptr) {
        for (const Json& role : *roles) {
            if (role.is_string()) {
                login.roles.push_back(role.get<std::string>());
            }
        }
    }
    return login;
}

Entitlements MicrosoftAuth::fetch_entitlements(
    std::string_view minecraft_access_token) const {
    if (minecraft_access_token.empty()) {
        fail("Minecraft access token is empty; sign in again");
    }

    const HttpResponse response = transport_->send(
        get_json(endpoints_.minecraft_entitlements_url, minecraft_access_token));
    if (response.status != 200) {
        fail_http("Minecraft entitlement check", response);
    }

    const Json value = parse_object(response.body, "Minecraft entitlement response");
    Entitlements entitlements;
    const auto items = value.find("items");
    if (items != value.end()) {
        if (!items->is_array()) {
            fail("Minecraft entitlement response has an invalid items list");
        }
        for (const Json& item : *items) {
            if (item.is_object()) {
                entitlements.items.push_back(json_string(item, "name"));
            } else {
                entitlements.items.emplace_back();
            }
        }
    }
    entitlements.owns_minecraft = !entitlements.items.empty();
    return entitlements;
}

MinecraftProfile MicrosoftAuth::fetch_minecraft_profile(
    std::string_view minecraft_access_token) const {
    if (minecraft_access_token.empty()) {
        fail("Minecraft access token is empty; sign in again");
    }

    const HttpResponse response = transport_->send(
        get_json(endpoints_.minecraft_profile_url, minecraft_access_token));
    if (response.status == 404) {
        fail(
            "This account does not have a Minecraft profile; verify that Minecraft: Java "
            "Edition is owned by the account");
    }
    if (response.status != 200) {
        fail_http("Minecraft profile request", response);
    }

    const Json value = parse_object(response.body, "Minecraft profile response");
    MinecraftProfile profile;
    profile.id = json_string(value, "id");
    profile.name = json_string(value, "name");
    if (profile.id.empty() || profile.name.empty()) {
        fail("Minecraft profile response is missing the account id or name");
    }
    return profile;
}

StoredSession MicrosoftAuth::complete_login(const MicrosoftTokens& tokens,
                                            bool check_entitlements) const {
    if (tokens.access_token.empty()) {
        fail("Microsoft access token is empty; sign in again");
    }

    const XboxUserToken xbox = authenticate_xbox_user(tokens.access_token);
    const XstsToken xsts = authorize_xsts(xbox.token);
    const MinecraftLogin login = login_with_xbox(xsts.user_hash, xsts.token);

    if (check_entitlements) {
        const Entitlements entitlements = fetch_entitlements(login.access_token);
        if (!entitlements.owns_minecraft) {
            fail(
                "This Microsoft account does not own Minecraft: Java Edition (no "
                "entitlement was found)");
        }
    }

    const MinecraftProfile profile = fetch_minecraft_profile(login.access_token);

    MinecraftSession session;
    session.username = profile.name;
    session.uuid = profile.id;
    session.access_token = login.access_token;
    session.user_type = "msa";
    session.xuid = !xsts.xuid.empty() ? xsts.xuid : xbox.xuid;
    if (login.expires_in > 0) {
        session.expires_at = Clock::now() + std::chrono::seconds(login.expires_in);
    }

    StoredSession stored;
    stored.minecraft = std::move(session);
    stored.refresh_token = tokens.refresh_token;
    return stored;
}

StoredSession MicrosoftAuth::authenticate_device_code(
    const DeviceCode& device_code,
    const DevicePollOptions& options) const {
    const MicrosoftTokens tokens = wait_for_microsoft_token(device_code, options);
    return complete_login(tokens);
}

StoredSession MicrosoftAuth::refresh_session(const StoredSession& previous,
                                             bool check_entitlements) const {
    if (previous.refresh_token.empty()) {
        fail("No Microsoft refresh token is available; sign in again");
    }

    MicrosoftTokens tokens = refresh_microsoft_token(previous.refresh_token);
    if (tokens.refresh_token.empty()) {
        tokens.refresh_token = previous.refresh_token;
    }
    StoredSession result = complete_login(tokens, check_entitlements);
    if (result.refresh_token.empty()) {
        result.refresh_token = previous.refresh_token;
    }
    return result;
}

std::vector<std::uint8_t> dpapi_protect(std::string_view plaintext) {
#ifdef _WIN32
    if (plaintext.empty()) {
        fail("Cannot encrypt an empty secret");
    }
    if (plaintext.size() > std::numeric_limits<DWORD>::max()) {
        fail("Authentication secret is too large to encrypt");
    }

    DATA_BLOB input{};
    input.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(plaintext.data()));
    input.cbData = static_cast<DWORD>(plaintext.size());

    DATA_BLOB entropy{};
    entropy.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(kDpapiEntropy.data()));
    entropy.cbData = static_cast<DWORD>(kDpapiEntropy.size());

    DATA_BLOB output{};
    if (!CryptProtectData(
            &input,
            L"AuroraLauncher authentication data",
            &entropy,
            nullptr,
            nullptr,
            CRYPTPROTECT_UI_FORBIDDEN,
            &output)) {
        fail(
            "Unable to encrypt authentication data (Windows error " +
            std::to_string(GetLastError()) + ")");
    }

    std::vector<std::uint8_t> ciphertext(output.pbData, output.pbData + output.cbData);
    LocalFree(output.pbData);
    return ciphertext;
#else
    (void)plaintext;
    fail("DPAPI is only available on Windows");
#endif
}

std::string dpapi_unprotect(const std::vector<std::uint8_t>& ciphertext) {
#ifdef _WIN32
    if (ciphertext.empty()) {
        fail("Encrypted authentication data is empty");
    }
    if (ciphertext.size() > std::numeric_limits<DWORD>::max()) {
        fail("Encrypted authentication data is too large");
    }

    DATA_BLOB input{};
    input.pbData =
        reinterpret_cast<BYTE*>(const_cast<std::uint8_t*>(ciphertext.data()));
    input.cbData = static_cast<DWORD>(ciphertext.size());

    DATA_BLOB entropy{};
    entropy.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(kDpapiEntropy.data()));
    entropy.cbData = static_cast<DWORD>(kDpapiEntropy.size());

    DATA_BLOB output{};
    if (!CryptUnprotectData(
            &input,
            nullptr,
            &entropy,
            nullptr,
            nullptr,
            CRYPTPROTECT_UI_FORBIDDEN,
            &output)) {
        fail(
            "Unable to decrypt authentication data (Windows error " +
            std::to_string(GetLastError()) + "); sign in again");
    }

    std::string plaintext(
        reinterpret_cast<const char*>(output.pbData),
        static_cast<std::size_t>(output.cbData));
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);
    return plaintext;
#else
    (void)ciphertext;
    fail("DPAPI is only available on Windows");
#endif
}

AuthStore::AuthStore(std::filesystem::path path) : path_(std::move(path)) {
    if (path_.empty()) {
        fail("Authentication store path is empty");
    }
}

bool AuthStore::exists() const {
    std::error_code ec;
    return std::filesystem::is_regular_file(path_, ec);
}

std::optional<StoredSession> AuthStore::load() const {
    if (!exists()) {
        return std::nullopt;
    }

    const std::vector<std::uint8_t> ciphertext = read_binary_file(path_);
    const std::string plaintext = dpapi_unprotect(ciphertext);

    Json value;
    try {
        value = parse_json(plaintext, "stored authentication data");
    } catch (const Error&) {
        fail("Stored authentication data is invalid; sign in again");
    }
    if (!value.is_object()) {
        fail("Stored authentication data is invalid; sign in again");
    }
    if (json_integer(value, "version") != 1) {
        fail("Stored authentication data has an unsupported version; sign in again");
    }

    const Json* account = json_object(value, "minecraft");
    if (account == nullptr) {
        fail("Stored authentication data is incomplete; sign in again");
    }

    StoredSession stored;
    stored.minecraft.username = json_string(*account, "username");
    stored.minecraft.uuid = json_string(*account, "uuid");
    stored.minecraft.access_token = json_string(*account, "accessToken");
    stored.minecraft.user_type = json_string(*account, "userType", "msa");
    stored.minecraft.xuid = json_string(*account, "xuid");
    stored.minecraft.expires_at =
        from_unix_seconds(json_integer(*account, "expiresAt"));
    stored.refresh_token = json_string(value, "refreshToken");

    if (stored.minecraft.username.empty() || stored.minecraft.uuid.empty() ||
        stored.minecraft.access_token.empty()) {
        fail("Stored authentication data is incomplete; sign in again");
    }
    if (stored.minecraft.user_type.empty()) {
        stored.minecraft.user_type = "msa";
    }
    return stored;
}

void AuthStore::save(const StoredSession& session) const {
    if (session.minecraft.username.empty() || session.minecraft.uuid.empty() ||
        session.minecraft.access_token.empty()) {
        fail("Cannot store an incomplete Minecraft session");
    }

    Json account = Json::object();
    account["username"] = session.minecraft.username;
    account["uuid"] = session.minecraft.uuid;
    account["accessToken"] = session.minecraft.access_token;
    account["userType"] = session.minecraft.user_type;
    account["xuid"] = session.minecraft.xuid;
    account["expiresAt"] = to_unix_seconds(session.minecraft.expires_at);

    Json value = Json::object();
    value["version"] = 1;
    value["minecraft"] = std::move(account);
    value["refreshToken"] = session.refresh_token;

    write_binary_file(path_, dpapi_protect(value.dump()));
}

void AuthStore::clear() const {
    std::error_code ec;
    std::filesystem::remove(path_, ec);
    if (ec) {
        fail("Unable to clear stored authentication data: " + ec.message());
    }
}

} // namespace aurora
