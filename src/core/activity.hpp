#pragma once

#include "core/json.hpp"

#include <ctime>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace aurora {

// Sessions shorter than this are not worth a heatmap square (and would make
// failed launches look like play time).
inline constexpr int k_min_session_seconds = 10;

struct PlaySession {
    std::string instance;
    std::string start;  // local time, "YYYY-MM-DDTHH:MM:SS"
    int seconds = 0;
};

struct ActivityDay {
    std::string date;  // local date, "YYYY-MM-DD"
    long long seconds = 0;
    int launches = 0;
};

struct ActivitySummary {
    std::vector<ActivityDay> days;
    long long total_seconds = 0;
    int total_launches = 0;
    int days_played = 0;
    std::string first_date;
    std::string last_date;
};

std::filesystem::path activity_file();

// Appends one completed session to activity.json. Returns false when the
// session is too short or the file cannot be written; never throws.
bool record_play_session(std::string_view instance,
                         std::time_t started,
                         int seconds,
                         const std::filesystem::path& file = activity_file());

std::vector<PlaySession> read_play_sessions(
    const std::filesystem::path& file = activity_file());

// activity.json plus launch banners from logs/aurora-launcher.log. Banner-only
// days keep their launch count but no duration, so past launches can show up
// without inventing play time.
ActivitySummary collect_activity(const std::filesystem::path& game_dir,
                                 const std::filesystem::path& file = activity_file());

std::string local_date_key(std::time_t time);
std::string local_iso8601(std::time_t time);

} // namespace aurora
