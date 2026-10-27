#pragma once

#include "core/repository.hpp"
#include "core/rules.hpp"

#include <filesystem>

namespace aurora {

struct InstallOptions {
    bool force = false;
    bool assets = true;
    bool libraries = true;
    bool natives = true;
};

void install_version(const ResolvedVersion& version,
                     const Environment& environment,
                     const InstallOptions& options = {});

void prepare_natives(const ResolvedVersion& version, const Environment& environment);

} // namespace aurora
