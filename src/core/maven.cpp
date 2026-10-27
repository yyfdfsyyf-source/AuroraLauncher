#include "core/maven.hpp"

#include "core/error.hpp"
#include "core/strings.hpp"

namespace aurora {

MavenCoordinate MavenCoordinate::parse(std::string_view value) {
    std::string text(value);
    MavenCoordinate result;

    const auto at = text.rfind('@');
    if (at != std::string::npos) {
        if (at + 1 >= text.size()) {
            fail("Invalid Maven coordinate: " + std::string(value));
        }
        result.extension = text.substr(at + 1);
        text.resize(at);
    }

    const auto parts = split(text, ':');
    if (parts.size() < 3 || parts.size() > 4) {
        fail("Invalid Maven coordinate: " + std::string(value));
    }
    result.group = trim(parts[0]);
    result.artifact = trim(parts[1]);
    result.version = trim(parts[2]);
    if (parts.size() == 4) {
        result.classifier = trim(parts[3]);
    }
    if (result.group.empty() || result.artifact.empty() || result.version.empty()) {
        fail("Invalid Maven coordinate: " + std::string(value));
    }
    return result;
}

std::string MavenCoordinate::path() const {
    std::string group_path = group;
    for (char& c : group_path) {
        if (c == '.') {
            c = '/';
        }
    }
    std::string file = artifact + "-" + version;
    if (!classifier.empty()) {
        file += "-" + classifier;
    }
    file += "." + extension;
    return group_path + "/" + artifact + "/" + version + "/" + file;
}

std::string MavenCoordinate::to_string() const {
    std::string result = group + ":" + artifact + ":" + version;
    if (!classifier.empty()) {
        result += ":" + classifier;
    }
    if (extension != "jar") {
        result += "@" + extension;
    }
    return result;
}

std::string maven_path(std::string_view coordinate) {
    return MavenCoordinate::parse(coordinate).path();
}

} // namespace aurora
