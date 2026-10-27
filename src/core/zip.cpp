#include "core/zip.hpp"

#include "core/error.hpp"
#include "core/paths.hpp"
#include "core/strings.hpp"

#include <miniz.h>

#include <cstdint>
#include <fstream>
#include <vector>

namespace aurora {

namespace {

struct Reader {
    mz_zip_archive archive{};
    bool initialized = false;

    ~Reader() {
        if (initialized) {
            mz_zip_reader_end(&archive);
        }
    }
};

std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        fail("Unable to open ZIP archive: " + path_to_utf8(path));
    }
    input.seekg(0, std::ios::end);
    const auto size = input.tellg();
    if (size < 0) {
        fail("Unable to determine ZIP size: " + path_to_utf8(path));
    }
    input.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    if (!data.empty()) {
        input.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (!input) {
            fail("Unable to read ZIP archive: " + path_to_utf8(path));
        }
    }
    return data;
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

} // namespace

void extract_zip(const std::filesystem::path& archive,
                 const std::filesystem::path& destination,
                 const ZipEntryFilter& filter) {
    const auto data = read_file(archive);
    Reader reader;
    if (!mz_zip_reader_init_mem(&reader.archive, data.data(), data.size(), 0)) {
        fail("Unable to open ZIP archive: " + path_to_utf8(archive));
    }
    reader.initialized = true;

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
        const auto relative = safe_entry_path(entry_name);
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
    }
}

} // namespace aurora
