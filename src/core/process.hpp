#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace aurora {

std::string build_command_line(const std::filesystem::path& executable,
                               const std::vector<std::string>& arguments);

// Runs a process without a shell. When output_log is non-empty, stdout and
// stderr are appended to that file and stdin is redirected from NUL so a
// detached child cannot block waiting for console input.
// The log never contains the command line: game arguments carry the access token.
int run_process(const std::filesystem::path& executable,
                const std::vector<std::string>& arguments,
                const std::filesystem::path& working_directory,
                bool wait = true,
                const std::filesystem::path& output_log = {});

// A started child process. The handle is owned by the object and closed on
// destruction, so a launcher UI can poll and stop the game without keeping
// a blocking thread alive.
class ProcessHandle {
public:
    ProcessHandle() = default;
    ~ProcessHandle();
    ProcessHandle(ProcessHandle&& other) noexcept;
    ProcessHandle& operator=(ProcessHandle&& other) noexcept;
    ProcessHandle(const ProcessHandle&) = delete;
    ProcessHandle& operator=(const ProcessHandle&) = delete;

    std::int64_t process_id() const;
    bool valid() const;
    bool running() const;
    // Waits for the child to exit. Returns its exit code, or -1 when the
    // timeout expires while the process is still running.
    int wait(std::uint64_t timeout_ms) const;
    // Forceful termination: a running Minecraft loses unsaved world changes.
    bool terminate() const;

private:
    explicit ProcessHandle(void* process, std::int64_t process_id);

    void* process_ = nullptr;
    std::int64_t process_id_ = 0;

    friend ProcessHandle spawn_process(const std::filesystem::path&,
                                       const std::vector<std::string>&,
                                       const std::filesystem::path&,
                                       const std::filesystem::path&);
};

ProcessHandle spawn_process(const std::filesystem::path& executable,
                            const std::vector<std::string>& arguments,
                            const std::filesystem::path& working_directory,
                            const std::filesystem::path& output_log = {});

} // namespace aurora
