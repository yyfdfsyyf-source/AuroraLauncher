#include "core/rules.hpp"

#include "core/error.hpp"
#include "core/strings.hpp"

#include <regex>
#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace aurora {

std::string architecture_bits(const std::string& arch) {
    if (arch == "x86_64") {
        return "64";
    }
    if (arch == "x86") {
        return "32";
    }
    return arch;
}

#ifdef _WIN32
namespace {
std::string windows_version() {
    using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    const HMODULE module = GetModuleHandleW(L"ntdll.dll");
    if (module != nullptr) {
        const auto function = reinterpret_cast<RtlGetVersionFn>(
            GetProcAddress(module, "RtlGetVersion"));
        if (function != nullptr) {
            RTL_OSVERSIONINFOW info{};
            info.dwOSVersionInfoSize = sizeof(info);
            if (function(&info) == 0) {
                std::ostringstream out;
                out << info.dwMajorVersion << '.' << info.dwMinorVersion << '.'
                    << info.dwBuildNumber;
                return out.str();
            }
        }
    }
    return "10.0.0";
}
} // namespace
#endif

Environment detect_environment() {
    Environment result;
#ifdef _WIN32
    result.os_name = "windows";
    result.os_version = windows_version();
#ifdef _M_ARM64
    result.arch = "arm64";
#elif defined(_M_X64) || defined(__x86_64__)
    result.arch = "x86_64";
#else
    result.arch = "x86";
#endif
#elif defined(__APPLE__)
    result.os_name = "osx";
    result.arch = sizeof(void*) == 8 ? "x86_64" : "x86";
#elif defined(__linux__)
    result.os_name = "linux";
    result.arch = sizeof(void*) == 8 ? "x86_64" : "x86";
#else
    result.os_name = "unknown";
    result.arch = sizeof(void*) == 8 ? "x86_64" : "x86";
#endif
    result.features["is_demo_user"] = false;
    result.features["has_custom_resolution"] = false;
    result.features["has_quick_plays_support"] = false;
    result.features["is_quick_play_singleplayer"] = false;
    result.features["is_quick_play_multiplayer"] = false;
    result.features["is_quick_play_realms"] = false;
    return result;
}

Rule parse_rule(const Json& value) {
    if (!value.is_object()) {
        fail("Rule must be a JSON object");
    }
    Rule rule;
    rule.action = json_string(value, "action");
    if (rule.action != "allow" && rule.action != "disallow") {
        fail("Rule has an invalid action");
    }
    if (const Json* os = json_object(value, "os")) {
        OsConstraint constraint;
        constraint.name = json_optional_string(*os, "name");
        constraint.arch = json_optional_string(*os, "arch");
        constraint.version = json_optional_string(*os, "version");
        rule.os = std::move(constraint);
    }
    if (const Json* features = json_object(value, "features")) {
        for (auto it = features->begin(); it != features->end(); ++it) {
            if (it.value().is_boolean()) {
                rule.features[it.key()] = it.value().get<bool>();
            }
        }
    }
    return rule;
}

bool rule_matches(const Rule& rule, const Environment& environment) {
    if (rule.os) {
        if (rule.os->name && *rule.os->name != environment.os_name) {
            return false;
        }
        if (rule.os->arch && *rule.os->arch != environment.arch) {
            return false;
        }
        if (rule.os->version) {
            try {
                if (!std::regex_match(environment.os_version, std::regex(*rule.os->version))) {
                    return false;
                }
            } catch (const std::regex_error&) {
                if (environment.os_version != *rule.os->version) {
                    return false;
                }
            }
        }
    }
    for (const auto& [name, expected] : rule.features) {
        const auto it = environment.features.find(name);
        const bool actual = it != environment.features.end() && it->second;
        if (actual != expected) {
            return false;
        }
    }
    return true;
}

bool rules_allow(const std::vector<Rule>& rules, const Environment& environment) {
    if (rules.empty()) {
        return true;
    }
    bool allowed = false;
    for (const Rule& rule : rules) {
        if (rule_matches(rule, environment)) {
            allowed = rule.action == "allow";
        }
    }
    return allowed;
}

} // namespace aurora
