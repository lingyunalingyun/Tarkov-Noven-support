#pragma once
#include "plugins/PluginDiscovery.h"
#include <chrono>
#include <memory>

namespace noven::plugins {
enum class HostState { Stopped,Starting,Connecting,Handshaking,Ready,Stopping,Exited,Crashed,ProtocolError };
enum class HostError { None,Startup,ConnectionTimeout,HandshakeTimeout,PeerMismatch,HandshakeMismatch,InvalidProtocol,Disconnected,PingTimeout,ShutdownTimeout,FrameTimeout };
struct HostSnapshot final {
    std::string pluginId;
    HostState state{HostState::Stopped};
    HostError error{HostError::None};
    DWORD processId{};
    std::chrono::steady_clock::time_point startedAt{};
    std::uint64_t pongs{};
    bool jobOwned{},shutdownAcknowledged{};
};
bool Terminal(HostState state);
// 第一方测试入口，不由发现或页面调用；独立进程是崩溃边界，不是 OS 安全沙箱。
// First-party test entry, never called by discovery/pages; process isolation is a crash boundary, not an OS sandbox.
class PluginRuntimeManager final {
public:
    explicit PluginRuntimeManager(std::filesystem::path executableDirectory);
    ~PluginRuntimeManager();
    PluginRuntimeManager(const PluginRuntimeManager&)=delete;
    PluginRuntimeManager& operator=(const PluginRuntimeManager&)=delete;
    bool Start(const PluginRecord& validatedRecord);
    bool Ping(std::string_view pluginId);
    bool Stop(std::string_view pluginId);
    std::optional<HostSnapshot> Snapshot(std::string_view pluginId) const;
    std::size_t SessionCount() const;
    const std::filesystem::path& HostPath() const;
    // 仅测试可以阻塞等待快照；UI 应使用非阻塞 Snapshot，不接收原生句柄或内存地址。
    // Only tests block awaiting snapshots; UI uses nonblocking Snapshot, without native handles/memory addresses.
    bool WaitFor(std::string_view pluginId,HostState state,DWORD milliseconds) const;
    bool WaitForTerminal(std::string_view pluginId,DWORD milliseconds) const;
    bool WaitForPong(std::string_view pluginId,std::uint64_t count,DWORD milliseconds) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
