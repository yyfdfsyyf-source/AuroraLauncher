#include "core/auth.hpp"
#include "core/error.hpp"
#include "core/http_post.hpp"
#include "core/json.hpp"
#include "core/strings.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

template <typename Function>
void run_test(const char* name, Function&& function) {
    try {
        function();
    } catch (const std::exception& error) {
        expect(false, std::string(name) + " threw an exception: " + error.what());
    }
}

#ifdef _WIN32
std::filesystem::path make_temp_dir(const std::string& name) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
                      ("aurora-auth-tests-" + name + "-" + std::to_string(stamp));
    std::filesystem::create_directories(path);
    return path;
}
#endif

struct MockReply {
    int status = 200;
    std::string body;
    bool transport_error = false;
};

class MockTransport final : public aurora::HttpTransport {
public:
    aurora::HttpResponse send(const aurora::HttpRequest& request) override {
        requests.push_back(request);
        if (next >= replies.size()) {
            throw aurora::Error("unexpected mock HTTP request");
        }
        const MockReply reply = replies[next++];
        if (reply.transport_error) {
            throw aurora::Error("simulated network error");
        }
        return aurora::HttpResponse{reply.status, reply.body};
    }

    std::vector<MockReply> replies;
    std::vector<aurora::HttpRequest> requests;
    std::size_t next = 0;
};

std::string raw_form_value(std::string_view body, std::string_view key) {
    for (const std::string& field : aurora::split(body, '&')) {
        const std::size_t equals = field.find('=');
        if (equals != std::string::npos &&
            std::string_view(field).substr(0, equals) == key) {
            return field.substr(equals + 1);
        }
    }
    return {};
}

bool has_header(const aurora::HttpRequest& request,
                std::string_view name,
                std::string_view value) {
    for (const aurora::HttpHeader& header : request.headers) {
        if (aurora::iequals(header.name, name) && header.value == value) {
            return true;
        }
    }
    return false;
}

constexpr std::string_view kDeviceCodeResponse = R"({
  "user_code": "ABCD-EFGH",
  "device_code": "device-code-secret",
  "verification_uri": "https://microsoft.com/devicelogin",
  "expires_in": 900,
  "interval": 5,
  "message": "Use a browser to sign in"
})";

constexpr std::string_view kTokenResponse = R"({
  "token_type": "Bearer",
  "scope": "XboxLive.signin offline_access",
  "expires_in": 3600,
  "access_token": "msa-access-token",
  "refresh_token": "msa-refresh-token"
})";

constexpr std::string_view kRotatedTokenResponse = R"({
  "token_type": "Bearer",
  "scope": "XboxLive.signin offline_access",
  "expires_in": 3600,
  "access_token": "new-msa-access-token",
  "refresh_token": "rotated-refresh-token"
})";

constexpr std::string_view kXboxUserResponse = R"({
  "IssueInstant": "2026-01-01T00:00:00.0000000Z",
  "NotAfter": "2026-01-21T00:00:00.0000000Z",
  "Token": "xbox-user-token",
  "DisplayClaims": {
    "xui": [
      { "uhs": "user-hash" }
    ]
  }
})";

constexpr std::string_view kXstsResponse = R"({
  "IssueInstant": "2026-01-01T00:00:00.0000000Z",
  "NotAfter": "2026-01-21T00:00:00.0000000Z",
  "Token": "xsts-token",
  "DisplayClaims": {
    "xui": [
      { "uhs": "user-hash", "xid": "123456789", "gtg": "user-hash" }
    ]
  }
})";

constexpr std::string_view kMinecraftLoginResponse = R"({
  "username": "Steve",
  "roles": [],
  "access_token": "minecraft-access-token",
  "token_type": "Bearer",
  "expires_in": 86400
})";

constexpr std::string_view kEntitlementsResponse = R"({
  "items": [
    { "name": "product_minecraft", "signature": "sig", "keyId": "key" }
  ],
  "signature": "sig",
  "keyId": "key"
})";

constexpr std::string_view kEmptyEntitlementsResponse = R"({
  "items": [],
  "signature": "",
  "keyId": ""
})";

constexpr std::string_view kProfileResponse = R"({
  "id": "069a79f444e94726a5befca90e38aaf5",
  "name": "Steve",
  "skins": [],
  "capes": []
})";

aurora::Json request_json(const MockTransport& transport, std::size_t index) {
    return aurora::parse_json(transport.requests.at(index).body, "mock request");
}

void test_form_encoding() {
    expect(
        aurora::form_url_encode("XboxLive.signin offline_access") ==
            "XboxLive.signin%20offline_access",
        "form encoding spaces");
    expect(
        aurora::form_url_encode("a+b/c?d=e") == "a%2Bb%2Fc%3Fd%3De",
        "form encoding reserved characters");
    expect(
        aurora::form_encode({{"a", "b c"}, {"d", "e/f"}}) == "a=b%20c&d=e%2Ff",
        "form field assembly");
}

void test_device_code_parsing() {
    auto transport = std::make_shared<MockTransport>();
    transport->replies.push_back({200, std::string(kDeviceCodeResponse)});

    aurora::MicrosoftAuth auth("test-client-id", transport);
    const aurora::DeviceCode code = auth.request_device_code();

    expect(code.user_code == "ABCD-EFGH", "device code user code");
    expect(code.device_code == "device-code-secret", "device code value");
    expect(
        code.verification_uri == "https://microsoft.com/devicelogin",
        "device code verification URI");
    expect(code.expires_in == 900, "device code expiry");
    expect(code.interval == 5, "device code interval");
    expect(
        code.message == "Use a browser to sign in",
        "device code user message");
    expect(transport->requests.size() == 1, "device code made one request");

    const aurora::HttpRequest& request = transport->requests[0];
    expect(request.method == "POST", "device code uses POST");
    expect(request.url == aurora::kMicrosoftDeviceCodeUrl, "device code URL");
    expect(
        has_header(request, "Content-Type", "application/x-www-form-urlencoded"),
        "device code form content type");
    expect(
        raw_form_value(request.body, "client_id") == "test-client-id",
        "device code client id is caller supplied");
    expect(
        raw_form_value(request.body, "scope") ==
            "XboxLive.signin%20offline_access",
        "device code scopes");

    bool empty_client_id_threw = false;
    try {
        aurora::MicrosoftAuth invalid("", transport);
    } catch (const aurora::Error&) {
        empty_client_id_threw = true;
    }
    expect(empty_client_id_threw, "empty client id is rejected");

    auto error_transport = std::make_shared<MockTransport>();
    error_transport->replies.push_back(
        {401, R"({"error":"invalid_client","error_description":"bad client"})"});
    aurora::MicrosoftAuth error_auth("test-client-id", error_transport);
    std::string message;
    try {
        error_auth.request_device_code();
    } catch (const aurora::Error& error) {
        message = error.what();
    }
    expect(
        message.find("Azure application client ID") != std::string::npos,
        "invalid client gives an actionable error");
}

void test_device_polling_states() {
    auto transport = std::make_shared<MockTransport>();
    transport->replies = {
        {400, R"({"error":"authorization_pending"})"},
        {400, R"({"error":"slow_down"})"},
        {400, R"({"error":"expired_token"})"},
        {400, R"({"error":"access_denied"})"},
        {503, "{}"},
        {408, "{}"},
        {429, "{}"},
        {0, "{}", true},
        {200, std::string(kTokenResponse)},
    };

    aurora::MicrosoftAuth auth("test-client-id", transport);
    expect(
        auth.poll_device_code_once("device-code-secret").status ==
            aurora::DeviceTokenStatus::AuthorizationPending,
        "poll authorization_pending");
    expect(
        auth.poll_device_code_once("device-code-secret").status ==
            aurora::DeviceTokenStatus::SlowDown,
        "poll slow_down");
    expect(
        auth.poll_device_code_once("device-code-secret").status ==
            aurora::DeviceTokenStatus::ExpiredToken,
        "poll expired_token");
    expect(
        auth.poll_device_code_once("device-code-secret").status ==
            aurora::DeviceTokenStatus::AccessDenied,
        "poll access_denied");
    expect(
        auth.poll_device_code_once("device-code-secret").status ==
            aurora::DeviceTokenStatus::TransientError,
        "poll HTTP 503 is transient");
    expect(
        auth.poll_device_code_once("device-code-secret").status ==
            aurora::DeviceTokenStatus::TransientError,
        "poll HTTP 408 is transient");
    expect(
        auth.poll_device_code_once("device-code-secret").status ==
            aurora::DeviceTokenStatus::TransientError,
        "poll HTTP 429 is transient");
    expect(
        auth.poll_device_code_once("device-code-secret").status ==
            aurora::DeviceTokenStatus::TransientError,
        "poll network failure is transient");

    const aurora::DeviceTokenPoll success =
        auth.poll_device_code_once("device-code-secret");
    expect(
        success.status == aurora::DeviceTokenStatus::Success,
        "poll success status");
    expect(
        success.tokens.access_token == "msa-access-token",
        "poll success access token");
    expect(
        success.tokens.refresh_token == "msa-refresh-token",
        "poll success refresh token");
    expect(
        success.tokens.expires_at >
            std::chrono::system_clock::now() + std::chrono::seconds(3000),
        "poll success expiry");

    expect(transport->requests.size() == 9, "poll state request count");
    expect(
        raw_form_value(transport->requests[0].body, "grant_type") ==
            "urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Adevice_code",
        "device poll grant type");
    expect(
        raw_form_value(transport->requests[0].body, "device_code") ==
            "device-code-secret",
        "device poll device code");
}

void test_device_poll_wait_sequence() {
    auto transport = std::make_shared<MockTransport>();
    transport->replies = {
        {400, R"({"error":"authorization_pending"})"},
        {400, R"({"error":"slow_down"})"},
        {200, std::string(kTokenResponse)},
    };

    aurora::MicrosoftAuth auth("test-client-id", transport);
    aurora::DeviceCode code;
    code.device_code = "device-code-secret";
    code.expires_in = 900;
    code.interval = 1;

    std::vector<std::chrono::milliseconds> sleeps;
    aurora::DevicePollOptions options;
    options.sleep = [&sleeps](std::chrono::milliseconds duration) {
        sleeps.push_back(duration);
    };

    const aurora::MicrosoftTokens tokens = auth.wait_for_microsoft_token(code, options);
    expect(tokens.access_token == "msa-access-token", "wait returns tokens");
    expect(transport->requests.size() == 3, "wait request count");
    expect(sleeps.size() == 2, "wait sleeps between polls");
    expect(sleeps[0] == std::chrono::milliseconds(1000), "wait initial interval");
    expect(
        sleeps[1] == std::chrono::milliseconds(6000),
        "wait slow_down increases interval");

    auto retry_transport = std::make_shared<MockTransport>();
    retry_transport->replies = {
        {0, "{}", true},
        {400, R"({"error":"authorization_pending"})"},
        {503, "{}"},
        {200, std::string(kTokenResponse)},
    };
    aurora::MicrosoftAuth retry_auth("test-client-id", retry_transport);
    aurora::DevicePollOptions retry_options;
    retry_options.max_transient_errors = 1;
    retry_options.sleep = [](std::chrono::milliseconds) {};
    const aurora::MicrosoftTokens retried =
        retry_auth.wait_for_microsoft_token(code, retry_options);
    expect(
        retried.access_token == "msa-access-token",
        "wait retries transient errors");
    expect(
        retry_transport->requests.size() == 4,
        "wait resets transient errors after a normal pending poll");

    auto failing_transport = std::make_shared<MockTransport>();
    failing_transport->replies = {
        {0, "{}", true},
        {0, "{}", true},
    };
    aurora::MicrosoftAuth failing_auth("test-client-id", failing_transport);
    aurora::DevicePollOptions failing_options;
    failing_options.max_transient_errors = 1;
    failing_options.sleep = [](std::chrono::milliseconds) {};
    std::string message;
    try {
        failing_auth.wait_for_microsoft_token(code, failing_options);
    } catch (const aurora::Error& error) {
        message = error.what();
    }
    expect(
        message.find("repeated network or server errors") != std::string::npos,
        "wait gives up after transient error limit");
}

void test_device_poll_terminal_failures() {
    struct Case {
        const char* body;
        std::string_view expected;
    };
    const Case cases[] = {
        {R"({"error":"expired_token"})", "expired"},
        {R"({"error":"access_denied"})", "cancelled"},
    };

    for (const Case& test_case : cases) {
        auto transport = std::make_shared<MockTransport>();
        transport->replies.push_back({400, test_case.body});
        aurora::MicrosoftAuth auth("test-client-id", transport);
        aurora::DeviceCode code;
        code.device_code = "device-code-secret";
        code.expires_in = 900;
        code.interval = 1;

        aurora::DevicePollOptions options;
        options.sleep = [](std::chrono::milliseconds) {};

        std::string message;
        try {
            auth.wait_for_microsoft_token(code, options);
        } catch (const aurora::Error& error) {
            message = error.what();
        }
        expect(
            message.find(test_case.expected) != std::string::npos,
            "terminal poll state is reported");
        expect(
            transport->requests.size() == 1,
            "terminal poll state stops polling");
    }
}

void test_full_chain() {
    auto transport = std::make_shared<MockTransport>();
    transport->replies = {
        {200, std::string(kXboxUserResponse)},
        {200, std::string(kXstsResponse)},
        {200, std::string(kMinecraftLoginResponse)},
        {200, std::string(kEntitlementsResponse)},
        {200, std::string(kProfileResponse)},
    };

    aurora::MicrosoftAuth auth("test-client-id", transport);
    aurora::MicrosoftTokens microsoft;
    microsoft.access_token = "msa-access-token";
    microsoft.refresh_token = "msa-refresh-token";

    const aurora::StoredSession stored = auth.complete_login(microsoft);
    expect(stored.minecraft.username == "Steve", "chain username from profile");
    expect(
        stored.minecraft.uuid == "069a79f444e94726a5befca90e38aaf5",
        "chain uuid from profile");
    expect(
        stored.minecraft.access_token == "minecraft-access-token",
        "chain minecraft access token");
    expect(stored.minecraft.user_type == "msa", "chain user type");
    expect(stored.minecraft.xuid == "123456789", "chain xuid from XSTS");
    expect(!stored.minecraft.expired(), "chain session is not expired");
    expect(
        stored.refresh_token == "msa-refresh-token",
        "chain keeps refresh token");

    expect(transport->requests.size() == 5, "chain request count");

    const aurora::Json user_auth = request_json(*transport, 0);
    const aurora::Json* user_properties = aurora::json_object(user_auth, "Properties");
    expect(transport->requests[0].url == aurora::kXboxUserAuthUrl, "Xbox user URL");
    expect(
        user_properties != nullptr &&
            aurora::json_string(*user_properties, "RpsTicket") ==
                "d=msa-access-token",
        "Xbox user RpsTicket");
    expect(
        has_header(transport->requests[0], "x-xbl-contract-version", "1"),
        "Xbox user contract header");

    const aurora::Json xsts = request_json(*transport, 1);
    const aurora::Json* xsts_properties = aurora::json_object(xsts, "Properties");
    expect(transport->requests[1].url == aurora::kXstsAuthorizeUrl, "XSTS URL");
    expect(
        xsts_properties != nullptr &&
            aurora::json_string(*xsts_properties, "SandboxId") == "RETAIL",
        "XSTS sandbox");
    expect(
        xsts_properties != nullptr &&
            aurora::json_array(*xsts_properties, "UserTokens") != nullptr &&
            (*aurora::json_array(*xsts_properties, "UserTokens"))[0] ==
                "xbox-user-token",
        "XSTS user token");

    const aurora::Json minecraft_login = request_json(*transport, 2);
    expect(
        transport->requests[2].url == aurora::kMinecraftLoginUrl,
        "Minecraft login URL");
    expect(
        aurora::json_string(minecraft_login, "identityToken") ==
            "XBL3.0 x=user-hash;xsts-token",
        "Minecraft identity token");

    expect(transport->requests[3].method == "GET", "entitlements uses GET");
    expect(
        transport->requests[3].url == aurora::kMinecraftEntitlementsUrl,
        "entitlements URL");
    expect(
        has_header(
            transport->requests[3],
            "Authorization",
            "Bearer minecraft-access-token"),
        "entitlements bearer token");
    expect(
        has_header(
            transport->requests[4],
            "Authorization",
            "Bearer minecraft-access-token"),
        "profile bearer token");
    expect(
        transport->requests[4].url == aurora::kMinecraftProfileUrl,
        "profile URL");
}

void test_profile_parsing() {
    auto transport = std::make_shared<MockTransport>();
    transport->replies.push_back({200, std::string(kProfileResponse)});

    aurora::MicrosoftAuth auth("test-client-id", transport);
    const aurora::MinecraftProfile profile =
        auth.fetch_minecraft_profile("minecraft-access-token");
    expect(profile.id == "069a79f444e94726a5befca90e38aaf5", "profile id");
    expect(profile.name == "Steve", "profile name");
    expect(
        has_header(
            transport->requests[0],
            "Authorization",
            "Bearer minecraft-access-token"),
        "profile request authorization");
}

void test_entitlements_and_missing_ownership() {
    auto transport = std::make_shared<MockTransport>();
    transport->replies.push_back({200, std::string(kEntitlementsResponse)});

    aurora::MicrosoftAuth auth("test-client-id", transport);
    const aurora::Entitlements entitlements =
        auth.fetch_entitlements("minecraft-access-token");
    expect(entitlements.owns_minecraft, "entitlements ownership");
    expect(
        entitlements.items.size() == 1 &&
            entitlements.items[0] == "product_minecraft",
        "entitlements item name");

    auto empty_transport = std::make_shared<MockTransport>();
    empty_transport->replies = {
        {200, std::string(kXboxUserResponse)},
        {200, std::string(kXstsResponse)},
        {200, std::string(kMinecraftLoginResponse)},
        {200, std::string(kEmptyEntitlementsResponse)},
    };
    aurora::MicrosoftAuth empty_auth("test-client-id", empty_transport);
    aurora::MicrosoftTokens microsoft;
    microsoft.access_token = "msa-access-token";
    std::string message;
    try {
        empty_auth.complete_login(microsoft);
    } catch (const aurora::Error& error) {
        message = error.what();
    }
    expect(
        message.find("does not own Minecraft") != std::string::npos,
        "missing entitlement is actionable");
    expect(
        empty_transport->requests.size() == 4,
        "missing entitlement stops before profile");
}

void test_xsts_errors() {
    struct Case {
        std::int64_t code;
        std::string_view expected;
    };
    const Case cases[] = {
        {2148916233, "Xbox profile"},
        {2148916235, "country or region"},
        {2148916238, "family"},
        {999999999, "XErr 999999999"},
    };

    for (const Case& test_case : cases) {
        auto transport = std::make_shared<MockTransport>();
        transport->replies.push_back(
            {401,
             R"({"Identity":"0","XErr":)" + std::to_string(test_case.code) +
                 R"(,"Message":"details","Redirect":"https://example.invalid/"})"});
        aurora::MicrosoftAuth auth("test-client-id", transport);

        std::string message;
        try {
            auth.authorize_xsts("xbox-user-token");
        } catch (const aurora::Error& error) {
            message = error.what();
        }
        expect(
            message.find(test_case.expected) != std::string::npos,
            "XSTS error " + std::to_string(test_case.code) + " is actionable");
        expect(
            message.find("xbox-user-token") == std::string::npos,
            "XSTS error does not expose the input token");
    }
}

void test_refresh_flow() {
    auto transport = std::make_shared<MockTransport>();
    transport->replies.push_back({200, std::string(kRotatedTokenResponse)});

    aurora::MicrosoftAuth auth("test-client-id", transport);
    const aurora::MicrosoftTokens tokens =
        auth.refresh_microsoft_token("old-refresh-token");
    expect(
        tokens.access_token == "new-msa-access-token",
        "refresh access token");
    expect(
        tokens.refresh_token == "rotated-refresh-token",
        "refresh token rotation");
    expect(
        raw_form_value(transport->requests[0].body, "grant_type") ==
            "refresh_token",
        "refresh grant type");
    expect(
        raw_form_value(transport->requests[0].body, "refresh_token") ==
            "old-refresh-token",
        "refresh uses supplied token");
    expect(
        raw_form_value(transport->requests[0].body, "client_id") ==
            "test-client-id",
        "refresh client id");

    auto invalid_transport = std::make_shared<MockTransport>();
    invalid_transport->replies.push_back(
        {400, R"({"error":"invalid_grant","error_description":"expired"})"});
    aurora::MicrosoftAuth invalid_auth("test-client-id", invalid_transport);
    std::string message;
    try {
        invalid_auth.refresh_microsoft_token("old-refresh-token");
    } catch (const aurora::Error& error) {
        message = error.what();
    }
    expect(
        message.find("no longer valid") != std::string::npos &&
            message.find("sign in again") != std::string::npos,
        "invalid refresh token is actionable");

    auto session_transport = std::make_shared<MockTransport>();
    session_transport->replies = {
        {200, R"({"access_token":"refreshed-msa-token","expires_in":3600})"},
        {200, std::string(kXboxUserResponse)},
        {200, std::string(kXstsResponse)},
        {200, std::string(kMinecraftLoginResponse)},
        {200, std::string(kEntitlementsResponse)},
        {200, std::string(kProfileResponse)},
    };
    aurora::MicrosoftAuth session_auth("test-client-id", session_transport);
    aurora::StoredSession previous;
    previous.refresh_token = "old-refresh-token";
    previous.minecraft.username = "Old";
    previous.minecraft.uuid = "00000000000000000000000000000000";
    previous.minecraft.access_token = "old-minecraft-token";

    const aurora::StoredSession refreshed = session_auth.refresh_session(previous);
    expect(
        refreshed.refresh_token == "old-refresh-token",
        "refresh session preserves the old token when none is rotated");
    expect(
        refreshed.minecraft.access_token == "minecraft-access-token",
        "refresh session completes the Minecraft chain");
    expect(
        session_transport->requests.size() == 6,
        "refresh session request count");
}

#ifdef _WIN32
std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }
    return std::string(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

void test_dpapi_and_store() {
    const std::string secret = "refresh-token-secret-value";
    const std::vector<std::uint8_t> ciphertext = aurora::dpapi_protect(secret);
    expect(!ciphertext.empty(), "DPAPI ciphertext is not empty");
    const std::string encrypted_bytes(
        reinterpret_cast<const char*>(ciphertext.data()),
        ciphertext.size());
    expect(
        encrypted_bytes.find(secret) == std::string::npos,
        "DPAPI ciphertext does not contain plaintext");
    expect(
        aurora::dpapi_unprotect(ciphertext) == secret,
        "DPAPI round-trip");

    const auto directory = make_temp_dir("store");
    const auto path = directory / "auth.bin";
    aurora::AuthStore store(path);
    expect(!store.exists(), "auth store starts empty");
    expect(!store.load().has_value(), "auth store load missing file");

    aurora::StoredSession session;
    session.minecraft.username = "Steve";
    session.minecraft.uuid = "069a79f444e94726a5befca90e38aaf5";
    session.minecraft.access_token = "minecraft-access-token-secret";
    session.minecraft.user_type = "msa";
    session.minecraft.xuid = "123456789";
    session.minecraft.expires_at = std::chrono::time_point_cast<std::chrono::seconds>(
        std::chrono::system_clock::now() + std::chrono::hours(1));
    session.refresh_token = secret;

    store.save(session);
    expect(store.exists(), "auth store file exists after save");

    const std::string stored_bytes = read_file(path);
    expect(
        stored_bytes.find("minecraft-access-token-secret") == std::string::npos,
        "stored file does not contain the Minecraft token");
    expect(
        stored_bytes.find(secret) == std::string::npos,
        "stored file does not contain the refresh token");

    const std::optional<aurora::StoredSession> loaded = store.load();
    expect(loaded.has_value(), "auth store loads saved session");
    if (loaded) {
        expect(loaded->minecraft.username == "Steve", "stored username");
        expect(
            loaded->minecraft.uuid == "069a79f444e94726a5befca90e38aaf5",
            "stored uuid");
        expect(
            loaded->minecraft.access_token == "minecraft-access-token-secret",
            "stored access token");
        expect(loaded->minecraft.user_type == "msa", "stored user type");
        expect(loaded->minecraft.xuid == "123456789", "stored xuid");
        expect(loaded->refresh_token == secret, "stored refresh token");
        expect(
            loaded->minecraft.expires_at == session.minecraft.expires_at,
            "stored expiry");
    }

    store.clear();
    expect(!store.exists(), "auth store file is removed");
    expect(!store.load().has_value(), "auth store is empty after clear");
}
#else
void test_dpapi_and_store() {
    std::cout << "DPAPI/auth store tests skipped (non-Windows build)\n";
}
#endif

} // namespace

int main() {
    run_test("form encoding", test_form_encoding);
    run_test("device code parsing", test_device_code_parsing);
    run_test("device polling states", test_device_polling_states);
    run_test("device poll wait", test_device_poll_wait_sequence);
    run_test("device poll terminal failures", test_device_poll_terminal_failures);
    run_test("full chain", test_full_chain);
    run_test("profile parsing", test_profile_parsing);
    run_test("entitlements", test_entitlements_and_missing_ownership);
    run_test("XSTS errors", test_xsts_errors);
    run_test("refresh flow", test_refresh_flow);
    run_test("DPAPI and store", test_dpapi_and_store);

    if (failures != 0) {
        std::cerr << failures << " auth test(s) failed\n";
        return 1;
    }
    std::cout << "All auth tests passed\n";
    return 0;
}
