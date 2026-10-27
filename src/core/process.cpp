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
                bool wait,
                const std::filesystem::path& output_log) {
    (void)executable;
    (void)arguments;
    (void)working_directory;
    (void)wait;
    (void)output_log;
#ifdef _WIN32
    const std::string command = build_command_line(executable, arguments);
    std::wstring wide_command = to_wide(command);
    const std::wstring wide_executable = executable.wstring();
    const std::wstring wide_working = working_directory.wstring();

    HANDLE log_handle = nullptr;
    HANDLE null_handle = nullptr;
    if (!output_log.empty()) {
        ensure_directory(output_log.parent_path());
        SECURITY_ATTRIBUTES attributes{};
        attributes.nLength = sizeof(attributes);
        attributes.bInheritHandle = TRUE;

        log_handle = CreateFileW(
            output_log.wstring().c_str(),
            FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            &attributes,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (log_handle == INVALID_HANDLE_VALUE) {
            log_handle = nullptr;
            fail("Unable to open process log: " + path_to_utf8(output_log) +
                 " (Windows error " + std::to_string(GetLastError()) + ")");
        }

        null_handle = CreateFileW(
            L"NUL",
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            &attributes,
            OPEN_EXISTING,
            0,
            nullptr);
        if (null_handle == INVALID_HANDLE_VALUE) {
            null_handle = nullptr;
            CloseHandle(log_handle);
            fail("Unable to open NUL device for process input (Windows error " +
                 std::to_string(GetLastError()) + ")");
        }

        const std::string header = "\n==== " + now_iso8601() + " ====\n" + command + "\n";
        DWORD written = 0;
        if (!WriteFile(
                log_handle, header.data(), static_cast<DWORD>(header.size()), &written, nullptr)) {
            CloseHandle(null_handle);
            CloseHandle(log_handle);
            fail("Unable to write process log: " + path_to_utf8(output_log));
        }
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    if (log_handle != nullptr) {
        startup.dwFlags |= STARTF_USESTDHANDLES;
        startup.hStdInput = null_handle;
        startup.hStdOutput = log_handle;
        startup.hStdError = log_handle;
    }
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
        const DWORD error = GetLastError();
        if (null_handle != nullptr) {
            CloseHandle(null_handle);
        }
        if (log_handle != nullptr) {
            CloseHandle(log_handle);
        }
        fail("Unable to start process: " + path_to_utf8(executable) + " (Windows error " +
             std::to_string(error) + ")");
    }
    if (null_handle != nullptr) {
        CloseHandle(null_handle);
    }
    if (log_handle != nullptr) {
        CloseHandle(log_handle);
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
