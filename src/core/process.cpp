#include "core/process.hpp"

#include "core/error.hpp"
#include "core/paths.hpp"
#include "core/strings.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace aurora {

std::string build_command_line(const std::filesystem::path& executable,
                               const std::vector<std::string>& arguments) {
    std::string result = quote_windows_arg(path_to_utf8(executable));
    for (const std::string& argument : arguments) {
        result.push_back(' ');
        result += quote_windows_arg(argument);
    }
    return result;
}

int run_process(const std::filesystem::path& executable,
                const std::vector<std::string>& arguments,
                const std::filesystem::path& working_directory,
                bool wait) {
    (void)executable;
    (void)arguments;
    (void)working_directory;
    (void)wait;
#ifdef _WIN32
    const std::string command = build_command_line(executable, arguments);
    std::wstring wide_command = to_wide(command);
    const std::wstring wide_executable = executable.wstring();
    const std::wstring wide_working = working_directory.wstring();

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(
            wide_executable.c_str(),
            wide_command.data(),
            nullptr,
            nullptr,
            TRUE,
            0,
            nullptr,
            wide_working.empty() ? nullptr : wide_working.c_str(),
            &startup,
            &process)) {
        fail("Unable to start process: " + path_to_utf8(executable) + " (Windows error " +
             std::to_string(GetLastError()) + ")");
    }

    if (!wait) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return 0;
    }

    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exit_code = 0;
    if (!GetExitCodeProcess(process.hProcess, &exit_code)) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        fail("Unable to read process exit code");
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return static_cast<int>(exit_code);
#else
    fail("Process launching is not implemented on this platform");
#endif
}

} // namespace aurora
