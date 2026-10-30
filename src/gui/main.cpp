#include "core/activity.hpp"
#include "core/config.hpp"
#include "core/error.hpp"
#include "core/java.hpp"
#include "core/json.hpp"
#include "core/launch.hpp"
#include "core/mods.hpp"
#include "core/paths.hpp"
#include "core/pcl.hpp"
#include "core/process.hpp"
#include "core/repository.hpp"
#include "core/rules.hpp"
#include "core/strings.hpp"
#include "core/zip.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>

// WIN32_LEAN_AND_MEAN (set by aurora_core) keeps <windows.h> from pulling in the
// COM headers that WebView2.h needs.
#include <objbase.h>

#include <WebView2.h>
#include <wrl.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <cwchar>
#include <fstream>
#include <initializer_list>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#pragma comment(lib, "ole32.lib")

using namespace aurora;

namespace {

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::RuntimeClass;
using Microsoft::WRL::RuntimeClassFlags;
using Microsoft::WRL::ClassicCom;

constexpr UINT k_poll_timer = 1;
constexpr int k_resize_border = 4;
constexpr std::uint64_t k_log_chunk_bytes = 64ULL * 1024ULL;
constexpr DWORD k_per_monitor_dpi_aware_v2 = static_cast<DWORD>(-4);

// The first screen only lists instances, starts and stops the game, and tails
// logs. Downloads and installs stay in the CLI until the UI is stable.
struct Tail {
    std::filesystem::path path;
    std::uint64_t offset = 0;
};

struct AppState {
    HWND window = nullptr;
    ComPtr<ICoreWebView2> webview;
    ComPtr<ICoreWebView2_3> webview3;
    ComPtr<ICoreWebView2Controller> controller;
    Config config;
    std::filesystem::path config_path;
    ProcessHandle game;
    std::string launched_id;
    int game_exit_code = 0;
    bool stopped_by_user = false;
    std::time_t session_started = 0;
    bool session_open = false;
    Tail launcher_log;
    Tail game_log;
    Json textures;
    HBRUSH rim_brush = nullptr;
    HBRUSH header_brush = nullptr;
};

AppState state;

std::filesystem::path ui_directory() {
    const std::filesystem::path source = path_from_utf8(AURORA_GUI_UI_DIR);
    if (std::filesystem::is_directory(source)) {
        return source;
    }
    wchar_t buffer[MAX_PATH]{};
    GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    return std::filesystem::path(buffer).parent_path() / "ui";
}

void post_json(const std::string& json_text) {
    if (state.webview == nullptr) {
        return;
    }
    state.webview->PostWebMessageAsJson(to_wide(json_text).c_str());
}

void post_event(const std::string& name, const Json& payload) {
    Json message = Json::object();
    message["event"] = name;
    message["data"] = payload;
    post_json(message.dump());
}

void post_error(const std::string& message) {
    Json payload = Json::object();
    payload["message"] = message;
    post_event("error", payload);
}

// The WebView stops short of the window edge so the parent window still
// receives WM_NCHITTEST for resize borders on a frameless window.
void update_webview_bounds() {
    if (state.controller == nullptr) {
        return;
    }
    RECT rect{};
    GetClientRect(state.window, &rect);
    rect.left += k_resize_border;
    rect.top += k_resize_border;
    rect.right -= k_resize_border;
    rect.bottom -= k_resize_border;
    if (rect.right < rect.left) {
        rect.right = rect.left;
    }
    if (rect.bottom < rect.top) {
        rect.bottom = rect.top;
    }
    state.controller->put_Bounds(rect);
}

void toggle_maximize(HWND window) {
    ShowWindow(window, IsZoomed(window) != 0 ? SW_RESTORE : SW_MAXIMIZE);
}

void center_in_work_area(HWND window) {
    RECT rect{};
    if (GetWindowRect(window, &rect) == 0) {
        return;
    }
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &info) == 0) {
        return;
    }
    const RECT work = info.rcWork;
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    const int x = work.left + ((work.right - work.left) - width) / 2;
    const int y = work.top + ((work.bottom - work.top) - height) / 2;
    SetWindowPos(window, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

Json settings_payload() {
    Json settings = Json::object();
    settings["gameDir"] = path_to_utf8(state.config.game_dir);
    settings["username"] = state.config.username;
    settings["memoryMb"] = state.config.memory_mb;
    settings["javaPath"] = path_to_utf8(state.config.java_path);
    settings["width"] = state.config.width;
    settings["height"] = state.config.height;
    return settings;
}

std::string loader_kind(const VersionMeta& meta) {
    const std::string main_class = to_lower_ascii(meta.main_class);
    if (main_class.find("fabricmc") != std::string::npos) {
        return "fabric";
    }
    if (main_class.find("neoforged") != std::string::npos) {
        return "neoforge";
    }
    if (main_class.find("quiltmc") != std::string::npos) {
        return "quilt";
    }
    if (main_class.find("minecraftforge") != std::string::npos ||
        main_class.find("modlauncher") != std::string::npos ||
        main_class.find("bootstraplauncher") != std::string::npos ||
        main_class.find("launchwrapper") != std::string::npos) {
        return "forge";
    }
    return "vanilla";
}

std::uint64_t file_size_or_zero(const std::filesystem::path& path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    return error ? 0 : size;
}

std::string after_last_colon(const std::string& coordinate) {
    const std::size_t position = coordinate.find_last_of(':');
    return position == std::string::npos ? std::string() : coordinate.substr(position + 1);
}

// Flattened instances (PCL writes these) carry no inheritsFrom, so the Minecraft
// version comes from the PCL config, the modpack marker, or the loader library.
std::string guess_minecraft_version(const ResolvedVersion& version,
                                    const PclInstanceSettings& pcl) {
    if (!version.meta.inherits_from.empty()) {
        return version.meta.inherits_from;
    }
    if (!pcl.minecraft_version.empty()) {
        return pcl.minecraft_version;
    }
    std::error_code error;
    const std::filesystem::path marker = version.run_dir / "modpack.json";
    if (std::filesystem::is_regular_file(marker, error) && !error) {
        const std::string from_marker = json_string(read_json_file(marker), "minecraft");
        if (!from_marker.empty()) {
            return from_marker;
        }
    }
    for (const Library& library : version.meta.libraries) {
        const std::string lower = to_lower_ascii(library.name);
        if (lower.find(":intermediary:") != std::string::npos ||
            lower.find(":forge:") != std::string::npos ||
            lower.find(":neoforge:") != std::string::npos ||
            lower.find(":bootstraplauncher:") != std::string::npos) {
            const std::string candidate = after_last_colon(library.name);
            if (!candidate.empty() && std::isdigit(static_cast<unsigned char>(candidate[0])) != 0) {
                return candidate;
            }
        }
    }
    return version.meta.id;
}

Json instance_entry(const VersionRepository& repository, const std::string& id) {
    Json entry = Json::object();
    entry["id"] = id;
    entry["kind"] = "vanilla";
    entry["minecraftVersion"] = id;
    entry["javaMajor"] = 0;
    entry["isolated"] = false;
    entry["modCount"] = 0;
    entry["modpack"] = std::string();
    entry["ready"] = false;
    try {
        const ResolvedVersion version = repository.resolve(id);
        const VersionMeta& meta = version.meta;
        const PclInstanceSettings pcl =
            read_pcl_instance_settings(version.json_path.parent_path());
        entry["kind"] = loader_kind(meta);
        entry["mainClass"] = meta.main_class;
        entry["minecraftVersion"] = guess_minecraft_version(version, pcl);
        entry["javaMajor"] = meta.java_major.value_or(0);
        entry["type"] = meta.type;
        entry["isolated"] = version.run_dir != version.game_dir;
        entry["runDir"] = path_to_utf8(version.run_dir);
        entry["ready"] = std::filesystem::is_regular_file(meta.client_jar(version.game_dir));
        const std::filesystem::path marker = version.run_dir / "modpack.json";
        std::error_code error;
        if (std::filesystem::is_regular_file(marker, error) && !error) {
            const Json pack = read_json_file(marker);
            entry["modpack"] = json_string(pack, "name");
            const std::string pack_loader = json_string(pack, "loader");
            if (!pack_loader.empty()) {
                entry["kind"] = pack_loader;
            }
        }
        std::size_t enabled = 0;
        for (const InstalledMod& mod : scan_mods(version.run_dir)) {
            if (mod.enabled) {
                ++enabled;
            }
        }
        entry["modCount"] = static_cast<int>(enabled);
    } catch (const std::exception& error) {
        entry["error"] = error.what();
    }
    return entry;
}

std::string base64_encode(std::string_view data) {
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string encoded;
    encoded.reserve((data.size() + 2) / 3 * 4);
    std::size_t index = 0;
    while (index + 2 < data.size()) {
        const unsigned value = (static_cast<unsigned char>(data[index]) << 16) |
                               (static_cast<unsigned char>(data[index + 1]) << 8) |
                               static_cast<unsigned char>(data[index + 2]);
        encoded.push_back(alphabet[(value >> 18) & 63]);
        encoded.push_back(alphabet[(value >> 12) & 63]);
        encoded.push_back(alphabet[(value >> 6) & 63]);
        encoded.push_back(alphabet[value & 63]);
        index += 3;
    }
    if (index < data.size()) {
        unsigned value = static_cast<unsigned char>(data[index]) << 16;
        if (index + 1 < data.size()) {
            value |= static_cast<unsigned char>(data[index + 1]) << 8;
        }
        encoded.push_back(alphabet[(value >> 18) & 63]);
        encoded.push_back(alphabet[(value >> 12) & 63]);
        encoded.push_back(index + 1 < data.size() ? alphabet[(value >> 6) & 63] : '=');
        encoded.push_back('=');
    }
    return encoded;
}

std::string zip_texture(const std::filesystem::path& jar,
                        std::initializer_list<std::string_view> names) {
    for (const std::string_view name : names) {
        try {
            const std::optional<std::string> bytes = read_zip_entry(jar, name);
            if (bytes && !bytes->empty()) {
                return "data:image/png;base64," + base64_encode(*bytes);
            }
        } catch (...) {
        }
    }
    return {};
}

// Grass-block and skin textures come from the player's own client jar at
// runtime; nothing from Minecraft is redistributed with the launcher.
Json textures_payload() {
    if (!state.textures.is_null()) {
        const Json& cached = state.textures;
        if (!json_string(cached, "grassTop").empty() &&
            !json_string(cached, "grassSide").empty()) {
            return cached;
        }
    }
    Json result = Json::object();
    result["grassTop"] = std::string();
    result["grassSide"] = std::string();
    result["skin"] = std::string();
    try {
        const VersionRepository repository(state.config.game_dir);
        for (const std::string& id : repository.list_ids()) {
            try {
                const ResolvedVersion version = repository.resolve(id);
                const std::filesystem::path jar = version.meta.client_jar(version.game_dir);
                if (!std::filesystem::is_regular_file(jar)) {
                    continue;
                }
                if (json_string(result, "grassTop").empty()) {
                    result["grassTop"] = zip_texture(
                        jar, {"assets/minecraft/textures/block/grass_block_top.png",
                              "assets/minecraft/textures/blocks/grass_top.png"});
                }
                if (json_string(result, "grassSide").empty()) {
                    result["grassSide"] = zip_texture(
                        jar, {"assets/minecraft/textures/block/grass_block_side.png",
                              "assets/minecraft/textures/blocks/grass_side.png"});
                }
                if (json_string(result, "skin").empty()) {
                    result["skin"] = zip_texture(
                        jar, {"assets/minecraft/textures/entity/steve.png",
                              "assets/minecraft/textures/entity/player/wide/steve.png",
                              "assets/minecraft/textures/entity/player/steve.png"});
                }
            } catch (...) {
                continue;
            }
            if (!json_string(result, "grassTop").empty() &&
                !json_string(result, "grassSide").empty() &&
                !json_string(result, "skin").empty()) {
                break;
            }
        }
    } catch (...) {
    }
    if (!json_string(result, "grassTop").empty() && !json_string(result, "grassSide").empty()) {
        state.textures = result;
    }
    return result;
}

Json instances_payload() {
    Json result = Json::object();
    Json list = Json::array();
    const VersionRepository repository(state.config.game_dir);
    for (const std::string& id : repository.list_ids()) {
        list.push_back(instance_entry(repository, id));
    }
    result["gameDir"] = path_to_utf8(state.config.game_dir);
    result["instances"] = list;
    result["textures"] = textures_payload();
    return result;
}

Json runtimes_payload() {
    Json result = Json::object();
    Json list = Json::array();
    for (const JavaRuntime& runtime : detect_java_runtimes(state.config.game_dir)) {
        Json item = Json::object();
        item["path"] = path_to_utf8(runtime.executable);
        item["major"] = runtime.major;
        item["version"] = runtime.version;
        item["vendor"] = runtime.vendor;
        list.push_back(std::move(item));
    }
    result["runtimes"] = std::move(list);
    return result;
}

Json state_json() {
    Json payload = Json::object();
    payload["running"] = state.game.running();
    payload["pid"] = static_cast<std::int64_t>(state.game.process_id());
    payload["id"] = state.launched_id;
    payload["exitCode"] = state.game_exit_code;
    payload["stoppedByUser"] = state.stopped_by_user;
    payload["launcherLog"] = path_to_utf8(state.launcher_log.path);
    payload["gameLog"] = path_to_utf8(state.game_log.path);
    return payload;
}

Json activity_payload() {
    const ActivitySummary summary = collect_activity(state.config.game_dir);
    Json days = Json::array();
    for (const ActivityDay& day : summary.days) {
        Json entry = Json::object();
        entry["date"] = day.date;
        entry["seconds"] = day.seconds;
        entry["launches"] = day.launches;
        days.push_back(std::move(entry));
    }
    Json result = Json::object();
    result["days"] = std::move(days);
    result["totalSeconds"] = summary.total_seconds;
    result["totalLaunches"] = summary.total_launches;
    result["daysPlayed"] = summary.days_played;
    result["firstDate"] = summary.first_date;
    result["lastDate"] = summary.last_date;
    result["today"] = local_date_key(std::time(nullptr));

    // Latest session per instance, so the home shelf can order by recency.
    std::map<std::string, std::pair<std::string, long long>> latest;
    for (const PlaySession& session : read_play_sessions()) {
        auto& entry = latest[session.instance];
        if (session.start > entry.first) {
            entry.first = session.start;
        }
        entry.second += session.seconds;
    }
    std::vector<std::pair<std::string, std::pair<std::string, long long>>> ordered(
        latest.begin(), latest.end());
    std::sort(ordered.begin(), ordered.end(),
              [](const auto& left, const auto& right) {
                  return left.second.first > right.second.first;
              });
    Json recent = Json::array();
    for (std::size_t index = 0; index < ordered.size() && index < 8; ++index) {
        Json item = Json::object();
        item["id"] = ordered[index].first;
        item["lastStart"] = ordered[index].second.first;
        item["seconds"] = ordered[index].second.second;
        recent.push_back(std::move(item));
    }
    result["recent"] = std::move(recent);
    return result;
}

// Closes the current play session, writes it to activity.json and refreshes
// the heatmap. Safe to call when no session is open.
void finish_session() {
    if (!state.session_open) {
        return;
    }
    state.session_open = false;
    const int seconds =
        static_cast<int>(std::difftime(std::time(nullptr), state.session_started));
    record_play_session(state.launched_id, state.session_started, seconds);
    state.session_started = 0;
    post_event("activity", activity_payload());
}

void poll_process_state() {
    if (!state.game.valid() || state.game_exit_code != 0) {
        return;
    }
    const int exit_code = state.game.wait(0);
    if (exit_code != -1) {
        state.game_exit_code = exit_code;
        finish_session();
        post_event("state", state_json());
    }
}

std::string read_new_log_text(Tail& tail) {
    std::ifstream input(tail.path, std::ios::binary);
    if (!input) {
        return {};
    }
    input.seekg(0, std::ios::end);
    const std::uint64_t size = static_cast<std::uint64_t>(input.tellg());
    if (size < tail.offset) {
        tail.offset = 0;  // the game rotated latest.log
    }
    if (size == tail.offset) {
        return {};
    }
    const std::uint64_t span = std::min<std::uint64_t>(size - tail.offset, k_log_chunk_bytes);
    std::vector<char> buffer(static_cast<std::size_t>(span));
    input.seekg(static_cast<std::streamoff>(tail.offset), std::ios::beg);
    input.read(buffer.data(), static_cast<std::streamsize>(span));
    const std::streamsize read = input.gcount();
    if (read <= 0) {
        return {};
    }
    tail.offset += static_cast<std::uint64_t>(read);
    return std::string(buffer.data(), static_cast<std::size_t>(read));
}

// The game's console stream duplicates latest.log in log4j's XML layout, and our
// own banner lines are noise inside the UI. Keep only what the game log cannot
// tell us: JVM notes, early failures and crash stubs.
std::string keep_launcher_only_lines(const std::string& text) {
    std::string kept;
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            end = text.size();
        }
        const std::string trimmed =
            trim(std::string_view(text.data() + start, end - start));
        start = end + 1;
        const bool xml = !trimmed.empty() && trimmed.front() == '<';
        const bool banner = trimmed.rfind("====", 0) == 0;
        const bool exe_path = trimmed.size() > 7 && trimmed[1] == ':' &&
                              trimmed.compare(trimmed.size() - 4, 4, ".exe") == 0;
        if (!trimmed.empty() && !xml && !banner && !exe_path) {
            kept += trimmed;
            kept += '\n';
        }
    }
    return kept;
}

void push_logs() {
    const std::string launcher =
        keep_launcher_only_lines(read_new_log_text(state.launcher_log));
    if (!launcher.empty()) {
        Json payload = Json::object();
        payload["source"] = "launcher";
        payload["text"] = launcher;
        post_event("log", payload);
    }
    const std::string game = read_new_log_text(state.game_log);
    if (!game.empty()) {
        Json payload = Json::object();
        payload["source"] = "game";
        payload["text"] = game;
        post_event("log", payload);
    }
}

std::optional<std::filesystem::path> resolve_java(const std::string& explicit_path,
                                                  const VersionMeta& meta) {
    if (!explicit_path.empty()) {
        const auto path = absolute_path(path_from_utf8(explicit_path));
        if (std::filesystem::is_regular_file(path)) {
            return path;
        }
        return std::nullopt;
    }
    if (!state.config.java_path.empty() && std::filesystem::is_regular_file(state.config.java_path)) {
        return state.config.java_path;
    }
    if (const auto runtime =
            find_java_runtime(meta.java_major.value_or(8), state.config.game_dir)) {
        return runtime->executable;
    }
    return std::nullopt;
}

void handle_launch(const Json& message) {
    const std::string id = json_string(message, "id");
    if (id.empty()) {
        post_error("launch needs an instance id");
        return;
    }
    if (state.game.running()) {
        post_error("Minecraft is already running: " + state.launched_id);
        return;
    }
    finish_session();
    const VersionRepository repository(state.config.game_dir);
    if (!repository.contains(id)) {
        post_error("Version not found: " + id);
        return;
    }
    const ResolvedVersion version = repository.resolve(id);
    const PclInstanceSettings pcl =
        read_pcl_instance_settings(version.json_path.parent_path());

    LaunchOptions options;
    options.username = json_string(message, "username", state.config.username);
    if (options.username.empty()) {
        options.username = "Player";
    }
    const std::int64_t memory = json_integer(message, "memoryMb", state.config.memory_mb);
    options.memory_mb = memory > 0 ? static_cast<int>(memory) : 2048;
    options.width = state.config.width;
    options.height = state.config.height;
    options.extra_jvm_args =
        pcl.detected && !pcl.jvm_args.empty() ? pcl.jvm_args : state.config.jvm_args;
    options.extra_game_args =
        pcl.detected && !pcl.game_args.empty() ? pcl.game_args : state.config.game_args;

    const auto java = resolve_java(json_string(message, "javaPath"), version.meta);
    if (!java) {
        post_error("No usable Java runtime for " + id + " (needs >= " +
                   std::to_string(version.meta.java_major.value_or(8)) +
                   "). Pick one in the settings panel.");
        return;
    }
    options.java_path = *java;
    // Offline only: the Microsoft session stays with the CLI so a token never
    // reaches the web view.
    options.user_type = "legacy";

    const LaunchPlan plan = build_launch_plan(version, detect_environment(), options);

    state.launcher_log = Tail{version.run_dir / "logs" / "aurora-launcher.log", 0};
    state.game_log = Tail{version.run_dir / "logs" / "latest.log",
                          file_size_or_zero(version.run_dir / "logs" / "latest.log")};
    state.launched_id = id;
    state.game_exit_code = 0;
    state.stopped_by_user = false;
    state.game =
        spawn_process(plan.java, plan.arguments, plan.working_directory, state.launcher_log.path);
    state.session_started = std::time(nullptr);
    state.session_open = true;
    post_event("launched", state_json());
}

void handle_stop() {
    if (!state.game.running()) {
        post_error("Minecraft is not running");
        return;
    }
    state.stopped_by_user = true;
    if (!state.game.terminate()) {
        post_error("Unable to stop Minecraft (Windows error " +
                   std::to_string(GetLastError()) + ")");
        return;
    }
    const int exit_code = state.game.wait(5000);
    state.game_exit_code = exit_code < 0 ? 1 : exit_code;
    finish_session();
    post_event("state", state_json());
}

void handle_save_settings(const Json& message) {
    if (const auto value = json_optional_string(message, "gameDir"); value && !value->empty()) {
        state.config.game_dir = absolute_path(path_from_utf8(*value));
    }
    if (const auto value = json_optional_string(message, "username")) {
        state.config.username = trim(*value);
    }
    if (const auto value = json_optional_string(message, "javaPath")) {
        state.config.java_path = value->empty()
                                     ? std::filesystem::path{}
                                     : absolute_path(path_from_utf8(*value));
    }
    const std::int64_t memory = json_integer(message, "memoryMb", state.config.memory_mb);
    if (memory > 0) {
        state.config.memory_mb = static_cast<int>(memory);
    }
    try {
        state.config.save(state.config_path);
    } catch (const std::exception& error) {
        post_error(std::string("Unable to save config: ") + error.what());
        return;
    }
    post_event("saved", settings_payload());
}

void handle_message(const std::string& json_text) {
    Json message;
    try {
        message = Json::parse(json_text);
    } catch (const std::exception& error) {
        post_error(std::string("Bad message: ") + error.what());
        return;
    }
    const std::string action = json_string(message, "action");
    const std::int64_t reply_to = json_integer(message, "id", 0);
    try {
        if (action == "boot") {
            Json payload = Json::object();
            payload["instances"] = instances_payload();
            payload["runtimes"] = runtimes_payload();
            payload["settings"] = settings_payload();
            payload["state"] = state_json();
            payload["activity"] = activity_payload();
            payload["version"] = std::string(AURORA_GUI_VERSION);
            if (reply_to != 0) {
                payload["replyTo"] = reply_to;
            }
            post_event("boot", payload);
        } else if (action == "instances") {
            post_event("instances", instances_payload());
        } else if (action == "runtimes") {
            post_event("runtimes", runtimes_payload());
        } else if (action == "settings") {
            post_event("settings", settings_payload());
        } else if (action == "activity") {
            post_event("activity", activity_payload());
        } else if (action == "window") {
            const std::string command = json_string(message, "command");
            if (command == "drag") {
                ReleaseCapture();
                SendMessageW(state.window, WM_NCLBUTTONDOWN, HTCAPTION, 0);
            } else if (command == "minimize") {
                ShowWindow(state.window, SW_MINIMIZE);
            } else if (command == "maximize") {
                toggle_maximize(state.window);
            } else if (command == "close") {
                finish_session();
                DestroyWindow(state.window);
            } else {
                post_error("Unknown window command: " + command);
            }
        } else if (action == "launch") {
            handle_launch(message);
        } else if (action == "stop") {
            handle_stop();
        } else if (action == "saveSettings") {
            handle_save_settings(message);
        } else {
            post_error("Unknown action: " + action);
        }
    } catch (const std::exception& error) {
        post_error(error.what());
    }
}

void fail_startup(const std::wstring& text) {
    MessageBoxW(nullptr, text.c_str(), L"AuroraLauncher", MB_OK | MB_ICONERROR);
    PostQuitMessage(1);
}

LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_SIZE:
        // Minimized windows report a collapsed client rect; resizing the web
        // view to that keeps Chromium from reflowing at a bogus viewport.
        if (wparam != SIZE_MINIMIZED) {
            update_webview_bounds();
        }
        if (wparam == SIZE_MAXIMIZED || wparam == SIZE_RESTORED) {
            Json payload = Json::object();
            payload["maximized"] = IsZoomed(hwnd) != 0;
            post_event("window", payload);
        }
        return 0;
    case WM_ERASEBKGND: {
        // Only the 4px rim around the web view is ever visible; paint it in the
        // title-bar blue on top so the header still reads edge-to-edge.
        HDC dc = reinterpret_cast<HDC>(wparam);
        RECT client{};
        GetClientRect(hwnd, &client);
        FillRect(dc, &client, state.rim_brush);
        RECT bar = client;
        bar.bottom = std::min<LONG>(bar.bottom, MulDiv(48, static_cast<int>(GetDpiForWindow(hwnd)), 96) + k_resize_border);
        FillRect(dc, &bar, state.header_brush);
        return 1;
    }
    case WM_NCCALCSIZE:
        // Drop the native caption and frame; the web view draws its own title bar.
        if (wparam != 0) {
            return 0;
        }
        break;
    case WM_NCHITTEST: {
        POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        ScreenToClient(hwnd, &point);
        RECT rect{};
        GetClientRect(hwnd, &rect);
        const int border = k_resize_border + 2;
        const bool left = point.x < border;
        const bool right = point.x >= rect.right - border;
        const bool top = point.y < border;
        const bool bottom = point.y >= rect.bottom - border;
        if (top && left) {
            return HTTOPLEFT;
        }
        if (top && right) {
            return HTTOPRIGHT;
        }
        if (bottom && left) {
            return HTBOTTOMLEFT;
        }
        if (bottom && right) {
            return HTBOTTOMRIGHT;
        }
        if (left) {
            return HTLEFT;
        }
        if (right) {
            return HTRIGHT;
        }
        if (top) {
            return HTTOP;
        }
        if (bottom) {
            return HTBOTTOM;
        }
        return HTCLIENT;
    }
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
        info->ptMinTrackSize.x = 980;
        info->ptMinTrackSize.y = 620;
        MONITORINFO monitor{};
        monitor.cbSize = sizeof(monitor);
        if (GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor)) {
            // Keep a frameless maximize inside the work area (taskbar visible).
            info->ptMaxPosition.x = monitor.rcWork.left - monitor.rcMonitor.left;
            info->ptMaxPosition.y = monitor.rcWork.top - monitor.rcMonitor.top;
            info->ptMaxSize.x = monitor.rcWork.right - monitor.rcWork.left;
            info->ptMaxSize.y = monitor.rcWork.bottom - monitor.rcWork.top;
        }
        return 0;
    }
    case WM_SETFOCUS:
        if (state.controller != nullptr) {
            state.controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
        }
        return 0;
    case WM_TIMER:
        if (wparam == k_poll_timer) {
            poll_process_state();
            push_logs();
            return 0;
        }
        break;
    case WM_DESTROY:
        finish_session();
        KillTimer(hwnd, k_poll_timer);
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

using EnvironmentHandler = RuntimeClass<RuntimeClassFlags<ClassicCom>,
                                        ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>;
using ControllerHandler = RuntimeClass<RuntimeClassFlags<ClassicCom>,
                                       ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>;
using MessageHandler = RuntimeClass<RuntimeClassFlags<ClassicCom>,
                                    ICoreWebView2WebMessageReceivedEventHandler>;
using CdpHandler = RuntimeClass<RuntimeClassFlags<ClassicCom>,
                                ICoreWebView2CallDevToolsProtocolMethodCompletedHandler>;

} // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int show_command) {
    // Per-monitor DPI v2 keeps the window sharp; older builds fall back to system
    // awareness. The API is resolved dynamically so the loader never hard-fails.
    if (const auto set_context = reinterpret_cast<BOOL(WINAPI*)(DWORD)>(
            GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"))) {
        set_context(k_per_monitor_dpi_aware_v2);
    } else {
        SetProcessDPIAware();
    }

    // Small escape hatch for testing and packaged launches:
    // aurora_gui [--config FILE] [--game-dir DIR]
    std::wstring command_line(GetCommandLineW());
    const auto read_option = [&command_line](std::wstring_view name, std::wstring& out) {
        const std::size_t found = command_line.find(name);
        if (found == std::wstring::npos) {
            return false;
        }
        std::size_t start = found + name.size();
        while (start < command_line.size() && command_line[start] == L' ') {
            ++start;
        }
        if (start < command_line.size() && command_line[start] == L'"') {
            const std::size_t end = command_line.find(L'"', start + 1);
            out = command_line.substr(start + 1, end == std::wstring::npos
                                                       ? std::wstring::npos
                                                       : end - start - 1);
        } else {
            const std::size_t end = command_line.find(L' ', start);
            out = command_line.substr(start, end == std::wstring::npos
                                                 ? std::wstring::npos
                                                 : end - start);
        }
        return !out.empty();
    };

    std::wstring config_argument;
    if (read_option(L"--config", config_argument)) {
        state.config_path = std::filesystem::path(config_argument);
    } else {
        state.config_path = app_data_dir() / "config.json";
    }
    state.config = Config::load(state.config_path);
    std::wstring game_dir_argument;
    if (read_option(L"--game-dir", game_dir_argument)) {
        state.config.game_dir = absolute_path(std::filesystem::path(game_dir_argument));
    }

    WNDCLASSW window_class{};
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground = CreateSolidBrush(RGB(11, 14, 19));
    window_class.lpszClassName = L"AuroraLauncherGuiWindow";
    if (RegisterClassW(&window_class) == 0) {
        return 1;
    }

    state.rim_brush = CreateSolidBrush(RGB(11, 14, 19));
    state.header_brush = CreateSolidBrush(RGB(42, 128, 227));

    state.window = CreateWindowExW(
        WS_EX_APPWINDOW, window_class.lpszClassName, L"AuroraLauncher",
        WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 1180, 760, nullptr, nullptr, instance, nullptr);
    if (state.window == nullptr) {
        return 1;
    }
    center_in_work_area(state.window);
    // Windows 11 can round the frameless window; older systems simply keep
    // square corners. Attribute 33 is DWMWA_WINDOW_CORNER_PREFERENCE.
    if (const auto dwm = LoadLibraryExW(L"dwmapi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)) {
        using DwmSetWindowAttributeFn = HRESULT(WINAPI *)(HWND, DWORD, LPCVOID, DWORD);
        if (const auto set_attribute = reinterpret_cast<DwmSetWindowAttributeFn>(
                GetProcAddress(dwm, "DwmSetWindowAttribute"))) {
            const int round = 2;  // DWMWCP_ROUND
            set_attribute(state.window, 33, &round, sizeof(round));
        }
        FreeLibrary(dwm);
    }

    ShowWindow(state.window, show_command);
    UpdateWindow(state.window);
    SetTimer(state.window, k_poll_timer, 500, nullptr);

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    const std::filesystem::path user_data = app_data_dir() / "WebView2";
    const HRESULT created = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, user_data.wstring().c_str(), nullptr,
        Callback<EnvironmentHandler>(
            [](HRESULT environment_result, ICoreWebView2Environment* environment) -> HRESULT {
                if (FAILED(environment_result)) {
                    fail_startup(
                        L"AuroraLauncher needs the Microsoft Edge WebView2 Runtime. It is "
                        L"preinstalled on Windows 11 and optional on older Windows 10.\n\n"
                        L"Install it from developer.microsoft.com/microsoft-edge/webview2 "
                        L"and start AuroraLauncher again.");
                    return environment_result;
                }
                return environment->CreateCoreWebView2Controller(
                    state.window,
                    Callback<ControllerHandler>(
                        [](HRESULT controller_result,
                           ICoreWebView2Controller* controller) -> HRESULT {
                            if (FAILED(controller_result)) {
                                fail_startup(L"Unable to create the embedded browser view.");
                                return controller_result;
                            }
                            state.controller = controller;
                            state.controller->get_CoreWebView2(&state.webview);
                            update_webview_bounds();
                            state.controller->MoveFocus(
                                COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);

                            COREWEBVIEW2_COLOR background{};
                            background.A = 255;
                            background.R = 11;
                            background.G = 14;
                            background.B = 19;
                            // Background lives on ICoreWebView2Controller2, not on the view.
                            ComPtr<ICoreWebView2Controller2> controller2;
                            if (SUCCEEDED(state.controller.As(&controller2))) {
                                controller2->put_DefaultBackgroundColor(background);
                            }
                            ComPtr<ICoreWebView2Settings> settings;
                            state.webview->get_Settings(&settings);
                            settings->put_IsStatusBarEnabled(FALSE);
                            settings->put_AreDevToolsEnabled(
                                getenv("AURORA_GUI_DEVTOOLS") != nullptr);
                            state.webview.As(&state.webview3);

                            state.webview->add_WebMessageReceived(
                                Callback<MessageHandler>(
                                    [](ICoreWebView2*,
                                       ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                                        LPWSTR json = nullptr;
                                        if (FAILED(args->get_WebMessageAsJson(&json)) ||
                                            json == nullptr) {
                                            return S_OK;
                                        }
                                        handle_message(to_utf8(json));
                                        CoTaskMemFree(json);
                                        return S_OK;
                                    })
                                    .Get(),
                                nullptr);

                            if (state.webview3 != nullptr) {
                                state.webview3->SetVirtualHostNameToFolderMapping(
                                    L"aurora.ui", ui_directory().wstring().c_str(),
                                    COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
                            }
                            // The virtual host caches index.html; disable the
                            // HTTP cache so UI edits show up on the next launch.
                            state.webview->CallDevToolsProtocolMethod(
                                L"Network.setCacheDisabled", L"{\"cacheDisabled\":true}",
                                Callback<CdpHandler>(
                                    [](HRESULT, LPCWSTR) -> HRESULT { return S_OK; })
                                    .Get());
                            state.webview->Navigate(L"https://aurora.ui/index.html");
                            return S_OK;
                        })
                        .Get());
            })
            .Get());
    if (FAILED(created)) {
        wchar_t code[16]{};
        std::swprintf(code, 16, L"0x%08X", static_cast<unsigned>(created));
        fail_startup(L"Unable to start the embedded browser (HRESULT " + std::wstring(code) +
                     L").");
        CoUninitialize();
        return 1;
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    DeleteObject(state.rim_brush);
    DeleteObject(state.header_brush);
    CoUninitialize();
    return static_cast<int>(message.wParam);
}
