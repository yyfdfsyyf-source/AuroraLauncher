#pragma once

#include "core/version.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace aurora {

struct ResolvedVersion {
    VersionMeta meta;
    std::filesystem::path game_dir;
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
    ResolvedVersion resolve(const std::string& id) const;

private:
    VersionMeta load(const std::string& id) const;

    std::filesystem::path game_dir_;
};

} // namespace aurora
