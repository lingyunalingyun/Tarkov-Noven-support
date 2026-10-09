#pragma once
#include "plugins/PluginRegistry.h"
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
namespace noven::plugins {
enum class MarketplaceState {Unconfigured,Empty,Loading,Live,Cached,Error};
enum class MarketplaceError {None,Fetch,InvalidRegistry,CacheWrite};
struct MarketplaceSnapshot final {
    MarketplaceState state{MarketplaceState::Unconfigured};
    MarketplaceError error{MarketplaceError::None};
    std::shared_ptr<const PluginRegistry> registry;
    std::int64_t fetchedAt{};
    bool reviewFixture{};
};
// 第一方传输只取一个预先配置的 Registry；没有包下载、插件路径或运行控制接口。
// First-party transport fetches one preconfigured Registry; no packages, plugin paths or runtime controls.
struct IRegistryTransport {virtual ~IRegistryTransport()=default;virtual std::string Fetch(std::stop_token stop)=0;};
class MarketplaceService final {
public:
    using Clock=std::function<std::int64_t()>;
    MarketplaceService(std::filesystem::path cache,std::string source,std::unique_ptr<IRegistryTransport> transport,
        std::function<void()> changed={},bool reviewFixture=false,Clock clock={});
    ~MarketplaceService();
    std::shared_ptr<const MarketplaceSnapshot> Snapshot() const;
    bool Refresh(bool explicitRequest=true);
private:
    void Run(std::stop_token stop);
    void Notify() const;
    std::filesystem::path cache_;
    std::string source_;
    std::unique_ptr<IRegistryTransport> transport_;
    std::function<void()> changed_;
    Clock clock_;
    mutable std::mutex mutex_;
    std::condition_variable_any wake_;
    std::shared_ptr<const MarketplaceSnapshot> snapshot_;
    std::int64_t attemptedAt_{};
    bool attempted_{},requested_{};
    std::jthread worker_;
};
bool ValidRegistryEndpoint(std::string_view url);
std::unique_ptr<IRegistryTransport> MakeRegistryTransport(std::string url);
}
