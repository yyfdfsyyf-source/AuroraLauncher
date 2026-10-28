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

namespace {

constexpr std::uint64_t k_infinite_wait = 0xFFFFFFFFFFFFFFFFull;

} // namespace

std::string build_command_line(const std::filesystem::path& executable,
                               const std::vector<std::string>& arguments) {
    std::string result = quote_windows_arg(path_to_utf8(executable));
    for (const std::string& argument : arguments) {
        result.push_back(' ');
        result += quote_windows_arg(argument);
    }
    return result;
}

#ifdef _WIN32

namespace {

struct StartedProcess {
    HANDLE process = nullptr;
    std::int64_t process_id = 0;
};

StartedProcess start_windows_process(const std::filesystem::path& executable,
                                     const std::vector<std::string>& arguments,
                                     const std::filesystem::path& working_directory,
                                     const std::filesystem::path& output_log) {
    std::wstring wide_command = to_wide(build_command_line(executable, arguments));
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

        // Only the executable is named here: the arguments carry the access token.
        const std::string header =
            "\n==== " + now_iso8601() + " ====\n" + path_to_utf8(executable) + "\n";
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
    CloseHandle(process.hThread);
    return StartedProcess{process.hProcess, static_cast<std::int64_t>(process.dwProcessId)};
}

} // namespace

ProcessHandle::ProcessHandle(void* process, std::int64_t process_id)
    : process_(process), process_id_(process_id) {}

ProcessHandle::~ProcessHandle() {
    if (process_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(process_));
    }
}

ProcessHandle::ProcessHandle(ProcessHandle&& other) noexcept
    : process_(other.process_), process_id_(other.process_id_) {
    other.process_ = nullptr;
    other.process_id_ = 0;
}

ProcessHandle& ProcessHandle::operator=(ProcessHandle&& other) noexcept {
    if (this != &other) {
        if (process_ != nullptr) {
            CloseHandle(static_cast<HANDLE>(process_));
        }
        process_ = other.process_;
        process_id_ = other.process_id_;
        other.process_ = nullptr;
        other.process_id_ = 0;
    }
    return *this;
}

std::int64_t ProcessHandle::process_id() const {
    return process_id_;
}

bool ProcessHandle::valid() const {
    return process_ != nullptr;
}

bool ProcessHandle::running() const {
    if (process_ == nullptr) {
        return false;
    }
    return WaitForSingleObject(static_cast<HANDLE>(process_), 0) == WAIT_TIMEOUT;
}

int ProcessHandle::wait(std::uint64_t timeout_ms) const {
    if (process_ == nullptr) {
        fail("Process handle is not open");
    }
    const DWORD result = WaitForSingleObject(
        static_cast<HANDLE>(process_),
        timeout_ms >= k_infinite_wait
            ? INFINITE
            : static_cast<DWORD>(timeout_ms > 0xFFFFFFFEull ? 0xFFFFFFFEull : timeout_ms));
    if (result == WAIT_TIMEOUT) {
        return -1;
    }
    if (result != WAIT_OBJECT_0) {
        fail("Unable to wait for process (Windows error " + std::to_string(GetLastError()) + ")");
    }
    DWORD exit_code = 0;
    if (!GetExitCodeProcess(static_cast<HANDLE>(process_), &exit_code)) {
        fail("Unable to read process exit code");
    }
    return static_cast<int>(exit_code);
}

bool ProcessHandle::terminate() const {
    if (process_ == nullptr) {
        return false;
    }
    return TerminateProcess(static_cast<HANDLE>(process_), 1) != 0;
}

ProcessHandle spawn_process(const std::filesystem::path& executable,
                            const std::vector<std::string>& arguments,
                            const std::filesystem::path& working_directory,
                            const std::filesystem::path& output_log) {
    const StartedProcess started =
        start_windows_process(executable, arguments, working_directory, output_log);
    return ProcessHandle{started.process, started.process_id};
}

int run_process(const std::filesystem::path& executable,
                const std::vector<std::string>& arguments,
                const std::filesystem::path& working_directory,
                bool wait,
                const std::filesystem::path& output_log) {
    ProcessHandle process = spawn_process(executable, arguments, working_directory, output_log);
    if (!wait) {
        return 0;
    }
    return process.wait(k_infinite_wait);
}

#else

ProcessHandle::ProcessHandle(void* process, std::int64_t process_id)
    : process_(process), process_id_(process_id) {}

ProcessHandle::~ProcessHandle() = default;

ProcessHandle::ProcessHandle(ProcessHandle&& other) noexcept
    : process_(other.process_), process_id_(other.process_id_) {
    other.process_ = nullptr;
    other.process_id_ = 0;
}

ProcessHandle& ProcessHandle::operator=(ProcessHandle&& other) noexcept {
    if (this != &other) {
        process_ = other.process_;
        process_id_ = other.process_id_;
        other.process_ = nullptr;
        other.process_id_ = 0;
    }
    return *this;
}

std::int64_t ProcessHandle::process_id() const {
    return process_id_;
}

bool ProcessHandle::valid() const {
    return process_ != nullptr;
}

bool ProcessHandle::running() const {
    return false;
}

int ProcessHandle::wait(std::uint64_t) const {
    fail("Process launching is not implemented on this platform");
}

bool ProcessHandle::terminate() const {
    return false;
}

ProcessHandle spawn_process(const std::filesystem::path&,
                            const std::vector<std::string>&,
                            const std::filesystem::path&,
                            const std::filesystem::path&) {
    fail("Process launching is not implemented on this platform");
}

int run_process(const std::filesystem::path&,
                const std::vector<std::string>&,
                const std::filesystem::path&,
                bool,
                const std::filesystem::path&) {
    fail("Process launching is not implemented on this platform");
}

#endif

} // namespace aurora
