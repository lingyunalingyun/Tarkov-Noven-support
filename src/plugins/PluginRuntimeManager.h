#pragma once
#include "plugins/PluginDiscovery.h"
#include "plugins/PluginStateStore.h"
#include "plugins/PluginUiDocument.h"
#include "plugins/CatalogPluginService.h"
#include <chrono>
#include <functional>
#include <memory>

namespace noven::plugins {
enum class HostState { Stopped,Starting,Connecting,Handshaking,Ready,Stopping,Exited,Crashed,ProtocolError,Loading,Running };
enum class HostError { None,Startup,ConnectionTimeout,HandshakeTimeout,PeerMismatch,HandshakeMismatch,InvalidProtocol,Disconnected,PingTimeout,ShutdownTimeout,FrameTimeout,LoadFailed,LoadTimeout,ActionTimeout,DataTimeout };
struct RuntimePage final {std::string localId,title;UiDocument document;};
struct HostSnapshot final {
    std::string pluginId;
    HostState state{HostState::Stopped};
    HostError error{HostError::None};
    DWORD processId{};
    std::chrono::steady_clock::time_point startedAt{};
    std::uint64_t pongs{};
    bool jobOwned{},shutdownAcknowledged{};
    std::uint64_t generation{};
    std::int64_t loadResult{};
    std::vector<RuntimePage> pages;
    std::string lastLog;
    std::uint64_t dataResults{};
    std::size_t pendingData{};
};
bool Terminal(HostState state);
// 会话所有权留在 Noven；发现不启动会话，独立进程不是 OS 安全沙箱。
// Noven owns sessions; discovery never starts them, and process isolation is not an OS sandbox.
class PluginRuntimeManager final {
public:
    PluginRuntimeManager();
    // 目录重载仅供第一方测试夹具；生产默认构造总从当前可执行文件定位 Host。
    // Directory overload is for first-party fixtures; production defaults always resolve Host from the current executable.
    explicit PluginRuntimeManager(std::filesystem::path executableDirectory);
    ~PluginRuntimeManager();
    PluginRuntimeManager(const PluginRuntimeManager&)=delete;
    PluginRuntimeManager& operator=(const PluginRuntimeManager&)=delete;
    bool Start(const PluginRecord& validatedRecord); // 第一方传输测试，不加载 DLL。 First-party transport test, no DLL load.
    // 仅 Noven 显式同意/已保存授权可进入此入口；发现和刷新不调用它。
    // Only explicit Noven consent/persisted grants enter here; discovery/refresh never call it.
    bool StartNative(const PluginRecord& validatedRecord,const PluginStateStore& grants);
    bool Action(std::string_view pluginId,std::uint64_t generation,std::string_view localPageId,std::string_view actionId);
    // 在启动前设置无阻塞通知（例如 PostMessage）；不传原生句柄给插件。
    // Set a nonblocking notification before starting (e.g. PostMessage); no native handles go to plugins.
    void SetChangeHandler(std::function<void()> handler);
    void SetCatalogService(std::shared_ptr<CatalogPluginService> service);
    void SetCatalogLocale(std::string_view locale);
    void PublishRaidHistory(std::span<const raid::RaidSession> completed);
    void PublishEvents(std::span<const events::EventRecord> events);
    std::vector<HostSnapshot> Snapshots() const;
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
