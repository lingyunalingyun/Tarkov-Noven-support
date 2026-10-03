#include "events/EventService.h"
#include <chrono>

namespace noven::events {
bool EventService::Start(const std::filesystem::path& file) {
    if(started_)return cacheReady_;
    started_=true;
    {std::lock_guard lock(mutex_);cacheReady_=cache_.Load(file,catalog_,sourceState_,refreshState_.error);}
    RequestRefresh();return cacheReady_;
}
bool EventService::RequestRefresh() {
    if(!started_)return false;
    {std::lock_guard lock(mutex_);if(refreshState_.phase==RefreshPhase::Refreshing)return false;}
    if(worker_.joinable())worker_.join();
    {std::lock_guard lock(mutex_);refreshState_.phase=RefreshPhase::Refreshing;refreshState_.enrichmentWarning.clear();}
    // 单次 worker 执行后退出，没有空闲轮询、定时器或渲染帧任务。
    // A single-shot worker exits after refresh; no idle polling, timer or render-frame work.
    worker_=std::jthread([this](std::stop_token stop){Refresh(stop);});return true;
}
void EventService::Stop(){worker_.request_stop();if(worker_.joinable())worker_.join();}
void EventService::Refresh(std::stop_token stop) {
    EventCatalog next;EventSourceState state;std::string error,warning;
    {std::lock_guard lock(mutex_);next=catalog_;state=sourceState_;}
    bool published=false;
    try {
        auto result=source_.Fetch(state,stop);
        if(!result.success)error=result.error.empty()?"event source refresh failed":result.error;
        else if(!cacheReady_)error="event cache unavailable; original file preserved";
        else if(!result.notModified && !next.Apply(result.announcements,error)){}
        else {
            if(enrich_ && !result.notModified && !stop.stop_requested()) {
                auto enriched=next;
                if(enrich_(enriched,stop,warning))next=std::move(enriched);
            }
            if(stop.stop_requested())error="event refresh stopped";
            else {
                state=std::move(result.state);
                state.lastSuccessfulRefresh=std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                // 先原子落盘，再发布快照；保存失败不能推进内存游标或清空旧目录。
                // Persist atomically before publishing; save failure cannot advance cursors or clear the old catalog.
                if(cache_.Save(next,state,error))published=true;
            }
        }
    }catch(const std::exception& e){error=e.what();}
    {std::lock_guard lock(mutex_);
        if(published){catalog_=std::move(next);sourceState_=std::move(state);}
        refreshState_={published?RefreshPhase::Ready:RefreshPhase::Failed,std::move(error),std::move(warning)};
    }
    if(changed_) {try{changed_();}catch(...){} }
}
std::vector<EventRecord> EventService::Events() const {std::lock_guard lock(mutex_);return catalog_.Events();}
std::vector<EventRecord> EventService::ActiveEvents(Timestamp now) const {std::lock_guard lock(mutex_);return catalog_.ActiveEvents(now);}
std::optional<EventRecord> EventService::FindEvent(std::string_view id) const {
    std::lock_guard lock(mutex_);if(const auto* event=catalog_.FindEvent(id))return *event;return {};
}
EventRefreshState EventService::RefreshState() const {std::lock_guard lock(mutex_);return refreshState_;}
std::optional<Timestamp> EventService::LastSuccessfulRefresh() const {std::lock_guard lock(mutex_);return sourceState_.lastSuccessfulRefresh;}
}
