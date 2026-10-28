#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace aurora {

using ZipEntryFilter = std::function<bool(std::string_view)>;

void extract_zip(const std::filesystem::path& archive,
                 const std::filesystem::path& destination,
                 const ZipEntryFilter& filter = {});

// Extracts only entries below prefix (for example "overrides/") and strips
// that prefix from the destination paths. Returns the number of files written.
std::size_t extract_zip_subtree(const std::filesystem::path& archive,
                                std::string_view prefix,
                                const std::filesystem::path& destination,
                                const ZipEntryFilter& filter = {});

// Reads a single entry into memory. Returns nullopt when the entry does not
// exist; fails when it exceeds max_bytes.
std::optional<std::string> read_zip_entry(const std::filesystem::path& archive,
                                          std::string_view name,
                                          std::size_t max_bytes = 8ULL * 1024ULL * 1024ULL);

// Checks for an entry without extracting it.
bool zip_entry_exists(const std::filesystem::path& archive, std::string_view name);

} // namespace aurora
