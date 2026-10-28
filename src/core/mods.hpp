#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace aurora {

// A downloaded mod JAR must be a plain, safe file name. This rejects path
// separators, ADS/colon characters, Windows reserved names and anything that
// does not end in ".jar".
bool is_safe_mod_filename(std::string_view filename);

std::filesystem::path mods_directory(const std::filesystem::path& game_dir);

// Metadata formats understood by mainstream launchers. The first matching
// file wins in this order: fabric.mod.json, quilt.mod.json,
// META-INF/neoforge.mods.toml, META-INF/mods.toml, mcmod.info, litemod.json.
struct ModMetadata {
    // fabric, quilt, forge, neoforge or liteloader.
    std::string loader;
    std::string mod_id;
    std::string name;
    std::string version;
    std::string authors;
    std::string description;
};

std::optional<ModMetadata> read_mod_metadata(const std::filesystem::path& jar);

struct InstalledMod {
    std::filesystem::path path;
    // Name on disk, including the ".disabled" suffix when disabled.
    std::string filename;
    // Name without the ".disabled" suffix; this is what a mod is called when
    // enabled.
    std::string name;
    bool enabled = false;
    std::uint64_t size = 0;
    // Empty when hashing was not requested or the file could not be read.
    std::string sha1;
    bool has_metadata = false;
    ModMetadata metadata;
};

// Lists <game>/mods and recognizes both "<name>.jar" and
// "<name>.jar.disabled". A missing directory is an empty list. Symlinks,
// subdirectories and non-JAR files are skipped. Hashes are optional because
// listing does not need to read entire JARs. Metadata reads the first few
// entries of each JAR and is disabled by default for bulk scans.
std::vector<InstalledMod> scan_mods(const std::filesystem::path& game_dir,
                                    bool compute_sha1 = false,
                                    bool read_metadata = false);

struct ModToggleResult {
    std::filesystem::path path;
    std::string filename;
    bool enabled = false;
    // False when the file was already in the requested state.
    bool changed = false;
};

// Accepts either "<name>.jar" or "<name>.jar.disabled" and switches the file
// between those two states. Toggling is idempotent when the requested state
// already holds. If both variants exist, or another entry differs only by
// case, the operation fails instead of overwriting.
ModToggleResult set_mod_enabled(const std::filesystem::path& game_dir,
                                std::string_view filename,
                                bool enabled);

} // namespace aurora
