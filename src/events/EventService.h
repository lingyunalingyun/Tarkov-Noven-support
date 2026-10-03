#pragma once
#include "events/IEventSource.h"
#include <functional>
#include <mutex>
#include <thread>

namespace noven::events {
enum class RefreshPhase { Idle, Refreshing, Ready, Failed };
struct EventRefreshState {RefreshPhase phase{RefreshPhase::Idle};std::string error,enrichmentWarning;};
using EventEnrichment=std::function<bool(EventCatalog&,std::stop_token,std::string&)>;
// 生命周期调用属于应用线程；查询可跨线程，快照不暴露 worker 的可变目录。
// Lifecycle calls belong to the app thread; cross-thread queries return snapshots, never mutable worker state.
class EventService final {
public:
    explicit EventService(IEventSource& source,EventEnrichment enrich={}):source_(source),enrich_(std::move(enrich)){}
    ~EventService(){Stop();}
    bool Start(const std::filesystem::path& cacheFile);
    bool RequestRefresh();
    void Stop();
    void SetChangedCallback(std::function<void()> callback){changed_=std::move(callback);}
    [[nodiscard]] std::vector<EventRecord> Events() const;
    [[nodiscard]] std::vector<EventRecord> ActiveEvents(Timestamp now) const;
    [[nodiscard]] std::optional<EventRecord> FindEvent(std::string_view) const;
    [[nodiscard]] EventRefreshState RefreshState() const;
    [[nodiscard]] std::optional<Timestamp> LastSuccessfulRefresh() const;
private:
    void Refresh(std::stop_token);
    IEventSource& source_;
    EventEnrichment enrich_;
    EventCache cache_;
    mutable std::mutex mutex_;
    EventCatalog catalog_;
    EventSourceState sourceState_;
    EventRefreshState refreshState_;
    std::function<void()> changed_;
    bool started_{},cacheReady_{};
    std::jthread worker_;
};
}
