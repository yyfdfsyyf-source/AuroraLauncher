#pragma once

#include "core/repository.hpp"
#include "core/rules.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace aurora {

struct InstallOptions {
    bool force = false;
    bool assets = true;
    bool libraries = true;
    bool natives = true;
    int download_retries = 3;
    std::vector<std::string> download_mirrors;
};

void install_version(const ResolvedVersion& version,
                     const Environment& environment,
                     const InstallOptions& options = {});

void prepare_natives(const ResolvedVersion& version, const Environment& environment);

} // namespace aurora
