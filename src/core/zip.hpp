#pragma once

#include <filesystem>
#include <functional>
#include <string_view>

namespace aurora {

using ZipEntryFilter = std::function<bool(std::string_view)>;

void extract_zip(const std::filesystem::path& archive,
                 const std::filesystem::path& destination,
                 const ZipEntryFilter& filter = {});

} // namespace aurora
