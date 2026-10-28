#include "core/mirrors.hpp"

#include "core/log.hpp"
#include "core/strings.hpp"

#include <algorithm>
#include <mutex>
#include <optional>

namespace aurora {

namespace {

constexpr std::string_view kBuiltinMirrorHost = "bmclapi2.bangbang93.com";
constexpr std::string_view kMcimMirrorHost = "mod.mcimirror.top";

struct ParsedUrl {
    std::string scheme;
    std::string host;
    std::string path;
};

std::optional<ParsedUrl> parse_url(std::string_view url) {
    const std::size_t scheme_end = url.find("://");
    if (scheme_end == std::string_view::npos || scheme_end == 0) {
        return std::nullopt;
    }

    ParsedUrl parsed;
    parsed.scheme = to_lower_ascii(std::string(url.substr(0, scheme_end)));

    const std::size_t authority_start = scheme_end + 3;
    const std::size_t authority_end = url.find_first_of("/?#", authority_start);
    const std::string_view authority = url.substr(
        authority_start,
        authority_end == std::string_view::npos ? std::string_view::npos
                                                : authority_end - authority_start);
    if (authority.empty() || authority.find('@') != std::string_view::npos ||
        authority.find(':') != std::string_view::npos) {
        return std::nullopt;
    }
    parsed.host = to_lower_ascii(std::string(authority));

    if (authority_end == std::string_view::npos) {
        parsed.path = "/";
    } else if (url[authority_end] == '/') {
        parsed.path = std::string(url.substr(authority_end));
    } else {
        parsed.path = "/" + std::string(url.substr(authority_end));
    }
    return parsed;
}

struct MirrorRewrite {
    std::string_view host;
    std::string_view source_prefix;
    std::string_view target_prefix;
    std::string_view mirror_root;
    MirrorProvider provider;
};

constexpr MirrorRewrite kRewrites[] = {
    {"piston-meta.mojang.com", "/", "/", "https://bmclapi2.bangbang93.com",
     MirrorProvider::BmclApi},
    {"launchermeta.mojang.com", "/", "/", "https://bmclapi2.bangbang93.com",
     MirrorProvider::BmclApi},
    {"piston-data.mojang.com", "/", "/", "https://bmclapi2.bangbang93.com",
     MirrorProvider::BmclApi},
    {"launcher.mojang.com", "/", "/", "https://bmclapi2.bangbang93.com",
     MirrorProvider::BmclApi},
    {"libraries.minecraft.net", "/", "/maven/", "https://bmclapi2.bangbang93.com",
     MirrorProvider::BmclApi},
    {"resources.download.minecraft.net", "/", "/assets/",
     "https://bmclapi2.bangbang93.com", MirrorProvider::BmclApi},
    {"meta.fabricmc.net", "/", "/fabric-meta/", "https://bmclapi2.bangbang93.com",
     MirrorProvider::BmclApi},
    {"maven.fabricmc.net", "/", "/maven/", "https://bmclapi2.bangbang93.com",
     MirrorProvider::BmclApi},
    {"maven.minecraftforge.net", "/", "/maven/", "https://bmclapi2.bangbang93.com",
     MirrorProvider::BmclApi},
    {"files.minecraftforge.net", "/maven/", "/maven/",
     "https://bmclapi2.bangbang93.com", MirrorProvider::BmclApi},
    {"maven.neoforged.net", "/releases/", "/maven/", "https://bmclapi2.bangbang93.com",
     MirrorProvider::BmclApi},
    {"api.modrinth.com", "/v2/", "/modrinth/v2/", "https://mod.mcimirror.top",
     MirrorProvider::McimApi},
    // MCIM redirects these paths to the official Modrinth CDN; it is an
    // alternate route rather than an independent cache.
    {"cdn.modrinth.com", "/data/", "/data/", "https://mod.mcimirror.top",
     MirrorProvider::McimCdn},
};

std::mutex g_mirror_health_mutex;
BuiltinMirrorHealth g_bmcl_api_health = BuiltinMirrorHealth::Unknown;
BuiltinMirrorHealth g_mcim_api_health = BuiltinMirrorHealth::Unknown;
BuiltinMirrorHealth g_mcim_cdn_health = BuiltinMirrorHealth::Unknown;

BuiltinMirrorHealth* health_slot(MirrorProvider provider) {
    switch (provider) {
    case MirrorProvider::BmclApi:
        return &g_bmcl_api_health;
    case MirrorProvider::McimApi:
        return &g_mcim_api_health;
    case MirrorProvider::McimCdn:
        return &g_mcim_cdn_health;
    case MirrorProvider::NoProvider:
        break;
    }
    return nullptr;
}

bool builtin_mirror_available(MirrorProvider provider) {
    std::lock_guard<std::mutex> lock(g_mirror_health_mutex);
    const BuiltinMirrorHealth* slot = health_slot(provider);
    return slot != nullptr && *slot != BuiltinMirrorHealth::Failed;
}

} // namespace

BuiltinMirror builtin_mirror(std::string_view url) {
    const auto parsed = parse_url(url);
    if (!parsed || parsed->scheme != "https") {
        return {};
    }

    for (const MirrorRewrite& rewrite : kRewrites) {
        const bool path_matches =
            parsed->path.compare(0, rewrite.source_prefix.size(), rewrite.source_prefix) == 0;
        if (parsed->host != rewrite.host || !path_matches) {
            continue;
        }
        std::string result(rewrite.mirror_root);
        result += rewrite.target_prefix;
        result += parsed->path.substr(rewrite.source_prefix.size());
        return BuiltinMirror{rewrite.provider, std::move(result)};
    }
    return {};
}

std::string builtin_mirror_url(std::string_view url) {
    return builtin_mirror(url).url;
}

MirrorProvider builtin_mirror_provider(std::string_view url) {
    const auto parsed = parse_url(url);
    if (!parsed) {
        return MirrorProvider::NoProvider;
    }
    if (parsed->host == kBuiltinMirrorHost) {
        return MirrorProvider::BmclApi;
    }
    if (parsed->host == kMcimMirrorHost) {
        if (parsed->path.starts_with("/modrinth/")) {
            return MirrorProvider::McimApi;
        }
        if (parsed->path.starts_with("/data/")) {
            return MirrorProvider::McimCdn;
        }
    }
    return MirrorProvider::NoProvider;
}

bool is_builtin_mirror_url(std::string_view url) {
    return builtin_mirror_provider(url) != MirrorProvider::NoProvider;
}

std::string_view mirror_provider_name(MirrorProvider provider) {
    switch (provider) {
    case MirrorProvider::BmclApi:
        return "BMCLAPI";
    case MirrorProvider::McimApi:
        return "MCIM API";
    case MirrorProvider::McimCdn:
        return "MCIM CDN";
    case MirrorProvider::NoProvider:
        break;
    }
    return "unknown";
}

BuiltinMirrorHealth builtin_mirror_health(MirrorProvider provider) {
    std::lock_guard<std::mutex> lock(g_mirror_health_mutex);
    const BuiltinMirrorHealth* slot = health_slot(provider);
    return slot == nullptr ? BuiltinMirrorHealth::Unknown : *slot;
}

void report_builtin_mirror_success(std::string_view url) {
    const MirrorProvider provider = builtin_mirror_provider(url);
    if (provider == MirrorProvider::NoProvider) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_mirror_health_mutex);
    BuiltinMirrorHealth* slot = health_slot(provider);
    if (slot != nullptr && *slot == BuiltinMirrorHealth::Unknown) {
        *slot = BuiltinMirrorHealth::Available;
    }
}

std::vector<std::string> download_url_candidates(
    std::string_view url,
    const std::vector<std::string>& extra_mirrors) {
    std::vector<std::string> candidates;
    const auto add = [&candidates](std::string_view value) {
        std::string candidate = trim(value);
        if (candidate.empty()) {
            return;
        }
        if (std::find(candidates.begin(), candidates.end(), candidate) ==
            candidates.end()) {
            candidates.push_back(std::move(candidate));
        }
    };

    const BuiltinMirror mirror = builtin_mirror(url);
    if (!mirror.url.empty() && builtin_mirror_available(mirror.provider)) {
        add(mirror.url);
    }
    for (const std::string& extra : extra_mirrors) {
        add(extra);
    }
    add(url);
    return candidates;
}

void report_download_source_failure(std::string_view url) {
    const MirrorProvider provider = builtin_mirror_provider(url);
    if (provider == MirrorProvider::NoProvider) {
        return;
    }

    bool newly_failed = false;
    {
        std::lock_guard<std::mutex> lock(g_mirror_health_mutex);
        BuiltinMirrorHealth* slot = health_slot(provider);
        newly_failed = slot != nullptr && *slot != BuiltinMirrorHealth::Failed;
        if (slot != nullptr) {
            *slot = BuiltinMirrorHealth::Failed;
        }
    }
    if (newly_failed) {
        log_warn(
            "Built-in " + std::string(mirror_provider_name(provider)) +
            " mirror is unavailable or slow; using fallback sources for the rest "
            "of this run");
    }
}

void reset_builtin_mirror_health() {
    std::lock_guard<std::mutex> lock(g_mirror_health_mutex);
    g_bmcl_api_health = BuiltinMirrorHealth::Unknown;
    g_mcim_api_health = BuiltinMirrorHealth::Unknown;
    g_mcim_cdn_health = BuiltinMirrorHealth::Unknown;
}

} // namespace aurora
