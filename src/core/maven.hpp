#pragma once

#include <string>
#include <string_view>

namespace aurora {

struct MavenCoordinate {
    std::string group;
    std::string artifact;
    std::string version;
    std::string classifier;
    std::string extension = "jar";

    static MavenCoordinate parse(std::string_view value);
    std::string path() const;
    std::string to_string() const;
};

std::string maven_path(std::string_view coordinate);

} // namespace aurora
