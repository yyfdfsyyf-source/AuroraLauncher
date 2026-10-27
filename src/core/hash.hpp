#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

namespace aurora {

std::array<std::uint8_t, 20> sha1(std::span<const std::uint8_t> data);
std::string sha1_hex(std::span<const std::uint8_t> data);
std::string sha1_hex(const std::string& data);
std::string sha1_file(const std::filesystem::path& path);

std::array<std::uint8_t, 32> sha256(std::span<const std::uint8_t> data);
std::string sha256_hex(std::span<const std::uint8_t> data);
std::string sha256_hex(const std::string& data);
std::string sha256_file(const std::filesystem::path& path);

std::array<std::uint8_t, 16> md5(std::span<const std::uint8_t> data);
std::string offline_uuid(const std::string& username);

std::string bytes_to_hex(std::span<const std::uint8_t> data);

} // namespace aurora
