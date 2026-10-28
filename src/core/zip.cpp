#include "core/zip.hpp"

#include "core/error.hpp"
#include "core/paths.hpp"
#include "core/strings.hpp"

#include <miniz.h>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <vector>

namespace aurora {

namespace {

struct Reader {
    mz_zip_archive archive{};
    std::FILE* file = nullptr;
    bool initialized = false;

    ~Reader() {
        if (initialized) {
            mz_zip_reader_end(&archive);
        }
        if (file != nullptr) {
            std::fclose(file);
        }
    }
};

// Opens the archive as a stream so large modpacks are not loaded into memory.
// mz_zip_reader_init_cfile does not take ownership of the FILE*, and
// reader_end only closes MZ_ZIP_TYPE_FILE handles, so Reader closes it.
bool open_reader(const std::filesystem::path& path, Reader& reader) {
#ifdef _WIN32
    std::FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"rb") != 0) {
        file = nullptr;
    }
#else
    std::FILE* file = std::fopen(path_to_utf8(path).c_str(), "rb");
#endif
    if (file == nullptr) {
        return false;
    }
#ifdef _WIN32
    _fseeki64(file, 0, SEEK_END);
    const long long size = _ftelli64(file);
    _fseeki64(file, 0, SEEK_SET);
#else
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
#endif
    if (size < 0 ||
        !mz_zip_reader_init_cfile(
            &reader.archive, file, static_cast<mz_uint64>(size), 0)) {
        std::fclose(file);
        return false;
    }
    reader.file = file;
    reader.initialized = true;
    return true;
}

std::filesystem::path safe_entry_path(std::string name) {
    for (char& c : name) {
        if (c == '\\') {
            c = '/';
        }
    }
    while (!name.empty() && name.front() == '/') {
        name.erase(name.begin());
    }
    if (name.empty() || (name.size() >= 2 && name[1] == ':')) {
        fail("ZIP entry has an unsafe absolute path");
    }
    std::filesystem::path result;
    for (const std::string& part : split(name, '/')) {
        if (part.empty() || part == ".") {
            continue;
        }
        if (part == ".." || part.find(':') != std::string::npos ||
            (!part.empty() && (part.back() == '.' || part.back() == ' '))) {
            fail("ZIP entry attempts path traversal: " + name);
        }
        result /= path_from_utf8(part);
    }
    return result;
}

std::string normalize_entry_name(std::string name) {
    for (char& c : name) {
        if (c == '\\') {
            c = '/';
        }
    }
    while (name.rfind("./", 0) == 0) {
        name.erase(0, 2);
    }
    while (!name.empty() && name.front() == '/') {
        name.erase(name.begin());
    }
    return name;
}

std::size_t extract_entries(const std::filesystem::path& archive,
                            const std::filesystem::path& destination,
                            std::string_view prefix,
                            const ZipEntryFilter& filter) {
    Reader reader;
    if (!open_reader(archive, reader)) {
        fail("Unable to open ZIP archive: " + path_to_utf8(archive));
    }

    std::size_t written = 0;
    const mz_uint count = mz_zip_reader_get_num_files(&reader.archive);
    for (mz_uint index = 0; index < count; ++index) {
        if (mz_zip_reader_is_file_a_directory(&reader.archive, index)) {
            continue;
        }
        mz_zip_archive_file_stat info{};
        if (!mz_zip_reader_file_stat(&reader.archive, index, &info)) {
            fail("Unable to read ZIP entry metadata: " + path_to_utf8(archive));
        }
        const std::string entry_name = info.m_filename;
        if (filter && !filter(entry_name)) {
            continue;
        }
        std::string normalized = normalize_entry_name(entry_name);
        if (!prefix.empty()) {
            if (normalized.size() <= prefix.size() ||
                normalized.compare(0, prefix.size(), prefix) != 0) {
                continue;
            }
            normalized.erase(0, prefix.size());
        }
        const auto relative = safe_entry_path(normalized);
        if (relative.empty()) {
            continue;
        }
        if (info.m_uncomp_size > 512ULL * 1024ULL * 1024ULL) {
            fail("ZIP entry is too large to extract: " + entry_name);
        }
        std::vector<std::uint8_t> content(static_cast<std::size_t>(info.m_uncomp_size));
        if (!content.empty() &&
            !mz_zip_reader_extract_to_mem(
                &reader.archive, index, content.data(), content.size(), 0)) {
            fail("Unable to extract ZIP entry: " + entry_name);
        }
        const auto output_path = destination / relative;
        ensure_directory(output_path.parent_path());
        std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
        if (!output) {
            fail("Unable to create extracted file: " + path_to_utf8(output_path));
        }
        if (!content.empty()) {
            output.write(
                reinterpret_cast<const char*>(content.data()),
                static_cast<std::streamsize>(content.size()));
        }
        if (!output) {
            fail("Unable to write extracted file: " + path_to_utf8(output_path));
        }
        ++written;
    }
    return written;
}

} // namespace

void extract_zip(const std::filesystem::path& archive,
                 const std::filesystem::path& destination,
                 const ZipEntryFilter& filter) {
    extract_entries(archive, destination, {}, filter);
}

std::size_t extract_zip_subtree(const std::filesystem::path& archive,
                                std::string_view prefix,
                                const std::filesystem::path& destination,
                                const ZipEntryFilter& filter) {
    std::string normalized = normalize_entry_name(std::string(prefix));
    while (!normalized.empty() && normalized.back() == '/') {
        normalized.pop_back();
    }
    if (!normalized.empty()) {
        normalized.push_back('/');
    }
    return extract_entries(archive, destination, normalized, filter);
}

bool zip_entry_exists(const std::filesystem::path& archive, std::string_view name) {
    Reader reader;
    if (!open_reader(archive, reader)) {
        fail("Unable to open ZIP archive: " + path_to_utf8(archive));
    }
    const std::string target = normalize_entry_name(std::string(name));
    const mz_uint count = mz_zip_reader_get_num_files(&reader.archive);
    for (mz_uint index = 0; index < count; ++index) {
        if (mz_zip_reader_is_file_a_directory(&reader.archive, index)) {
            continue;
        }
        mz_zip_archive_file_stat info{};
        if (!mz_zip_reader_file_stat(&reader.archive, index, &info)) {
            fail("Unable to read ZIP entry metadata: " + path_to_utf8(archive));
        }
        if (normalize_entry_name(info.m_filename) == target) {
            return true;
        }
    }
    return false;
}

std::optional<std::string> read_zip_entry(const std::filesystem::path& archive,
                                          std::string_view name,
                                          std::size_t max_bytes) {
    Reader reader;
    if (!open_reader(archive, reader)) {
        fail("Unable to open ZIP archive: " + path_to_utf8(archive));
    }

    const std::string target = normalize_entry_name(std::string(name));
    const mz_uint count = mz_zip_reader_get_num_files(&reader.archive);
    for (mz_uint index = 0; index < count; ++index) {
        if (mz_zip_reader_is_file_a_directory(&reader.archive, index)) {
            continue;
        }
        mz_zip_archive_file_stat info{};
        if (!mz_zip_reader_file_stat(&reader.archive, index, &info)) {
            fail("Unable to read ZIP entry metadata: " + path_to_utf8(archive));
        }
        if (normalize_entry_name(info.m_filename) != target) {
            continue;
        }
        if (info.m_uncomp_size > max_bytes) {
            fail("ZIP entry is too large to read: " + std::string(name));
        }
        std::string content(static_cast<std::size_t>(info.m_uncomp_size), '\0');
        if (!content.empty() &&
            !mz_zip_reader_extract_to_mem(
                &reader.archive, index, content.data(), content.size(), 0)) {
            fail("Unable to extract ZIP entry: " + std::string(name));
        }
        return content;
    }
    return std::nullopt;
}

} // namespace aurora
