#pragma once

#include "core/repository.hpp"
#include "core/rules.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace aurora {

struct InstallOptions {
    bool force = false;
    bool assets = true;
    bool libraries = true;
    bool natives = true;
    bool logging = true;
    int download_retries = 3;
    std::vector<std::string> download_mirrors;
};

void install_version(const ResolvedVersion& version,
                     const Environment& environment,
                     const InstallOptions& options = {});

bool install_logging_config(const VersionMeta& meta,
                            const std::filesystem::path& game_dir,
                            const Environment& environment,
                            bool force = false);

void prepare_natives(const ResolvedVersion& version, const Environment& environment);

struct LegacyAssetLayout {
    bool virtual_assets = false;
    bool map_to_resources = false;
};

// Reads the flags embedded in an asset index file. These flags are not part of
// the version JSON's assetIndex reference.
LegacyAssetLayout legacy_asset_layout(const std::filesystem::path& index_file);

// Copies downloaded objects into the legacy layouts used by pre-1.7 versions:
// assets/virtual/<index>/ or <game>/resources/.
void materialize_legacy_assets(const std::filesystem::path& game_dir,
                               std::string_view index_id,
                               const std::filesystem::path& index_file,
                               bool force = false);

} // namespace aurora
