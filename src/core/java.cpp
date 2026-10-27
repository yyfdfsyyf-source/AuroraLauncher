#include "core/java.hpp"

#include "core/error.hpp"
#include "core/log.hpp"
#include "core/paths.hpp"
#include "core/strings.hpp"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <map>
#include <set>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace aurora {

namespace {

std::string strip_quotes(std::string value) {
    value = trim(value);
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value = value.substr(1, value.size() - 2);
    }
    return value;
}

int parse_java_major(const std::string& version) {
    if (version.rfind("1.", 0) == 0) {
        int major = 0;
        for (std::size_t i = 2; i < version.size() && std::isdigit(static_cast<unsigned char>(version[i])); ++i) {
            major = major * 10 + (version[i] - '0');
        }
        return major;
    }
    int major = 0;
    for (const char c : version) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            break;
        }
        major = major * 10 + (c - '0');
    }
    return major;
}

bool has_java_binary(const std::filesystem::path& home) {
#ifdef _WIN32
    return std::filesystem::is_regular_file(home / "bin" / "java.exe");
#else
    return std::filesystem::is_regular_file(home / "bin" / "java");
#endif
}

std::string normalized_key(const std::filesystem::path& path) {
    return to_lower_ascii(path_to_utf8(absolute_path(path).lexically_normal()));
}

void add_candidate(std::vector<std::pair<std::filesystem::path, std::string>>& candidates,
                   std::set<std::string>& seen,
                   const std::filesystem::path& path,
                   std::string source) {
    if (path.empty()) {
        return;
    }
    const auto normalized = absolute_path(path);
    const std::string key = normalized_key(normalized);
    if (!seen.insert(key).second) {
        return;
    }
    candidates.emplace_back(normalized, std::move(source));
}

#ifdef _WIN32
void read_registry_java_homes(HKEY root,
                              const wchar_t* subkey,
                              std::vector<std::pair<std::filesystem::path, std::string>>& candidates,
                              std::set<std::string>& seen,
                              std::string source) {
    for (const REGSAM view : {KEY_WOW64_64KEY, KEY_WOW64_32KEY}) {
        HKEY key = nullptr;
        if (RegOpenKeyExW(root, subkey, 0, KEY_READ | KEY_ENUMERATE_SUB_KEYS | view, &key) !=
            ERROR_SUCCESS) {
            continue;
        }
        DWORD index = 0;
        wchar_t name[256];
        while (true) {
            DWORD name_length = static_cast<DWORD>(std::size(name));
            const LSTATUS status = RegEnumKeyExW(
                key, index++, name, &name_length, nullptr, nullptr, nullptr, nullptr);
            if (status == ERROR_NO_MORE_ITEMS) {
                break;
            }
            if (status != ERROR_SUCCESS) {
                break;
            }
            HKEY version_key = nullptr;
            if (RegOpenKeyExW(key, name, 0, KEY_READ | view, &version_key) != ERROR_SUCCESS) {
                continue;
            }
            wchar_t value[1024];
            DWORD value_size = sizeof(value);
            DWORD type = 0;
            if (RegQueryValueExW(
                    version_key, L"JavaHome", nullptr, &type,
                    reinterpret_cast<LPBYTE>(value), &value_size) == ERROR_SUCCESS &&
                type == REG_SZ) {
                add_candidate(candidates, seen, std::filesystem::path(value), source);
            }
            RegCloseKey(version_key);
        }
        RegCloseKey(key);
    }
}

void scan_common_directories(
    std::vector<std::pair<std::filesystem::path, std::string>>& candidates,
    std::set<std::string>& seen) {
    std::vector<std::filesystem::path> roots;
    for (const char* variable : {"ProgramFiles", "ProgramW6432", "ProgramFiles(x86)"}) {
        const auto value = environment_value(variable);
        if (value.empty()) {
            continue;
        }
        const auto base = path_from_utf8(value);
        roots.push_back(base / "Java");
        roots.push_back(base / "Eclipse Adoptium");
        roots.push_back(base / "Microsoft");
        roots.push_back(base / "Amazon Corretto");
        roots.push_back(base / "Zulu");
        roots.push_back(base / "BellSoft");
        roots.push_back(base / "IBM");
        roots.push_back(base / "Semeru");
    }
    const auto local = environment_value("LOCALAPPDATA");
    if (!local.empty()) {
        const auto base = path_from_utf8(local);
        roots.push_back(base / "Programs" / "Eclipse Adoptium");
        roots.push_back(base / "Programs" / "Microsoft");
        roots.push_back(base / "Programs" / "Java");
    }
    roots.push_back(home_dir() / ".jdks");
    roots.push_back(home_dir() / ".gradle" / "jdks");
    roots.push_back(home_dir() / "scoop" / "apps");

    for (const auto& root : roots) {
        std::error_code ec;
        if (!std::filesystem::is_directory(root, ec)) {
            continue;
        }
        add_candidate(candidates, seen, root, "scan");
        for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
            if (ec) {
                break;
            }
            if (!entry.is_directory()) {
                continue;
            }
            add_candidate(candidates, seen, entry.path(), "scan");
        }
    }
}
#endif

} // namespace

std::optional<JavaRuntime> probe_java_home(const std::filesystem::path& home,
                                           std::string source) {
    const auto normalized = absolute_path(home);
    if (!has_java_binary(normalized)) {
        return std::nullopt;
    }

    std::string version;
    std::string vendor;
    const auto release = normalized / "release";
    std::error_code ec;
    if (std::filesystem::is_regular_file(release, ec)) {
        try {
            const std::string text = read_text_file(release);
            for (const std::string& line : split(text, '\n')) {
                const auto equals = line.find('=');
                if (equals == std::string::npos) {
                    continue;
                }
                const std::string key = trim(line.substr(0, equals));
                const std::string value = strip_quotes(line.substr(equals + 1));
                if (key == "JAVA_VERSION") {
                    version = value;
                } else if (key == "IMPLEMENTOR" || key == "JAVA_VENDOR") {
                    if (vendor.empty()) {
                        vendor = value;
                    }
                }
            }
        } catch (const Error&) {
            // A JDK release file is optional; the directory may still be usable.
        }
    }
    if (version.empty()) {
        return std::nullopt;
    }

    JavaRuntime runtime;
    runtime.home = normalized;
#ifdef _WIN32
    runtime.executable = normalized / "bin" / "javaw.exe";
    if (!std::filesystem::is_regular_file(runtime.executable, ec)) {
        runtime.executable = normalized / "bin" / "java.exe";
    }
#else
    runtime.executable = normalized / "bin" / "java";
#endif
    runtime.major = parse_java_major(version);
    runtime.version = version;
    runtime.vendor = vendor;
    runtime.source = std::move(source);
    return runtime;
}

std::vector<JavaRuntime> detect_java_runtimes() {
    std::vector<std::pair<std::filesystem::path, std::string>> candidates;
    std::set<std::string> seen;

    add_candidate(candidates, seen, path_from_utf8(environment_value("JAVA_HOME")), "JAVA_HOME");
    add_candidate(candidates, seen, path_from_utf8(environment_value("JDK_HOME")), "JDK_HOME");

#ifdef _WIN32
    const auto path_value = environment_value("PATH");
    for (const std::string& entry : split(path_value, ';')) {
        if (entry.empty()) {
            continue;
        }
        const auto directory = path_from_utf8(strip_quotes(entry));
        if (std::filesystem::is_regular_file(directory / "java.exe")) {
            const auto home = directory.filename() == "bin" ? directory.parent_path() : directory;
            add_candidate(candidates, seen, home, "PATH");
        }
    }

    for (const HKEY root : {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER}) {
        read_registry_java_homes(root, L"SOFTWARE\\JavaSoft\\JDK", candidates, seen, "registry");
        read_registry_java_homes(
            root, L"SOFTWARE\\JavaSoft\\Java Development Kit", candidates, seen, "registry");
        read_registry_java_homes(
            root, L"SOFTWARE\\JavaSoft\\Java Runtime Environment", candidates, seen, "registry");
        read_registry_java_homes(root, L"SOFTWARE\\JavaSoft\\JRE", candidates, seen, "registry");
    }
    scan_common_directories(candidates, seen);
#else
    const auto path_value = environment_value("PATH");
    for (const std::string& entry : split(path_value, ':')) {
        const auto directory = path_from_utf8(strip_quotes(entry));
        if (std::filesystem::is_regular_file(directory / "java")) {
            const auto home = directory.filename() == "bin" ? directory.parent_path() : directory;
            add_candidate(candidates, seen, home, "PATH");
        }
    }
#endif

    std::vector<JavaRuntime> runtimes;
    std::set<std::string> executable_seen;
    for (const auto& [path, source] : candidates) {
        auto runtime = probe_java_home(path, source);
        if (!runtime) {
            continue;
        }
        const std::string executable_key = normalized_key(runtime->executable);
        if (!executable_seen.insert(executable_key).second) {
            continue;
        }
        runtimes.push_back(std::move(*runtime));
    }
    std::sort(runtimes.begin(), runtimes.end(), [](const JavaRuntime& left, const JavaRuntime& right) {
        if (left.major != right.major) {
            return left.major < right.major;
        }
        return left.home < right.home;
    });
    return runtimes;
}

std::optional<JavaRuntime> find_java_runtime(int required_major) {
    const auto runtimes = detect_java_runtimes();
    for (const JavaRuntime& runtime : runtimes) {
        if (runtime.major > 0 && (required_major <= 0 || runtime.major >= required_major)) {
            return runtime;
        }
    }
    return std::nullopt;
}

} // namespace aurora
