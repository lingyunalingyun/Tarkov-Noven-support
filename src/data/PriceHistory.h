#pragma once
#include "data/GameMode.h"
#include <windows.h>
#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace noven::data {
inline constexpr std::int64_t kHistoryDayMs = 86400000;
// “一个月”统一定义为滚动 30 天；落盘入口始终再次裁剪。
// One month means a rolling 30 days; every disk write enforces retention again.
inline constexpr int kHistoryRetentionDays = 30;
struct HistoryPoint { std::int64_t timeMs{}; std::int64_t price{}; };
struct HistorySnapshot {
    std::string itemId;
    GameMode mode{GameMode::Pvp};
    int days{30};
    std::int64_t asOfMs{};
    std::vector<HistoryPoint> points;
    bool loading{true};
    bool failed{};
    bool cached{};
};
bool ValidHistoryId(const std::string& id) noexcept;
bool ParseHistory(std::string_view json, std::vector<HistoryPoint>& points);
std::vector<HistoryPoint> HistoryRange(const std::vector<HistoryPoint>& points,
    std::int64_t now, int days);
bool SaveHistory(const std::filesystem::path& file, const std::vector<HistoryPoint>& points,
    std::int64_t now);
bool LoadHistory(const std::filesystem::path& file, std::vector<HistoryPoint>& points);
inline std::optional<std::pair<std::int64_t, std::int64_t>> HistoryExtrema(
    const std::vector<HistoryPoint>& points) {
    if (points.empty()) return std::nullopt;
    const auto [low, high] = std::minmax_element(points.begin(), points.end(),
        [](const auto& a, const auto& b) { return a.price < b.price; });
    return std::pair{low->price, high->price};
}

// 窗口拥有一个工作线程，网络/JSON/磁盘全在后台；仅传递数据快照给 UI。
// The window owns one worker; network/JSON/disk stay off UI and only snapshots cross threads.
// 队列只保留最新请求，旧请求不能覆盖新卡片；较早价格只存在当前内存快照中。
// Only the latest request is queued; stale replies cannot replace newer cards.
// Older prices exist only in the current in-memory snapshot.
class PriceHistoryService final {
public:
    using Downloader = std::function<bool(const std::string&, GameMode, std::string&)>;
    // 默认使用官方网络接口；测试可注入离线响应而不访问真实缓存或网络。
    // Production uses the official endpoint; tests may inject offline responses without real cache/network access.
    explicit PriceHistoryService(Downloader downloader = {}) : downloader_(std::move(downloader)) {}
    static constexpr UINT kReadyMessage = WM_APP + 4;
    ~PriceHistoryService() { Stop(); }
    void Start(HWND window, std::filesystem::path directory);
    void Stop();
    void Request(std::string id, GameMode mode, int days);
    std::optional<HistorySnapshot> TakeReady();
private:
    Downloader downloader_;
    struct RequestInfo { std::string id; GameMode mode; int days; std::uint64_t generation; };
    void Run();
    void Publish(const RequestInfo& request, HistorySnapshot snapshot);
    HWND window_{};
    std::filesystem::path directory_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::optional<RequestInfo> pending_;
    std::optional<HistorySnapshot> ready_;
    std::uint64_t generation_{};
    bool stopping_{};
    std::thread worker_;
};
}
