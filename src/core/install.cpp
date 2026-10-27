#include "core/install.hpp"

#include "core/error.hpp"
#include "core/hash.hpp"
#include "core/http.hpp"
#include "core/json.hpp"
#include "core/log.hpp"
#include "core/paths.hpp"
#include "core/strings.hpp"
#include "core/zip.hpp"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

namespace aurora {

namespace {

struct AssetObject {
    std::string hash;
    std::uint64_t size = 0;
};

struct AssetTask {
    std::string name;
    AssetObject object;
};

std::vector<AssetTask> read_asset_index(const std::filesystem::path& path) {
    const Json index = read_json_file(path);
    const Json* objects = json_object(index, "objects");
    if (objects == nullptr) {
        fail("Asset index has no objects map: " + path_to_utf8(path));
    }
    std::vector<AssetTask> tasks;
    tasks.reserve(objects->size());
    for (auto it = objects->begin(); it != objects->end(); ++it) {
        if (!it.value().is_object()) {
            continue;
        }
        AssetObject object;
        object.hash = to_lower_ascii(json_string(it.value(), "hash"));
        const auto size = json_integer(it.value(), "size");
        if (size > 0) {
            object.size = static_cast<std::uint64_t>(size);
        }
        if (object.hash.empty()) {
            continue;
        }
        tasks.push_back(AssetTask{it.key(), std::move(object)});
    }
    return tasks;
}

HttpDownloadOptions http_options_from(const InstallOptions& options) {
    HttpDownloadOptions http_options;
    http_options.max_retries = std::max(0, options.download_retries);
    http_options.mirrors = options.download_mirrors;
    return http_options;
}

void download_file(const std::string& url,
                   const std::filesystem::path& destination,
                   const std::string& sha1,
                   bool force,
                   const HttpDownloadOptions& http_options) {
    if (force) {
        std::error_code ec;
        std::filesystem::remove(destination, ec);
    }
    http_download(url, destination, http_options, sha1);
}

void download_asset_objects(const std::filesystem::path& game_dir,
                            const std::vector<AssetTask>& tasks,
                            bool force,
                            const HttpDownloadOptions& http_options) {
    if (tasks.empty()) {
        return;
    }
    const std::filesystem::path objects_dir = game_dir / "assets" / "objects";
    const unsigned int threads = std::min(
        8u, std::max(1u, std::thread::hardware_concurrency() == 0
                            ? 4u
                            : std::thread::hardware_concurrency()));
    std::atomic<std::size_t> next{0};
    std::atomic<std::size_t> completed{0};
    std::atomic<bool> cancelled{false};
    std::mutex error_mutex;
    std::string first_error;

    auto worker = [&]() {
        while (!cancelled.load()) {
            const std::size_t index = next.fetch_add(1);
            if (index >= tasks.size()) {
                return;
            }
            const AssetTask& task = tasks[index];
            try {
                const std::string prefix = task.object.hash.substr(0, 2);
                const auto destination = objects_dir /
                    checked_relative_path(prefix + "/" + task.object.hash, "asset object");
                download_file(
                    "https://resources.download.minecraft.net/" + prefix + "/" + task.object.hash,
                    destination,
                    task.object.hash,
                    force,
                    http_options);
                const std::size_t done = completed.fetch_add(1) + 1;
                if (done % 250 == 0 || done == tasks.size()) {
                    log_info("Assets: " + std::to_string(done) + "/" + std::to_string(tasks.size()));
                }
            } catch (const std::exception& e) {
                {
                    std::lock_guard lock(error_mutex);
                    if (first_error.empty()) {
                        first_error = "Failed to download asset '" + task.name + "': " + e.what();
                    }
                }
                cancelled.store(true);
                return;
            }
        }
    };

    std::vector<std::thread> workers;
    workers.reserve(threads);
    for (unsigned int i = 0; i < threads; ++i) {
        workers.emplace_back(worker);
    }
    for (auto& thread : workers) {
        thread.join();
    }
    if (!first_error.empty()) {
        fail(first_error);
    }
}

bool native_extension_matches(std::string_view name) {
    const auto position = name.find_last_of('.');
    if (position == std::string_view::npos) {
        return false;
    }
    const std::string extension = to_lower_ascii(std::string(name.substr(position + 1)));
#ifdef _WIN32
    return extension == "dll";
#elif defined(__APPLE__)
    return extension == "dylib" || extension == "jnilib";
#else
    return extension == "so";
#endif
}

void extract_native_library(const ResolvedVersion& version,
                            const Library& library,
                            const std::filesystem::path& jars_dir,
                            const Environment& environment) {
    const std::string relative = library.native_relative_path(environment);
    if (relative.empty()) {
        return;
    }
    const auto archive = jars_dir / checked_relative_path(relative, "native library");
    if (!std::filesystem::is_regular_file(archive)) {
        fail("Native library is missing: " + path_to_utf8(archive));
    }
    const auto natives_dir = version.meta.natives_directory(version.game_dir);
    ensure_directory(natives_dir);
    extract_zip(archive, natives_dir, [&library](std::string_view entry) {
        if (!native_extension_matches(entry)) {
            return false;
        }
        for (const std::string& exclude : library.extract_exclude) {
            if (exclude.empty()) {
                continue;
            }
            if (entry == exclude) {
                return false;
            }
            if (entry.size() <= exclude.size() ||
                entry.compare(0, exclude.size(), exclude) != 0) {
                continue;
            }
            if (exclude.back() == '/' || exclude.back() == '\\' ||
                entry[exclude.size()] == '/' || entry[exclude.size()] == '\\') {
                return false;
            }
        }
        return true;
    });
}

} // namespace

void prepare_natives(const ResolvedVersion& version, const Environment& environment) {
    const auto jars_dir = version.game_dir / "libraries";
    for (const Library& library : version.meta.libraries) {
        if (!library.allowed(environment) || !library.native_classifier(environment)) {
            continue;
        }
        extract_native_library(version, library, jars_dir, environment);
    }
}

bool install_logging_config(const VersionMeta& meta,
                            const std::filesystem::path& game_dir,
                            const Environment& environment,
                            bool force) {
    if (!meta.logging_client || !meta.logging_client->allowed(environment)) {
        return false;
    }
    const std::filesystem::path destination = meta.logging_config_file(game_dir);
    if (destination.empty()) {
        if (!meta.logging_client->argument.empty()) {
            log_warn("Version " + meta.id +
                     " defines logging.client.argument without a logging file id");
        }
        return false;
    }
    const LoggingFile& file = *meta.logging_client->file;
    std::error_code ec;
    if (force && file.has_url()) {
        std::filesystem::remove(destination, ec);
        ec.clear();
    }
    if (file.has_url()) {
        try {
            http_download(file.url, destination, file.sha1);
        } catch (const std::exception& error) {
            log_warn("Unable to download logging configuration '" + file.id + "' for version '" +
                     meta.id + "': " + error.what());
            return std::filesystem::is_regular_file(destination, ec);
        }
    }
    if (!std::filesystem::is_regular_file(destination, ec)) {
        log_warn("Logging configuration is missing: " + path_to_utf8(destination) +
                 (file.has_url() ? std::string()
                                 : std::string(" (the version provides no download URL)")));
        return false;
    }
    log_debug("Logging configuration is ready: " + path_to_utf8(destination));
    return true;
}

void install_version(const ResolvedVersion& version,
                     const Environment& environment,
                     const InstallOptions& options) {
    const auto& meta = version.meta;
    const auto game_dir = version.game_dir;
    const auto libraries_dir = game_dir / "libraries";
    const auto http_options = http_options_from(options);

    log_info("Installing version " + meta.id + " into " + path_to_utf8(game_dir));

    if (meta.client && meta.client->has_url()) {
        download_file(
            meta.client->url, meta.client_jar(game_dir), meta.client->sha1, options.force,
            http_options);
    } else if (!std::filesystem::is_regular_file(meta.client_jar(game_dir))) {
        fail("Client jar is missing and the version has no client download URL: " +
             path_to_utf8(meta.client_jar(game_dir)));
    }

    if (options.logging) {
        install_logging_config(meta, game_dir, environment, options.force);
    }

    if (options.libraries) {
        for (const Library& library : meta.libraries) {
            if (!library.allowed(environment)) {
                continue;
            }
            if (library.artifact) {
                const std::string relative =
                    library.artifact->path.empty() ? library.relative_path() : library.artifact->path;
                const auto destination =
                    libraries_dir / checked_relative_path(relative, "library " + library.name);
                if (library.artifact->has_url()) {
                    download_file(
                        library.artifact->url, destination, library.artifact->sha1, options.force,
                        http_options);
                } else if (!std::filesystem::is_regular_file(destination)) {
                    fail("Library is missing and has no download URL: " + library.name);
                }
            }
            if (options.natives && library.native_classifier(environment)) {
                const auto download = library.native_download(environment);
                if (!download || download->path.empty()) {
                    fail("Unable to determine native library path: " + library.name);
                }
                const auto destination =
                    libraries_dir / checked_relative_path(download->path, "native " + library.name);
                if (download->has_url()) {
                    download_file(
                        download->url, destination, download->sha1, options.force,
                        http_options);
                } else if (!std::filesystem::is_regular_file(destination)) {
                    fail("Native library is missing and has no download URL: " + library.name);
                }
            }
        }
        if (options.natives) {
            prepare_natives(version, environment);
        }
    }

    if (options.assets) {
        if (meta.asset_index && meta.asset_index->has_url()) {
            download_file(
                meta.asset_index->url,
                meta.asset_index_file(game_dir),
                meta.asset_index->sha1,
                options.force,
                http_options);
        }
        const auto index_file = meta.asset_index_file(game_dir);
        if (!std::filesystem::is_regular_file(index_file)) {
            fail("Asset index is missing: " + path_to_utf8(index_file));
        }
        const auto tasks = read_asset_index(index_file);
        download_asset_objects(game_dir, tasks, options.force, http_options);
    }

    log_info("Version " + meta.id + " is ready");
}

} // namespace aurora
