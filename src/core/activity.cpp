#include "core/activity.hpp"

#include "core/paths.hpp"
#include "core/strings.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>

namespace aurora {

namespace {

constexpr std::size_t k_max_sessions = 2000;
constexpr std::uintmax_t k_max_activity_bytes = 4ULL * 1024 * 1024;

bool is_date_key(std::string_view value) {
    if (value.size() != 10) {
        return false;
    }
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (index == 4 || index == 7) {
            if (value[index] != '-') {
                return false;
            }
        } else if (std::isdigit(static_cast<unsigned char>(value[index])) == 0) {
            return false;
        }
    }
    return true;
}

std::string format_local(std::time_t time, const char* pattern) {
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &time);
#else
    localtime_r(&time, &local);
#endif
    char buffer[32]{};
    std::strftime(buffer, sizeof(buffer), pattern, &local);
    return buffer;
}

Json load_root(const std::filesystem::path& file) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(file, error) || error) {
        return Json::object();
    }
    if (std::filesystem::file_size(file, error) > k_max_activity_bytes || error) {
        return Json::object();
    }
    try {
        const Json root = read_json_file(file);
        return root.is_object() ? root : Json::object();
    } catch (...) {
        return Json::object();
    }
}

bool write_root(const std::filesystem::path& file, const Json& root) {
    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);
    const std::filesystem::path temporary(file.string() + ".tmp");
    try {
        write_text_file(temporary, root.dump(2) + "\n");
    } catch (...) {
        return false;
    }
    std::filesystem::rename(temporary, file, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        return false;
    }
    return true;
}

void scan_banners(const std::filesystem::path& log_path, std::map<std::string, int>& counts) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(log_path, error) || error) {
        return;
    }
    std::ifstream input(log_path, std::ios::binary);
    if (!input) {
        return;
    }
    std::string line;
    while (std::getline(input, line)) {
        const std::size_t banner = line.find("====");
        if (banner == std::string::npos) {
            continue;
        }
        std::size_t start = banner + 4;
        while (start < line.size() && line[start] == ' ') {
            ++start;
        }
        if (start + 10 > line.size() ||
            (start + 10 < line.size() && line[start + 10] != ' ' && line[start + 10] != 'T')) {
            continue;
        }
        const std::string date = line.substr(start, 10);
        if (is_date_key(date)) {
            ++counts[date];
        }
    }
}

void collect_banners(const std::filesystem::path& game_dir, std::map<std::string, int>& counts) {
    scan_banners(game_dir / "logs" / "aurora-launcher.log", counts);
    std::error_code error;
    const std::filesystem::path versions = game_dir / "versions";
    if (!std::filesystem::is_directory(versions, error) || error) {
        return;
    }
    for (const auto& entry : std::filesystem::directory_iterator(versions, error)) {
        if (error) {
            break;
        }
        if (entry.is_directory(error) && !error) {
            scan_banners(entry.path() / "logs" / "aurora-launcher.log", counts);
        }
    }
}

} // namespace

std::filesystem::path activity_file() {
    return app_data_dir() / "activity.json";
}

std::string local_date_key(std::time_t time) {
    return format_local(time, "%Y-%m-%d");
}

std::string local_iso8601(std::time_t time) {
    return format_local(time, "%Y-%m-%dT%H:%M:%S");
}

std::vector<PlaySession> read_play_sessions(const std::filesystem::path& file) {
    std::vector<PlaySession> sessions;
    const Json root = load_root(file);
    const Json* list = json_array(root, "sessions");
    if (list == nullptr) {
        return sessions;
    }
    for (const Json& item : *list) {
        if (!item.is_object()) {
            continue;
        }
        PlaySession session;
        session.instance = json_string(item, "instance");
        session.start = json_string(item, "start");
        session.seconds = static_cast<int>(json_integer(item, "seconds", 0));
        if (session.seconds <= 0) {
            continue;
        }
        sessions.push_back(std::move(session));
    }
    return sessions;
}

bool record_play_session(std::string_view instance,
                         std::time_t started,
                         int seconds,
                         const std::filesystem::path& file) {
    if (seconds < k_min_session_seconds) {
        return false;
    }
    Json root = load_root(file);
    Json list = Json::array();
    if (const Json* existing = json_array(root, "sessions")) {
        list = *existing;
    }
    Json entry = Json::object();
    entry["instance"] = std::string(instance);
    entry["start"] = local_iso8601(started);
    entry["seconds"] = seconds;
    list.push_back(std::move(entry));
    if (list.size() > k_max_sessions) {
        Json trimmed = Json::array();
        for (std::size_t index = list.size() - k_max_sessions; index < list.size(); ++index) {
            trimmed.push_back(list[index]);
        }
        list = std::move(trimmed);
    }
    root["sessions"] = std::move(list);
    return write_root(file, root);
}

ActivitySummary collect_activity(const std::filesystem::path& game_dir,
                                 const std::filesystem::path& file) {
    std::map<std::string, ActivityDay> by_date;
    for (const PlaySession& session : read_play_sessions(file)) {
        if (session.start.size() < 10) {
            continue;
        }
        const std::string date = session.start.substr(0, 10);
        if (!is_date_key(date)) {
            continue;
        }
        ActivityDay& day = by_date[date];
        day.date = date;
        day.seconds += session.seconds;
        ++day.launches;
    }
    std::map<std::string, int> banners;
    collect_banners(game_dir, banners);
    for (const auto& [date, count] : banners) {
        ActivityDay& day = by_date[date];
        day.date = date;
        // The same launch writes both a session and a banner; take the larger
        // count instead of summing so recorded launches are not counted twice.
        day.launches = std::max(day.launches, count);
    }

    ActivitySummary summary;
    for (const auto& [date, day] : by_date) {
        summary.days.push_back(day);
    }
    for (const ActivityDay& day : summary.days) {
        summary.total_seconds += day.seconds;
        summary.total_launches += day.launches;
    }
    summary.days_played = static_cast<int>(summary.days.size());
    if (!summary.days.empty()) {
        summary.first_date = summary.days.front().date;
        summary.last_date = summary.days.back().date;
    }
    return summary;
}

} // namespace aurora
