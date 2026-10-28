#include "core/manifest.hpp"

#include "core/error.hpp"
#include "core/hash.hpp"
#include "core/http.hpp"
#include "core/log.hpp"
#include "core/paths.hpp"
#include "core/repository.hpp"
#include "core/strings.hpp"

#include <set>
#include <system_error>
#include <utility>

namespace aurora {

namespace {

std::string manifest_label(const std::string& source) {
    return source.empty() ? std::string("version manifest") : source;
}

void remove_quietly(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

} // namespace

VersionManifest VersionManifest::parse(const Json& value, std::string source) {
    const std::string label = manifest_label(source);
    if (!value.is_object()) {
        fail("Version manifest " + label + " must be a JSON object");
    }

    VersionManifest result;
    if (const Json* latest = json_object(value, "latest")) {
        VersionManifestLatest parsed;
        parsed.release = json_string(*latest, "release");
        parsed.snapshot = json_string(*latest, "snapshot");
        if (parsed.release.empty() || parsed.snapshot.empty()) {
            fail("Version manifest " + label +
                 " latest section must contain release and snapshot ids");
        }
        result.latest = std::move(parsed);
    }

    const Json* versions = json_array(value, "versions");
    if (versions == nullptr) {
        fail("Version manifest " + label + " has no versions array");
    }

    std::set<std::string> seen;
    for (const Json& item : *versions) {
        if (!item.is_object()) {
            fail("Version manifest " + label + " contains a non-object version entry");
        }
        VersionManifestEntry entry;
        entry.id = json_string(item, "id");
        entry.type = json_string(item, "type");
        entry.url = json_string(item, "url");
        entry.sha1 = to_lower_ascii(json_string(item, "sha1"));
        if (entry.id.empty()) {
            fail("Version manifest " + label + " contains an entry without an id");
        }
        if (!seen.insert(entry.id).second) {
            fail("Version manifest " + label + " contains duplicate version '" + entry.id + "'");
        }
        if (entry.type.empty()) {
            fail("Version manifest entry '" + entry.id + "' has no type");
        }
        if (entry.url.empty()) {
            fail("Version manifest entry '" + entry.id + "' has no URL");
        }
        if (entry.sha1.empty()) {
            fail("Version manifest entry '" + entry.id + "' has no SHA-1");
        }
        entry.release_time = json_optional_string(item, "releaseTime");
        entry.time = json_optional_string(item, "time");
        result.versions.push_back(std::move(entry));
    }
    return result;
}

const VersionManifestEntry* VersionManifest::find(std::string_view id) const {
    for (const VersionManifestEntry& entry : versions) {
        if (entry.id == id) {
            return &entry;
        }
    }
    return nullptr;
}

const VersionManifestEntry& VersionManifest::require(std::string_view id) const {
    if (const VersionManifestEntry* entry = find(id)) {
        return *entry;
    }
    fail("Version '" + std::string(id) + "' is not present in the version manifest");
}

const VersionManifestEntry& VersionManifest::latest_release() const {
    if (!latest || latest->release.empty()) {
        fail("Version manifest has no latest.release entry");
    }
    const VersionManifestEntry* entry = find(latest->release);
    if (entry == nullptr) {
        fail("Version manifest latest.release '" + latest->release +
             "' does not exist in the versions array");
    }
    return *entry;
}

const VersionManifestEntry& VersionManifest::latest_snapshot() const {
    if (!latest || latest->snapshot.empty()) {
        fail("Version manifest has no latest.snapshot entry");
    }
    const VersionManifestEntry* entry = find(latest->snapshot);
    if (entry == nullptr) {
        fail("Version manifest latest.snapshot '" + latest->snapshot +
             "' does not exist in the versions array");
    }
    return *entry;
}

VersionManifest parse_version_manifest(std::string_view text, std::string_view source) {
    return VersionManifest::parse(parse_json(text, source), std::string(source));
}

std::string version_manifest_url(std::string_view base_url) {
    if (base_url.empty()) {
        fail("Version manifest base URL is empty");
    }
    std::string base(base_url);
    while (!base.empty() && base.back() == '/') {
        base.pop_back();
    }
    if (base.empty()) {
        fail("Version manifest base URL is empty");
    }
    base += "/mc/game/version_manifest_v2.json";
    return base;
}

VersionManifest fetch_version_manifest(std::string_view url) {
    if (url.empty()) {
        fail("Version manifest URL is empty");
    }
    log_info("Fetching version manifest from " + std::string(url));
    return parse_version_manifest(http_get(url), url);
}

std::filesystem::path version_json_path(const std::filesystem::path& game_dir,
                                        std::string_view id) {
    const std::filesystem::path relative_id = checked_relative_path(id, "version id");
    const std::filesystem::path file_name =
        checked_relative_path(std::string(id) + ".json", "version JSON file");
    return game_dir / "versions" / relative_id / file_name;
}

VersionJsonInstallResult install_version_json(
    const VersionManifestEntry& entry,
    const std::filesystem::path& game_dir,
    const VersionJsonInstallOptions& options) {
    if (entry.id.empty()) {
        fail("Cannot install a version JSON: the manifest entry has no id");
    }
    if (entry.url.empty()) {
        fail("Cannot install version JSON for '" + entry.id + "': the manifest entry has no URL");
    }

    const std::filesystem::path destination = version_json_path(game_dir, entry.id);
    std::error_code ec;
    const bool destination_exists = std::filesystem::exists(destination, ec);
    if (ec) {
        fail("Unable to inspect version JSON " + path_to_utf8(destination) + ": " +
             ec.message());
    }
    if (destination_exists) {
        const bool destination_is_file = std::filesystem::is_regular_file(destination, ec);
        if (ec) {
            fail("Unable to inspect version JSON " + path_to_utf8(destination) + ": " +
                 ec.message());
        }
        if (!destination_is_file) {
            fail("Version JSON path is not a regular file: " + path_to_utf8(destination));
        }
        if (!options.force) {
            Json existing;
            try {
                existing = read_json_file(destination);
            } catch (const Error& error) {
                fail("Existing version JSON " + path_to_utf8(destination) +
                     " cannot be read: " + error.what() + ". Use force to replace it.");
            }
            if (!existing.is_object()) {
                fail("Existing version JSON " + path_to_utf8(destination) +
                     " is not a JSON object. Use force to replace it.");
            }
            const std::string existing_id = json_string(existing, "id");
            if (existing_id != entry.id) {
                fail("Version JSON " + path_to_utf8(destination) + " already exists for version '" +
                     (existing_id.empty() ? std::string("<unknown>") : existing_id) +
                     "'. Use force to replace it with '" + entry.id + "'.");
            }
            log_debug("Version JSON is already installed: " + path_to_utf8(destination));
            return VersionJsonInstallResult{destination, false};
        }
    }

    ensure_directory(destination.parent_path());
    std::filesystem::path staging = destination;
    staging += ".download";
    std::filesystem::path staging_partial = staging;
    staging_partial += ".part";
    remove_quietly(staging);
    remove_quietly(staging_partial);

    try {
        if (options.download) {
            options.download(entry.url, staging, entry.sha1);
        } else {
            http_download(entry.url, staging, entry.sha1);
        }

        const bool staging_is_file = std::filesystem::is_regular_file(staging, ec);
        if (ec) {
            fail("Unable to inspect downloaded version JSON " + path_to_utf8(staging) + ": " +
                 ec.message());
        }
        if (!staging_is_file) {
            fail("Downloading version JSON for '" + entry.id + "' did not produce " +
                 path_to_utf8(staging));
        }
        if (!entry.sha1.empty()) {
            const std::string actual = sha1_file(staging);
            if (actual != to_lower_ascii(entry.sha1)) {
                fail("SHA-1 mismatch for version JSON '" + entry.id + "' (expected " +
                     entry.sha1 + ", got " + actual + ")");
            }
        }

        Json downloaded;
        try {
            downloaded = read_json_file(staging);
        } catch (const Error& error) {
            fail("Downloaded version JSON for '" + entry.id + "' is invalid: " + error.what());
        }
        if (!downloaded.is_object()) {
            fail("Downloaded version JSON for '" + entry.id + "' is not a JSON object");
        }
        const std::string actual_id = json_string(downloaded, "id");
        if (actual_id != entry.id) {
            fail("Downloaded version JSON declares id '" +
                 (actual_id.empty() ? std::string("<missing>") : actual_id) +
                 "' but '" + entry.id + "' was expected");
        }

        std::filesystem::remove(destination, ec);
        ec.clear();
        std::filesystem::rename(staging, destination, ec);
        if (ec) {
            fail("Unable to install version JSON at " + path_to_utf8(destination) + ": " +
                 ec.message());
        }
    } catch (...) {
        remove_quietly(staging);
        remove_quietly(staging_partial);
        throw;
    }

    log_info("Installed version JSON for " + entry.id + " at " + path_to_utf8(destination));
    return VersionJsonInstallResult{destination, true};
}

VersionJsonInstallResult install_version_json(
    const VersionManifest& manifest,
    std::string_view id,
    const std::filesystem::path& game_dir,
    const VersionJsonInstallOptions& options) {
    return install_version_json(manifest.require(id), game_dir, options);
}

ResolvedVersion resolve_or_fetch_version(const std::filesystem::path& game_dir,
                                         const std::string& id,
                                         bool allow_fetch,
                                         bool force_json,
                                         std::string_view manifest_url) {
    const VersionRepository repository(game_dir);
    if (repository.contains(id)) {
        return repository.resolve(id);
    }
    if (!allow_fetch) {
        fail("Version '" + id +
             "' is not installed locally. Use 'aurora version fetch " + id +
             "' or add --install to fetch it.");
    }
    log_info("Version '" + id + "' is not local; fetching version JSON");
    const VersionManifest manifest = fetch_version_manifest(manifest_url);
    VersionJsonInstallOptions options;
    options.force = force_json;
    const VersionJsonInstallResult result =
        install_version_json(manifest, id, repository.game_dir(), options);
    log_info("Version JSON is ready: " + path_to_utf8(result.path));
    return repository.resolve(id);
}

} // namespace aurora
