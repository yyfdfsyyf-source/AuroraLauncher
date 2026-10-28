#include "core/activity.hpp"
#include "core/paths.hpp"

#include <chrono>
#include <ctime>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

std::filesystem::path make_temp_dir(const std::string& name) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
                      ("aurora-activity-tests-" + name + "-" + std::to_string(stamp));
    std::filesystem::create_directories(path);
    return path;
}

void cleanup(const std::filesystem::path& directory) {
    std::error_code error;
    std::filesystem::remove_all(directory, error);
}

std::time_t local_time(int year, int month, int day) {
    std::tm value{};
    value.tm_year = year - 1900;
    value.tm_mon = month - 1;
    value.tm_mday = day;
    value.tm_hour = 12;
    return std::mktime(&value);
}

void test_record_roundtrip() {
    const auto directory = make_temp_dir("record");
    const auto file = directory / "activity.json";

    expect(!aurora::record_play_session("too-short", local_time(2026, 10, 5), 3, file),
           "sessions under the minimum are rejected");
    expect(!std::filesystem::exists(file), "a rejected session writes nothing");

    expect(aurora::record_play_session("1.19.2", local_time(2026, 10, 5), 600, file),
           "a real session is written");
    const auto sessions = aurora::read_play_sessions(file);
    expect(sessions.size() == 1, "one session is read back");
    if (sessions.size() == 1) {
        expect(sessions[0].instance == "1.19.2", "instance name round-trips");
        expect(sessions[0].start.rfind("2026-10-05T", 0) == 0, "start is an ISO local time");
        expect(sessions[0].seconds == 600, "duration round-trips");
    }

    expect(aurora::record_play_session("fabric", local_time(2026, 10, 6), 1200, file),
           "a second session is appended");
    expect(aurora::read_play_sessions(file).size() == 2, "sessions append");
    cleanup(directory);
}

void test_malformed_file() {
    const auto directory = make_temp_dir("broken");
    const auto file = directory / "activity.json";
    aurora::write_text_file(file, "{ this is not json");
    expect(aurora::read_play_sessions(file).empty(), "a broken file reads as empty");
    expect(aurora::record_play_session("x", local_time(2026, 10, 5), 60, file),
           "recording repairs a broken file");
    expect(aurora::read_play_sessions(file).size() == 1, "repaired file has the new session");
    cleanup(directory);
}

void test_collect_merges_banners_and_sessions() {
    const auto directory = make_temp_dir("collect");
    const auto game_dir = directory / "game";
    const auto file = directory / "activity.json";

    const auto log_dir = game_dir / "versions" / "1.19.2" / "logs";
    std::filesystem::create_directories(log_dir);
    aurora::write_text_file(
        log_dir / "aurora-launcher.log",
        "==== 2026-10-01T18:00:00 ====\n"
        "D:\\Java\\bin\\javaw.exe\n"
        "==== 2026-10-02T09:30:00 ====\n"
        "==== 2026-10-05T20:00:00 ====\n"
        "==== 2026-10-05T22:15:00 ====\n");
    const auto shared_log_dir = game_dir / "logs";
    std::filesystem::create_directories(shared_log_dir);
    aurora::write_text_file(shared_log_dir / "aurora-launcher.log",
                            "==== 2026-10-03T11:00:00 ====\nnot a banner with ==== inside\n");

    aurora::record_play_session("1.19.2", local_time(2026, 10, 5), 600, file);
    aurora::record_play_session("1.19.2", local_time(2026, 10, 6), 1800, file);

    const auto summary = aurora::collect_activity(game_dir, file);
    expect(summary.days_played == 5, "five days are aggregated");
    expect(summary.total_seconds == 2400, "durations only come from recorded sessions");
    expect(summary.total_launches == 6, "launch counts merge banners and sessions");
    expect(summary.first_date == "2026-10-01", "first date is the earliest");
    expect(summary.last_date == "2026-10-06", "last date is the latest");

    const auto day = [&summary](const std::string& date) {
        for (const auto& entry : summary.days) {
            if (entry.date == date) {
                return entry;
            }
        }
        return aurora::ActivityDay{};
    };
    expect(day("2026-10-01").seconds == 0 && day("2026-10-01").launches == 1,
           "banner-only day keeps the launch count");
    expect(day("2026-10-03").launches == 1, "shared game dir banners are counted");
    expect(day("2026-10-05").seconds == 600 && day("2026-10-05").launches == 2,
           "a recorded day takes the larger launch count instead of summing");
    expect(day("2026-10-06").seconds == 1800 && day("2026-10-06").launches == 1,
           "recorded day without banners is intact");
    cleanup(directory);
}

void test_date_helpers() {
    const std::time_t time = local_time(2026, 2, 7);
    expect(aurora::local_date_key(time) == "2026-02-07", "local date key format");
    expect(aurora::local_iso8601(time).rfind("2026-02-07T12:00:00", 0) == 0,
           "local ISO timestamp format");
}

} // namespace

int main() {
    try {
        test_record_roundtrip();
        test_malformed_file();
        test_collect_merges_banners_and_sessions();
        test_date_helpers();
    } catch (const std::exception& error) {
        std::cerr << "Unexpected exception: " << error.what() << '\n';
        return 1;
    }

    if (failures != 0) {
        std::cerr << failures << " activity test(s) failed\n";
        return 1;
    }
    std::cout << "All activity tests passed\n";
    return 0;
}
