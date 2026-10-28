#pragma once

#include "core/version.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace aurora {

struct ResolvedVersion {
    VersionMeta meta;
    // Shared root that owns libraries, assets and the versions directory.
    std::filesystem::path game_dir;
    // Working directory passed to the game. Modpack instances use their
    // version directory; regular versions use game_dir.
    std::filesystem::path run_dir;
    std::filesystem::path json_path;
};

class VersionRepository {
public:
    explicit VersionRepository(std::filesystem::path game_dir);

    const std::filesystem::path& game_dir() const;
    std::filesystem::path versions_dir() const;

    std::vector<std::string> list_ids() const;
    bool contains(const std::string& id) const;
    std::optional<std::filesystem::path> find_json(const std::string& id) const;
    // Instance working directory without resolving inheritance: the version
    // folder for modpack instances, otherwise game_dir.
    std::filesystem::path run_dir_for(const std::string& id) const;
    ResolvedVersion resolve(const std::string& id) const;

private:
    VersionMeta load(const std::string& id) const;

    std::filesystem::path game_dir_;
};

} // namespace aurora
