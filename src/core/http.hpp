#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

namespace aurora {

using ProgressCallback = std::function<void(std::uint64_t received, std::uint64_t total)>;

struct HttpMetadata {
    int status = 0;
    std::uint64_t content_length = 0;
};

HttpMetadata http_request(std::string_view url,
                          const std::function<void(const void*, std::size_t)>& sink,
                          const ProgressCallback& progress = {});

std::string http_get(std::string_view url);

void http_download(std::string_view url,
                   const std::filesystem::path& destination,
                   std::string_view expected_sha1 = {},
                   const ProgressCallback& progress = {});

} // namespace aurora
