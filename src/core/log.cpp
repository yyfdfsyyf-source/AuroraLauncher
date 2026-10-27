#include "core/log.hpp"

#include "core/error.hpp"
#include "core/strings.hpp"

#include <iostream>

namespace aurora {

namespace {
const char* level_name(LogLevel level) {
    switch (level) {
    case LogLevel::Debug:
        return "DEBUG";
    case LogLevel::Info:
        return "INFO";
    case LogLevel::Warn:
        return "WARN";
    case LogLevel::Error:
        return "ERROR";
    }
    return "INFO";
}
} // namespace

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

void Logger::set_level(LogLevel level) {
    std::lock_guard lock(mutex_);
    level_ = level;
}

LogLevel Logger::level() const {
    std::lock_guard lock(mutex_);
    return level_;
}

void Logger::set_file(const std::filesystem::path& path) {
    std::lock_guard lock(mutex_);
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            fail("Unable to create log directory: " + path_to_utf8(path.parent_path()) +
                 " (" + ec.message() + ")");
        }
    }
    file_.open(path, std::ios::out | std::ios::app);
    if (!file_) {
        fail("Unable to open log file: " + path_to_utf8(path));
    }
}

void Logger::clear_file() {
    std::lock_guard lock(mutex_);
    if (file_.is_open()) {
        file_.close();
    }
}

void Logger::write(LogLevel level, std::string_view message) {
    std::lock_guard lock(mutex_);
    if (level < level_) {
        return;
    }
    const std::string line =
        "[" + now_iso8601() + "][" + level_name(level) + "] " + std::string(message);
    std::cerr << line << '\n';
    if (file_.is_open()) {
        file_ << line << '\n';
        file_.flush();
    }
}

void log_debug(std::string_view message) {
    Logger::instance().write(LogLevel::Debug, message);
}

void log_info(std::string_view message) {
    Logger::instance().write(LogLevel::Info, message);
}

void log_warn(std::string_view message) {
    Logger::instance().write(LogLevel::Warn, message);
}

void log_error(std::string_view message) {
    Logger::instance().write(LogLevel::Error, message);
}

} // namespace aurora
