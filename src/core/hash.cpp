#include "core/hash.hpp"

#include "core/error.hpp"
#include "core/paths.hpp"
#include "core/strings.hpp"

#include <fstream>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#endif

namespace aurora {

#ifdef _WIN32
namespace {
template <std::size_t Size>
std::array<std::uint8_t, Size> hash_data(LPCWSTR algorithm,
                                         std::span<const std::uint8_t> data) {
    BCRYPT_ALG_HANDLE provider = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD object_size = 0;
    DWORD result_size = 0;
    std::vector<std::uint8_t> object;

    auto cleanup = [&]() {
        if (hash != nullptr) {
            BCryptDestroyHash(hash);
            hash = nullptr;
        }
        if (provider != nullptr) {
            BCryptCloseAlgorithmProvider(provider, 0);
            provider = nullptr;
        }
    };

    NTSTATUS status = BCryptOpenAlgorithmProvider(&provider, algorithm, nullptr, 0);
    if (!BCRYPT_SUCCESS(status)) {
        fail("Unable to open a BCrypt hash provider");
    }
    status = BCryptGetProperty(
        provider, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&object_size),
        sizeof(object_size), &result_size, 0);
    if (!BCRYPT_SUCCESS(status)) {
        cleanup();
        fail("Unable to query the BCrypt hash object size");
    }
    object.resize(object_size);
    status = BCryptCreateHash(
        provider, &hash, object.data(), static_cast<ULONG>(object.size()), nullptr, 0, 0);
    if (!BCRYPT_SUCCESS(status)) {
        cleanup();
        fail("Unable to create a BCrypt hash");
    }
    if (!data.empty()) {
        status = BCryptHashData(
            hash,
            const_cast<PUCHAR>(reinterpret_cast<const UCHAR*>(data.data())),
            static_cast<ULONG>(data.size()),
            0);
        if (!BCRYPT_SUCCESS(status)) {
            cleanup();
            fail("Unable to hash data");
        }
    }
    std::array<std::uint8_t, Size> output{};
    status = BCryptFinishHash(hash, output.data(), static_cast<ULONG>(output.size()), 0);
    cleanup();
    if (!BCRYPT_SUCCESS(status)) {
        fail("Unable to finish BCrypt hashing");
    }
    return output;
}

std::array<std::uint8_t, 20> hash_sha1(std::span<const std::uint8_t> data) {
    return hash_data<20>(BCRYPT_SHA1_ALGORITHM, data);
}

std::array<std::uint8_t, 16> hash_md5(std::span<const std::uint8_t> data) {
    return hash_data<16>(BCRYPT_MD5_ALGORITHM, data);
}

std::array<std::uint8_t, 32> hash_sha256(std::span<const std::uint8_t> data) {
    return hash_data<32>(BCRYPT_SHA256_ALGORITHM, data);
}

template <std::size_t Size>
std::array<std::uint8_t, Size> hash_file_data(LPCWSTR algorithm,
                                               const char* label,
                                               const std::filesystem::path& path) {
    BCRYPT_ALG_HANDLE provider = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD object_size = 0;
    DWORD result_size = 0;
    std::vector<std::uint8_t> object;

    auto cleanup = [&]() {
        if (hash != nullptr) {
            BCryptDestroyHash(hash);
            hash = nullptr;
        }
        if (provider != nullptr) {
            BCryptCloseAlgorithmProvider(provider, 0);
            provider = nullptr;
        }
    };

    const std::string name(label);
    NTSTATUS status = BCryptOpenAlgorithmProvider(&provider, algorithm, nullptr, 0);
    if (!BCRYPT_SUCCESS(status)) {
        fail("Unable to open the " + name + " provider");
    }
    status = BCryptGetProperty(
        provider, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&object_size),
        sizeof(object_size), &result_size, 0);
    if (!BCRYPT_SUCCESS(status)) {
        cleanup();
        fail("Unable to query the " + name + " object size");
    }
    object.resize(object_size);
    status = BCryptCreateHash(
        provider, &hash, object.data(), static_cast<ULONG>(object.size()), nullptr, 0, 0);
    if (!BCRYPT_SUCCESS(status)) {
        cleanup();
        fail("Unable to create a " + name + " hash");
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        cleanup();
        fail("Unable to open file for " + name + ": " + path_to_utf8(path));
    }
    std::array<char, 64 * 1024> buffer{};
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = input.gcount();
        if (count > 0) {
            status = BCryptHashData(
                hash,
                reinterpret_cast<PUCHAR>(buffer.data()),
                static_cast<ULONG>(count),
                0);
            if (!BCRYPT_SUCCESS(status)) {
                cleanup();
                fail("Unable to hash file with " + name + ": " + path_to_utf8(path));
            }
        }
    }
    if (!input.eof()) {
        cleanup();
        fail("Unable to read file for " + name + ": " + path_to_utf8(path));
    }

    std::array<std::uint8_t, Size> digest{};
    status = BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0);
    cleanup();
    if (!BCRYPT_SUCCESS(status)) {
        fail("Unable to finish " + name + " hashing");
    }
    return digest;
}
} // namespace
#endif

std::array<std::uint8_t, 20> sha1(std::span<const std::uint8_t> data) {
#ifdef _WIN32
    return hash_sha1(data);
#else
    (void)data;
    fail("SHA-1 is not implemented on this platform");
#endif
}

std::string bytes_to_hex(std::span<const std::uint8_t> data) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(data.size() * 2);
    for (const std::uint8_t byte : data) {
        result.push_back(digits[byte >> 4]);
        result.push_back(digits[byte & 0x0F]);
    }
    return result;
}

std::string sha1_hex(std::span<const std::uint8_t> data) {
    return bytes_to_hex(sha1(data));
}

std::string sha1_hex(const std::string& data) {
    return sha1_hex(std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(data.data()), data.size()));
}

std::string sha1_file(const std::filesystem::path& path) {
#ifdef _WIN32
    return bytes_to_hex(hash_file_data<20>(BCRYPT_SHA1_ALGORITHM, "SHA-1", path));
#else
    (void)path;
    fail("SHA-1 is not implemented on this platform");
#endif
}

std::array<std::uint8_t, 32> sha256(std::span<const std::uint8_t> data) {
#ifdef _WIN32
    return hash_sha256(data);
#else
    (void)data;
    fail("SHA-256 is not implemented on this platform");
#endif
}

std::string sha256_hex(std::span<const std::uint8_t> data) {
    return bytes_to_hex(sha256(data));
}

std::string sha256_hex(const std::string& data) {
    return sha256_hex(std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(data.data()), data.size()));
}

std::string sha256_file(const std::filesystem::path& path) {
#ifdef _WIN32
    return bytes_to_hex(hash_file_data<32>(BCRYPT_SHA256_ALGORITHM, "SHA-256", path));
#else
    (void)path;
    fail("SHA-256 is not implemented on this platform");
#endif
}

std::array<std::uint8_t, 16> md5(std::span<const std::uint8_t> data) {
#ifdef _WIN32
    return hash_md5(data);
#else
    (void)data;
    fail("MD5 is not implemented on this platform");
#endif
}

std::string offline_uuid(const std::string& username) {
    const std::string input = "OfflinePlayer:" + username;
    auto digest = md5(std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(input.data()), input.size()));
    digest[6] = static_cast<std::uint8_t>((digest[6] & 0x0F) | 0x30);
    digest[8] = static_cast<std::uint8_t>((digest[8] & 0x3F) | 0x80);
    const std::string hex = bytes_to_hex(digest);
    return hex.substr(0, 8) + "-" + hex.substr(8, 4) + "-" + hex.substr(12, 4) + "-" +
           hex.substr(16, 4) + "-" + hex.substr(20, 12);
}

} // namespace aurora
