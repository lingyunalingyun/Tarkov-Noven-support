#pragma once

// 网络刷新由独立线程处理；F2 扫描不等待下载，已有磁盘缓存可离线使用。
// A separate worker handles network refresh; F2 never waits for downloads and can use disk cache offline.

#include "data/GameMode.h"

#include <condition_variable>
#include <deque>
#include <filesystem>
#include <mutex>
#include <thread>

namespace noven::data {

class ItemEconomyStore;

class DataRefreshService final {
public:
    explicit DataRefreshService(ItemEconomyStore& store);
    ~DataRefreshService();

    DataRefreshService(const DataRefreshService&) = delete;
    DataRefreshService& operator=(const DataRefreshService&) = delete;

    void Start(std::filesystem::path cache_directory);
    void RequestRefresh(GameMode mode);
    void Stop();

private:
    void WorkerLoop();
    void RefreshOne(GameMode mode);
    bool DownloadItems(GameMode mode, std::string& payload, std::wstring& error) const;

    ItemEconomyStore& store_;
    std::filesystem::path cache_directory_;
    // 队列与停止标志由此互斥量保护；析构前先唤醒并结束工作线程。
    // This mutex protects the queue and stop flag; wake and join the worker before destruction.
    std::mutex queue_mutex_;
    std::condition_variable queue_available_;
    std::deque<GameMode> queue_;
    bool stopping_{};
    bool started_{};
    std::thread worker_thread_;
};

} // namespace noven::data
