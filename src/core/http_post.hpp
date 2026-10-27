#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace aurora {

struct HttpHeader {
    std::string name;
    std::string value;
};

struct HttpRequest {
    std::string method = "GET";
    std::string url;
    std::string body;
    std::vector<HttpHeader> headers;
};

struct HttpResponse {
    int status = 0;
    std::string body;
};

// Injectable transport used by the authentication module. Implementations must
// return the response for any HTTP status and only throw for transport errors.
class HttpTransport {
public:
    virtual ~HttpTransport() = default;
    virtual HttpResponse send(const HttpRequest& request) = 0;
};

std::shared_ptr<HttpTransport> default_http_transport();

std::string form_url_encode(std::string_view value);
std::string form_encode(const std::vector<std::pair<std::string, std::string>>& fields);

// Sends a request with WinHTTP. Non-2xx responses are returned to the caller.
HttpResponse http_send(const HttpRequest& request);

HttpResponse http_post_form(
    std::string_view url,
    const std::vector<std::pair<std::string, std::string>>& fields,
    const std::vector<HttpHeader>& headers = {});

HttpResponse http_post_json(std::string_view url,
                            std::string_view body,
                            const std::vector<HttpHeader>& headers = {});

HttpResponse http_get_with_headers(std::string_view url,
                                   const std::vector<HttpHeader>& headers = {});

} // namespace aurora
