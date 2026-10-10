#pragma once
#include "resources/ResourceManifest.h"
#include "common/AppPaths.h"
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stop_token>
#include <thread>
namespace noven::resources {
enum class ResourceState {Unavailable,NotInstalled,Queued,Downloading,Paused,Verifying,Installing,Installed,UpdateAvailable,Deleting,Error};
enum class RemoteAvailability {ProductionEndpointUnconfigured,OfflineFixture};
enum class ResourceAction {Download,Pause,Resume,Cancel,Verify,Repair,Delete};
bool LegalResourceTransition(ResourceState from,ResourceState to);
struct ResourceSnapshot final {ResourceRecord record;ResourceState state{ResourceState::Unavailable};std::uint64_t received{};std::string error;bool installed{},present{};};
struct ResourceMapLabel final {std::string stableMapId,titleZh,titleEn;};
// 传输必须有界并响应取消；只有第一方服务能注入，插件没有入口。生产默认没有传输。
// Transports must be bounded/cancellable, injected only by first-party code; production defaults to no transport.
class ResourceStream {
public:
    virtual ~ResourceStream()=default;
    virtual std::uint64_t Offset() const=0;
    virtual std::uint64_t TotalSize() const=0;
    virtual std::string Identity() const=0;
    virtual std::size_t Read(std::span<char> buffer,std::stop_token stop)=0;
};
class ResourceTransport {
public:
    virtual ~ResourceTransport()=default;
    virtual std::unique_ptr<ResourceStream> Open(const ResourceRecord& record,std::uint64_t offset,std::stop_token stop)=0;
};
class ResourceService final {
public:
    using MapLease=std::shared_ptr<const std::filesystem::path>;
    ResourceService(common::AppPaths paths,ResourceManifest manifest,std::shared_ptr<ResourceTransport> offlineTransport={},
        std::function<std::uint64_t()> diskSpace={},std::function<void()> changed={},std::vector<ResourceMapLabel> unavailableMaps={});
    ~ResourceService();
    ResourceService(const ResourceService&)=delete;
    RemoteAvailability Remote() const {return transport_?RemoteAvailability::OfflineFixture:RemoteAvailability::ProductionEndpointUnconfigured;}
    bool CanDownload() const;
    std::vector<ResourceSnapshot> Snapshot() const;
    bool Act(std::string_view id,ResourceAction action);
    void DownloadAll();
    void CheckResources();
    bool ClearDownloadCache();
    MapLease ResolveMap(std::string_view stableMapId) const;
    bool WaitIdle(std::chrono::milliseconds timeout);
    void Shutdown();
private:
    enum class Work {Download,Verify,Repair,Delete,ClearCache};
    struct Entry {ResourceSnapshot view;std::optional<ResourceRecord> installed;MapLease location;bool busy{},cancel{};std::uint64_t reserved{};std::stop_source stop;};
    struct Job {std::string id;Work work;};
    void Worker(std::stop_token stop);
    void Execute(const Job& job,std::stop_token stop);
    void Notify() const;
    void Set(const std::string& id,ResourceState state,std::string error={});
    std::filesystem::path Partial(const ResourceRecord& r) const;
    std::filesystem::path Pointer(const ResourceRecord& r) const;
    std::uint64_t LoadPartial(const ResourceRecord& r) const;
    void DiscardPartial(const ResourceRecord& r) const;
    common::AppPaths paths_;std::shared_ptr<ResourceTransport> transport_;
    std::function<std::uint64_t()> diskSpace_;std::function<void()> changed_;
    mutable std::mutex mutex_;std::condition_variable condition_,idle_;
    std::map<std::string,Entry> entries_;std::deque<Job> jobs_;std::vector<std::jthread> workers_;
    bool stopping_{},clearing_{};unsigned active_{};std::uint64_t reservedBytes_{};
};
std::vector<ResourceAction> ResourceActions(const ResourceSnapshot& snapshot,RemoteAvailability remote);
}
