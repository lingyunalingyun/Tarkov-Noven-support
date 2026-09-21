#pragma once

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
    std::mutex queue_mutex_;
    std::condition_variable queue_available_;
    std::deque<GameMode> queue_;
    bool stopping_{};
    bool started_{};
    std::thread worker_thread_;
};

} // namespace noven::data
