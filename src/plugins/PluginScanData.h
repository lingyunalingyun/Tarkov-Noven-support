#pragma once
#include "data/RecentScanStore.h"
#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

namespace noven::plugins {
inline constexpr std::size_t MaximumScanEvents=32;
std::string ScanRecord(const data::RecentScanEntry& entry);
bool ValidScanId(std::string_view id);
bool ValidScanRecord(std::string_view json);
// 生产者不等待消费/IPC/回调；锁竞争丢弃新事件，容量满则丢弃最旧待发事件。
// Producers never wait for consumers/IPC/callbacks; contention drops newest, capacity drops oldest pending.
class ScanEventQueue final {
public:
    void Subscribe(bool enabled);
    bool Push(std::shared_ptr<const std::string> record);
    std::shared_ptr<const std::string> Pop();
    bool Subscribed() const;
    std::size_t Pending() const;
    std::uint32_t Dropped() const{return dropped_.load();}
private:
    void Drop();
    mutable std::mutex mutex_;
    bool subscribed_{};
    std::deque<std::shared_ptr<const std::string>> pending_;
    std::atomic<std::uint32_t> dropped_{};
};
}
