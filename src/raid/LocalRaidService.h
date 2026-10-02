#pragma once
#include "raid/RaidSessionStore.h"
#include <memory>
#include <mutex>
#include <thread>
#include <functional>

namespace noven::raid {
struct RaidServiceStatus final {
    bool running{};
    std::string error;
    std::uint64_t bytesRead{}, ignoredLines{}, events{}, directoryPasses{};
};
// 服务线程独占读取器/解析状态/写入器；调用方只取得加锁后的值快照。
// Worker exclusively owns readers/parser state/writer; callers receive locked value snapshots.
class LocalRaidService final {
public:
    LocalRaidService();
    ~LocalRaidService();
    bool Start(const std::filesystem::path& root, const std::filesystem::path& history);
    void Stop();
    std::optional<RaidSession> ActiveSession() const;
    std::optional<RaidSession> FindSession(std::string_view localId) const;
    std::vector<RaidSession> CompletedSessions() const;
    RaidServiceStatus Status() const;
    // 启动前设置通知；只通知语义变化，不因无关日志追加驱动 UI 刷新。
    // Set before Start; notify semantic changes only, never UI refreshes for unrelated appends.
    void SetChangedCallback(std::function<void()> callback) { changed_=std::move(callback); }
private:
    struct Pipeline;
    std::unique_ptr<Pipeline> pipeline_;
    HANDLE stopEvent_{};
    std::jthread worker_;
    mutable std::mutex mutex_;
    DetectorSnapshot published_;
    RaidServiceStatus status_;
    std::function<void()> changed_;
    void Run(std::stop_token stop, std::filesystem::path root, std::filesystem::path history);
};
// 无配置即不读取；只接受明确 UTF-8 路径，不探测磁盘/注册表/进程。
// Without configuration, read nothing; accept an explicit UTF-8 path, never probe disks/registry/processes.
std::optional<std::filesystem::path> ReadEftLogRoot(const std::filesystem::path& config);
}
