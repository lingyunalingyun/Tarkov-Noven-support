#pragma once

#include "data/GameMode.h"
#include "data/ItemEconomyTypes.h"

#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace noven::data {

enum class RecentMatchMode { Strict, BestEffort };

// 名称与价格都是扫描时的快照；稳定物品 ID 才是身份键。
// Names and prices are scan-time snapshots; the stable item ID is identity.
struct RecentScanEntry final {
    std::uint64_t scanId{};
    std::string stableItemId;
    std::string canonicalName;
    std::string canonicalShortName;
    GameMode gameMode{GameMode::Pvp};
    RecentMatchMode matchMode{RecentMatchMode::Strict};
    bool ambiguous{};
    std::int64_t scannedAtUnixMs{};
    std::optional<std::int64_t> fleaPrice;
    std::optional<std::int64_t> bestTraderPrice;
    std::string bestTraderName;
    std::optional<double> valuePerSlot;
    FleaStatus fleaStatus{FleaStatus::Unknown};
    int itemWidth{};
    int itemHeight{};
};

class RecentScanStore final {
public:
    // 所有模式共享总量上限；标签只过滤视图，不建立独立存储。
    // All modes share this total limit; tabs filter views rather than creating separate stores.
    static constexpr std::size_t kMaxEntries = 200;

    RecentScanStore() = default;
    ~RecentScanStore();
    RecentScanStore(const RecentScanStore&) = delete;
    RecentScanStore& operator=(const RecentScanStore&) = delete;

    // 缺失文件是正常的空历史；损坏文件保持原样，直到追加新记录后再写入。
    // A missing file is empty history; malformed data remains untouched until a new append.
    bool Load(const std::filesystem::path& path, std::wstring& error);
    // 单次启动加载后追加；扫描 ID 必须递增，快照复制后才交给 UI 绘制。
    // Append after the one-time startup load; IDs must increase and UI rendering uses snapshot copies.
    bool Append(RecentScanEntry entry);
    [[nodiscard]] std::vector<RecentScanEntry> Snapshot() const;
    [[nodiscard]] std::uint64_t MaxScanId() const;

private:
    void PersistLoop();

    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::filesystem::path path_;
    std::vector<RecentScanEntry> entries_;
    std::thread writer_;
    std::uint64_t revision_{};
    bool stopping_{};
};

} // namespace noven::data
