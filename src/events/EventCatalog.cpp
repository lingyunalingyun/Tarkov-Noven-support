#include "events/EventCatalog.h"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <set>

namespace noven::events {
namespace {
bool MessageId(std::string_view id) {
    return !id.empty() && id.size() <= 20 && id.front() != '0'
        && id.find_first_not_of("0123456789") == id.npos;
}
std::string Identity(std::string_view id) { return "official-telegram:" + std::string(id); }
std::string Url(std::string_view id) { return "https://t.me/escapefromtarkovEN/" + std::string(id); }
bool Time(const std::optional<Timestamp>& t) { return !t || (*t >= 0 && *t <= 253402300799LL); }
bool Ids(const std::vector<std::string>& ids) {
    if (ids.size() > 256) return false;
    std::set<std::string> unique;
    for (const auto& id : ids) if (id.empty() || id.size() > 256 || !unique.insert(id).second) return false;
    return true;
}
void RebuildEntities(EventRecord& e) {
    e.itemIds.clear();e.taskIds.clear();e.mapIds.clear();e.bossIds.clear();
    auto append=[](auto& to,const auto& from){for(const auto& id:from)if(std::ranges::find(to,id)==to.end())to.push_back(id);};
    for(const auto& evidence:e.sourceEvidence) {
        append(e.itemIds,evidence.itemIds);append(e.taskIds,evidence.taskIds);
        append(e.mapIds,evidence.mapIds);append(e.bossIds,evidence.bossIds);
    }
}
}
std::optional<Timestamp> ParseTimestamp(std::string_view text) {
    // 仅接受明确的 ISO 8601 时区，拒绝本地时间、无效日历和溢出。
    // Accept explicit ISO 8601 offsets only; reject local time, invalid calendars and overflow.
    if (text.size() != 20 && text.size() != 25) return {};
    if (text[4]!='-' || text[7]!='-' || text[10]!='T' || text[13]!=':' || text[16]!=':') return {};
    auto number = [&](std::size_t offset, std::size_t count) {
        int n = -1; const auto part = text.substr(offset, count);
        if (part.find_first_not_of("0123456789") != part.npos) return -1;
        const auto result = std::from_chars(part.data(), part.data()+part.size(), n);
        return result.ec == std::errc{} ? n : -1;
    };
    using namespace std::chrono;
    const int y=number(0,4), m=number(5,2), d=number(8,2), h=number(11,2), min=number(14,2), s=number(17,2);
    const year_month_day date{year{y}, month{static_cast<unsigned>(m)}, day{static_cast<unsigned>(d)}};
    if (y<1970 || !date.ok() || h<0 || h>23 || min<0 || min>59 || s<0 || s>59) return {};
    int offset{};
    if (text.size()==20) { if(text[19]!='Z') return {}; }
    else {
        if ((text[19]!='+' && text[19]!='-') || text[22]!=':') return {};
        const int oh=number(20,2), om=number(23,2);
        if(oh<0 || oh>14 || om<0 || om>59 || (oh==14 && om!=0)) return {};
        offset=(oh*60+om)*60*(text[19]=='+' ? 1 : -1);
    }
    const auto result=duration_cast<seconds>(sys_days{date}.time_since_epoch()).count()+h*3600+min*60+s-offset;
    return result>=0 && result<=253402300799LL ? std::optional<Timestamp>{result} : std::nullopt;
}
EventStatus StatusAt(const EventRecord& e, Timestamp now) noexcept {
    if (e.sourceStatus==EventStatus::Ended || (e.endsAt && *e.endsAt<=now)) return EventStatus::Ended;
    if (e.startsAt) return *e.startsAt>now ? EventStatus::Upcoming : EventStatus::Active;
    return e.sourceStatus;
}
bool ValidEvidence(const EventEvidence& e) {
    if(e.sourceKind==SourceKind::OfficialTelegram) {
        if(e.type!=EvidenceType::Announcement && e.type!=EvidenceType::Update)return false;
        for(const auto& id:e.linkedChangeRecordIds)if(!MessageId(id))return false;
    }else {
        if(!e.linkedChangeRecordIds.empty())return false;
        if(e.sourceKind==SourceKind::TarkovDev && (e.type!=EvidenceType::EntityReference || e.sourceUrl!="https://tarkov.dev/api/"))return false;
        if(e.sourceKind==SourceKind::TarkovChanges && (e.type!=EvidenceType::ConfigurationChange || !MessageId(e.sourceRecordId)
            || e.sourceUrl!="https://changes.tarkov-changes.com/view/"+e.sourceRecordId))return false;
    }
    return !e.evidenceId.empty() && e.evidenceId.size()<=256 && !e.sourceRecordId.empty()
        && e.sourceRecordId.size()<=256 && e.sourceUrl.size()<=2048 && e.sourceUrl.starts_with("https://")
        && static_cast<int>(e.sourceKind)>=0 && static_cast<int>(e.sourceKind)<=2
        && static_cast<int>(e.type)>=0 && static_cast<int>(e.type)<=3 && Time(e.publishedAt)
        && Ids(e.itemIds) && Ids(e.taskIds) && Ids(e.mapIds) && Ids(e.bossIds)
        && e.linkedChangeRecordIds.size()<=4 && Ids(e.linkedChangeRecordIds)
        && e.changedKey.size()<=1024 && e.oldValue.size()<=1024 && e.newValue.size()<=1024
        && (e.sourceKind!=SourceKind::OfficialTelegram || (MessageId(e.sourceRecordId)
            && e.sourceUrl==Url(e.sourceRecordId)));
}
bool ValidRecord(const EventRecord& e) {
    if (!e.eventId.starts_with("official-telegram:") || !MessageId(std::string_view(e.eventId).substr(18))
        || e.title.empty() || e.title.size()>2048 || e.summary.size()>16384
        || static_cast<int>(e.sourceStatus)<0 || static_cast<int>(e.sourceStatus)>3
        || !Time(e.announcedAt) || !Time(e.startsAt) || !Time(e.endsAt) || !Time(e.lastUpdatedAt)
        || (e.startsAt && e.endsAt && *e.endsAt<*e.startsAt)
        || !Ids(e.taskIds) || !Ids(e.itemIds) || !Ids(e.mapIds) || !Ids(e.bossIds)
        || e.sourceEvidence.empty() || e.sourceEvidence.size()>kMaximumEvidence || e.modes.size()>3
        || e.localizedTitles.size()>16) return false;
    std::set<EventMode> modes;
    for(auto mode:e.modes) if(static_cast<int>(mode)<0 || static_cast<int>(mode)>2 || !modes.insert(mode).second) return false;
    for(const auto& [language,title]:e.localizedTitles) if(language.empty() || language.size()>32 || title.size()>2048) return false;
    bool origin=false; std::set<std::string> evidenceIds, linkedChanges;
    for(const auto& evidence:e.sourceEvidence)if(evidence.sourceKind==SourceKind::OfficialTelegram)
        for(const auto& id:evidence.linkedChangeRecordIds)linkedChanges.insert(id);
    for(const auto& evidence:e.sourceEvidence) {
        if(!ValidEvidence(evidence) || !evidenceIds.insert(evidence.evidenceId).second) return false;
        if(evidence.sourceKind==SourceKind::TarkovChanges && !linkedChanges.contains(evidence.sourceRecordId))return false;
        origin |= evidence.sourceKind==SourceKind::OfficialTelegram && evidence.type==EvidenceType::Announcement
            && Identity(evidence.sourceRecordId)==e.eventId;
    }
    auto rebuilt=e;RebuildEntities(rebuilt);
    return origin && e.itemIds==rebuilt.itemIds && e.taskIds==rebuilt.taskIds && e.mapIds==rebuilt.mapIds && e.bossIds==rebuilt.bossIds;
}
const EventRecord* EventCatalog::FindEvent(std::string_view id) const noexcept {
    const auto it=std::ranges::find(events_,id,&EventRecord::eventId);
    return it==events_.end()?nullptr:&*it;
}
std::vector<EventRecord> EventCatalog::ActiveEvents(Timestamp now) const {
    std::vector<EventRecord> result;
    for(const auto& e:events_) if(StatusAt(e,now)==EventStatus::Active) result.push_back(e);
    return result;
}
bool EventCatalog::Restore(std::vector<EventRecord> records, std::string& error) {
    error.clear(); std::set<std::string> ids;
    if(records.size()>kMaximumEvents) { error="event capacity exceeded"; return false; }
    for(const auto& e:records) if(!ValidRecord(e) || !ids.insert(e.eventId).second) {
        error="invalid event record"; return false;
    }
    events_=std::move(records); return true;
}
bool EventCatalog::Apply(std::span<const OfficialAnnouncement> announcements, std::string& error) {
    error.clear(); if(announcements.size()>256) {error="announcement capacity exceeded";return false;}
    auto next=events_; std::vector<OfficialAnnouncement> unresolved;
    for(const auto& a:announcements) {
        if(!MessageId(a.sourceRecordId) || a.sourceUrl!=Url(a.sourceRecordId)
            || (a.updatesRecordId && (!MessageId(*a.updatesRecordId) || *a.updatesRecordId==a.sourceRecordId))
            || !Time(a.publishedAt) || !Time(a.startsAt) || !Time(a.endsAt)
            || (a.startsAt && a.endsAt && *a.endsAt<*a.startsAt)
            || a.title.empty() || a.title.size()>2048 || a.summary.size()>16384
            || static_cast<int>(a.status)<0 || static_cast<int>(a.status)>3 || a.modes.size()>3
            || a.linkedChangeRecordIds.size()>4) {error="invalid official facts/identity";return false;}
        const auto id=Identity(a.updatesRecordId.value_or(a.sourceRecordId));
        auto it=std::ranges::find(next,id,&EventRecord::eventId);
        if(it==next.end()) {
            if(a.updatesRecordId) { unresolved.push_back(a); continue; }
            EventRecord e; e.eventId=id; e.title=a.title; e.summary=a.summary; e.announcedAt=a.publishedAt;
            next.push_back(std::move(e)); it=next.end()-1;
        }
        EventEvidence evidence; evidence.evidenceId=Identity(a.sourceRecordId); evidence.sourceRecordId=a.sourceRecordId;
        evidence.sourceUrl=a.sourceUrl; evidence.publishedAt=a.publishedAt;
        evidence.linkedChangeRecordIds=a.linkedChangeRecordIds;
        evidence.type=a.updatesRecordId?EvidenceType::Update:EvidenceType::Announcement;
        auto old=std::ranges::find(it->sourceEvidence,evidence.evidenceId,&EventEvidence::evidenceId);
        // 旧公告回放不能撤销较新的更新；同消息编辑仍可补充事实。
        // Replayed older announcements cannot undo newer updates; edits can supplement facts.
        const bool newer=!it->lastUpdatedAt || (a.publishedAt && *a.publishedAt>=*it->lastUpdatedAt);
        if(newer) {
            if(!a.updatesRecordId) {it->title=a.title;it->summary=a.summary;it->titleIsExcerpt=a.titleIsExcerpt;}
            if(a.status!=EventStatus::Unknown) it->sourceStatus=a.status;
            if(a.startsAt) it->startsAt=a.startsAt;
            if(a.endsAt) it->endsAt=a.endsAt;
            if(!a.modes.empty()) it->modes=a.modes;
            it->lastUpdatedAt=a.publishedAt;
        }
        if(old==it->sourceEvidence.end()) {
            if(it->sourceEvidence.size()>=kMaximumEvidence) {error="event evidence capacity exceeded";return false;}
            it->sourceEvidence.push_back(std::move(evidence));
        } else *old=std::move(evidence);
        it->partial=!(it->startsAt && it->endsAt && !it->modes.empty());
        if(!ValidRecord(*it)) {error="invalid official facts";return false;}
    }
    // 有限窗口：按公告时间保留最新身份，不归档无限频道历史。
    // Bounded window: retain latest announced identities, never archive an unlimited channel.
    std::ranges::stable_sort(next,[](const auto& a,const auto& b){return a.announcedAt>b.announcedAt;});
    if(next.size()>kMaximumEvents) next.resize(kMaximumEvents);
    events_=std::move(next);unresolved_=std::move(unresolved);return true;
}
bool EventCatalog::AttachEvidence(std::string_view id, EventEvidence evidence, std::string& error) {
    error.clear(); auto it=std::ranges::find(events_,id,&EventRecord::eventId);
    if(it==events_.end() || !ValidEvidence(evidence) || evidence.sourceKind==SourceKind::OfficialTelegram) {
        error="invalid enrichment evidence";return false;
    }
    auto next=*it; auto old=std::ranges::find(next.sourceEvidence,evidence.evidenceId,&EventEvidence::evidenceId);
    if(old==next.sourceEvidence.end()) {
        if(next.sourceEvidence.size()>=kMaximumEvidence) {error="event evidence capacity exceeded";return false;}
        next.sourceEvidence.push_back(evidence);
    } else *old=evidence;
    RebuildEntities(next);
    if(!ValidRecord(next)) {error="invalid enriched record";return false;}
    *it=std::move(next);return true;
}
}
