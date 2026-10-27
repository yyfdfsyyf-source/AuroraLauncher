#include "core/repository.hpp"

#include "core/error.hpp"
#include "core/log.hpp"
#include "core/paths.hpp"
#include "core/strings.hpp"

#include <algorithm>
#include <set>

namespace aurora {

VersionRepository::VersionRepository(std::filesystem::path game_dir)
    : game_dir_(absolute_path(std::move(game_dir))) {}

const std::filesystem::path& VersionRepository::game_dir() const {
    return game_dir_;
}

std::filesystem::path VersionRepository::versions_dir() const {
    return game_dir_ / "versions";
}

std::optional<std::filesystem::path> VersionRepository::find_json(const std::string& id) const {
    if (id.empty() || id == "." || id == ".." || id.find('/') != std::string::npos ||
        id.find('\\') != std::string::npos) {
        return std::nullopt;
    }
    const std::filesystem::path directory = versions_dir() / path_from_utf8(id);
    std::error_code ec;
    const auto exact = directory / path_from_utf8(id + ".json");
    if (std::filesystem::is_regular_file(exact, ec)) {
        return exact;
    }
    if (!std::filesystem::is_directory(directory, ec)) {
        return std::nullopt;
    }
    std::vector<std::filesystem::path> candidates;
    for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
        if (ec) {
            break;
        }
        if (entry.is_regular_file() &&
            iequals(path_to_utf8(entry.path().extension()), ".json")) {
            candidates.push_back(entry.path());
        }
    }
    if (candidates.empty()) {
        return std::nullopt;
    }
    std::sort(candidates.begin(), candidates.end());
    return candidates.front();
}

std::vector<std::string> VersionRepository::list_ids() const {
    std::vector<std::string> ids;
    std::error_code ec;
    if (!std::filesystem::is_directory(versions_dir(), ec)) {
        return ids;
    }
    for (const auto& entry : std::filesystem::directory_iterator(versions_dir(), ec)) {
        if (ec || !entry.is_directory()) {
            continue;
        }
        const std::string id = path_to_utf8(entry.path().filename());
        if (find_json(id)) {
            ids.push_back(id);
        }
    }
    std::sort(ids.begin(), ids.end(), std::greater<>());
    return ids;
}

bool VersionRepository::contains(const std::string& id) const {
    return find_json(id).has_value();
}

VersionMeta VersionRepository::load(const std::string& id) const {
    const auto path = find_json(id);
    if (!path) {
        fail("Version '" + id + "' was not found in " + path_to_utf8(versions_dir()));
    }
    log_debug("Loading version JSON: " + path_to_utf8(*path));
    return VersionMeta::parse(read_json_file(*path), id);
}

ResolvedVersion VersionRepository::resolve(const std::string& id) const {
    const auto json_path = find_json(id);
    if (!json_path) {
        fail("Version '" + id + "' was not found in " + path_to_utf8(versions_dir()));
    }

    std::vector<VersionMeta> chain;
    std::set<std::string> visited;
    std::string current_id = id;
    while (true) {
        if (!visited.insert(current_id).second) {
            fail("Version inheritance cycle detected at '" + current_id + "'");
        }
        VersionMeta current = load(current_id);
        const std::string parent = current.inherits_from;
        chain.push_back(std::move(current));
        if (parent.empty()) {
            break;
        }
        current_id = parent;
    }

    VersionMeta resolved = chain.back();
    for (std::size_t i = chain.size() - 1; i-- > 0;) {
        resolved = merge_versions(resolved, chain[i]);
    }
    resolved.id = id;
    if (resolved.main_class.empty()) {
        fail("Version '" + id + "' has no mainClass after inheritance");
    }

    return ResolvedVersion{
        std::move(resolved),
        game_dir_,
        *json_path,
    };
}

} // namespace aurora
