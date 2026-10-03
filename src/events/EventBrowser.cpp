#include "events/EventBrowser.h"
#include <algorithm>
#include <array>
#include <numeric>

namespace noven::events {
namespace {
std::string Fold(std::string value) {
    for(auto& c:value)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');
    return value;
}
int Priority(EventStatus status) {
    switch(status){case EventStatus::Active:return 0;case EventStatus::Upcoming:return 1;
        case EventStatus::Unknown:return 2;case EventStatus::Ended:return 3;}
    return 2;
}
auto Groups(const EventRecord& e) {
    return std::array<std::pair<EntityKind,const std::vector<std::string>*>,4>{{
        {EntityKind::Map,&e.mapIds},{EntityKind::Task,&e.taskIds},
        {EntityKind::Item,&e.itemIds},{EntityKind::Boss,&e.bossIds}}};
}
}
void EventBrowser::SetEvents(std::vector<EventRecord> records) {
    if(events_==records)return;
    events_=std::move(records);PrepareSearch();Rebuild();
}
void EventBrowser::SetEntities(std::map<EntityKey,std::string> names) {
    if(names_==names)return;
    names_=std::move(names);PrepareSearch();Rebuild();
}
void EventBrowser::SetFilter(std::optional<EventStatus> status,std::string query) {
    query=Fold(std::move(query));if(filter_==status&&query_==query)return;
    filter_=status;query_=std::move(query);Rebuild();
}
void EventBrowser::SetNow(Timestamp now) {
    const bool changed=std::ranges::any_of(events_,[&](const auto& e){return StatusAt(e,now_)!=StatusAt(e,now);});
    now_=now;if(changed)Rebuild();
}
const EventRecord* EventBrowser::Find(std::string_view id) const noexcept {
    const auto i=std::ranges::find(events_,id,&EventRecord::eventId);return i==events_.end()?nullptr:&*i;
}
std::vector<EventEntity> EventBrowser::Associations(const EventRecord& event) const {
    std::vector<EventEntity> result;
    for(const auto& [kind,ids]:Groups(event))for(const auto& id:*ids)
        if(const auto name=names_.find({kind,id});name!=names_.end())result.push_back({kind,id,name->second});
    return result;
}
std::size_t EventBrowser::Unresolved(const EventRecord& event) const {
    std::size_t count{};for(const auto& [kind,ids]:Groups(event))for(const auto& id:*ids)if(!names_.contains({kind,id}))++count;
    return count;
}
std::size_t EventBrowser::Count(EventStatus status) const {
    return std::ranges::count_if(events_,[&](const auto& e){return Status(e)==status;});
}
void EventBrowser::PrepareSearch() {
    search_.clear();search_.reserve(events_.size());
    for(const auto& e:events_) {
        std::string text=e.eventId+"\n"+e.title+"\n"+e.summary;
        for(const auto& entity:Associations(e))text+='\n'+entity.name;
        search_.push_back(Fold(std::move(text)));
    }
}
void EventBrowser::Rebuild() {
    ++builds_;rows_.clear();
    for(std::size_t i=0;i<events_.size();++i)
        if((!filter_||Status(events_[i])==*filter_) && (query_.empty()||search_[i].find(query_)!=std::string::npos))rows_.push_back(i);
    // UI 排序独立于来源目录：状态优先，未来开始升序，其他相关时间降序，身份兜底。
    // UI ordering is independent of source order: status, upcoming start ascending, other times descending, then ID.
    std::ranges::sort(rows_,[&](auto a,auto b) {
        const auto& x=events_[a];const auto& y=events_[b];const auto sx=Status(x),sy=Status(y);
        if(sx!=sy)return Priority(sx)<Priority(sy);
        const auto time=[&](const auto& e){return sx==EventStatus::Ended?e.endsAt.value_or(e.announcedAt.value_or(0)):
            e.startsAt.value_or(e.announcedAt.value_or(0));};
        if(time(x)!=time(y))return sx==EventStatus::Upcoming?time(x)<time(y):time(x)>time(y);
        return x.eventId<y.eventId;
    });
}
bool SafeEventSourceUrl(std::string_view url) noexcept {
    for(auto prefix:{"https://t.me/escapefromtarkovEN/","https://changes.tarkov-changes.com/view/"})
        if(url.starts_with(prefix)) {
            const auto id=url.substr(std::string_view(prefix).size());
            return !id.empty()&&id.size()<=20&&id.front()!='0'&&id.find_first_not_of("0123456789")==id.npos;
        }
    return false;
}
}
