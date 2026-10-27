#pragma once

#include "core/json.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace aurora {

struct Environment {
    std::string os_name;
    std::string arch;
    std::string os_version;
    std::map<std::string, bool> features;
};

Environment detect_environment();

struct OsConstraint {
    std::optional<std::string> name;
    std::optional<std::string> arch;
    std::optional<std::string> version;
};

struct Rule {
    std::string action;
    std::optional<OsConstraint> os;
    std::map<std::string, bool> features;
};

Rule parse_rule(const Json& value);
bool rule_matches(const Rule& rule, const Environment& environment);
bool rules_allow(const std::vector<Rule>& rules, const Environment& environment);

std::string architecture_bits(const std::string& arch);

} // namespace aurora
