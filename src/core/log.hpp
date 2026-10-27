#pragma once

#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>

namespace aurora {

enum class LogLevel { Debug = 0, Info = 1, Warn = 2, Error = 3 };

class Logger {
public:
    static Logger& instance();

    void set_level(LogLevel level);
    LogLevel level() const;

    void set_file(const std::filesystem::path& path);
    void clear_file();

    void write(LogLevel level, std::string_view message);

private:
    Logger() = default;

    mutable std::mutex mutex_;
    LogLevel level_ = LogLevel::Info;
    std::ofstream file_;
};

void log_debug(std::string_view message);
void log_info(std::string_view message);
void log_warn(std::string_view message);
void log_error(std::string_view message);

} // namespace aurora
