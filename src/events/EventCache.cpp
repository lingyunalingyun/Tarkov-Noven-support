#include "events/EventCache.h"
#include "raid/RaidJson.h"
#include <fstream>
#include <sstream>

namespace noven::events {
namespace {
namespace json = noven::raid::json;
using json::Value;
constexpr std::size_t kMaximumCache = 8*1024*1024;
std::string Number(const std::optional<Timestamp>& v) {return v?std::to_string(*v):"null";}
std::optional<Timestamp> Time(const Value& v) {
    if(v.type==Value::Type::Null) return {};
    const auto n=v.Int();if(n<0 || n>253402300799LL)throw std::runtime_error("invalid event time");return n;
}
template<class E> E Enum(const Value& v, int maximum) {
    const auto n=v.Int();if(n<0 || n>maximum)throw std::runtime_error("invalid event enum");return static_cast<E>(n);
}
std::vector<std::string> Strings(const Value& v) {
    if(v.Array().size()>256)throw std::runtime_error("event identity capacity exceeded");
    std::vector<std::string> out;for(const auto& item:v.Array())out.push_back(item.String());return out;
}
std::string Array(const std::vector<std::string>& values) {
    std::string out="[";for(const auto& value:values){if(out.size()>1)out+=',';out+=json::Quote(value);}return out+']';
}
bool ValidState(const EventSourceState& s) {
    return s.etag.size()<=1024 && s.lastModified.size()<=128
        && s.etag.find_first_of("\r\n")==s.etag.npos && s.lastModified.find_first_of("\r\n")==s.lastModified.npos
        && s.newestMessageId.size()<=20 && s.newestMessageId.find_first_not_of("0123456789")==s.newestMessageId.npos
        && (s.newestMessageId.empty() || s.newestMessageId.front()!='0')
        && (!s.lastSuccessfulRefresh || (*s.lastSuccessfulRefresh>=0 && *s.lastSuccessfulRefresh<=253402300799LL));
}
}
std::string EventCache::Encode(const EventCatalog& catalog, const EventSourceState& state) {
    std::ostringstream o;o<<"{\"schemaVersion\":1,\"state\":{\"etag\":"<<json::Quote(state.etag)
        <<",\"lastModified\":"<<json::Quote(state.lastModified)<<",\"newestMessageId\":"<<json::Quote(state.newestMessageId)
        <<",\"lastSuccessfulRefresh\":"<<Number(state.lastSuccessfulRefresh)<<"},\"events\":[";
    bool comma=false;for(const auto& e:catalog.Events()) {
        if(comma)o<<',';comma=true;
        o<<"{\"eventId\":"<<json::Quote(e.eventId)<<",\"title\":"<<json::Quote(e.title)<<",\"summary\":"<<json::Quote(e.summary)
            <<",\"sourceStatus\":"<<static_cast<int>(e.sourceStatus)<<",\"announcedAt\":"<<Number(e.announcedAt)
            <<",\"startsAt\":"<<Number(e.startsAt)<<",\"endsAt\":"<<Number(e.endsAt)<<",\"lastUpdatedAt\":"<<Number(e.lastUpdatedAt)
            <<",\"partial\":"<<(e.partial?"true":"false")<<",\"localizedTitles\":{";
        bool first=true;for(const auto& [locale,title]:e.localizedTitles){if(!first)o<<',';first=false;o<<json::Quote(locale)<<':'<<json::Quote(title);}
        o<<"},\"modes\":[";first=true;for(auto mode:e.modes){if(!first)o<<',';first=false;o<<static_cast<int>(mode);}
        o<<"],\"taskIds\":"<<Array(e.taskIds)<<",\"itemIds\":"<<Array(e.itemIds)<<",\"mapIds\":"<<Array(e.mapIds)
            <<",\"bossIds\":"<<Array(e.bossIds)<<",\"evidence\":[";
        first=true;for(const auto& v:e.sourceEvidence) {
            if(!first)o<<',';first=false;
            o<<"{\"id\":"<<json::Quote(v.evidenceId)<<",\"sourceRecordId\":"<<json::Quote(v.sourceRecordId)
                <<",\"sourceUrl\":"<<json::Quote(v.sourceUrl)<<",\"kind\":"<<static_cast<int>(v.sourceKind)
                <<",\"type\":"<<static_cast<int>(v.type)<<",\"publishedAt\":"<<Number(v.publishedAt)
                <<",\"linkedChanges\":"<<Array(v.linkedChangeRecordIds)
                <<",\"taskIds\":"<<Array(v.taskIds)<<",\"itemIds\":"<<Array(v.itemIds)<<",\"mapIds\":"<<Array(v.mapIds)
                <<",\"bossIds\":"<<Array(v.bossIds)<<",\"changedKey\":"<<json::Quote(v.changedKey)
                <<",\"oldValue\":"<<json::Quote(v.oldValue)<<",\"newValue\":"<<json::Quote(v.newValue)<<'}';
        }o<<"]}";
    }o<<"]}";return o.str();
}
bool EventCache::Decode(std::string_view text, EventCatalog& catalog, EventSourceState& state, std::string& error) {
    error.clear();try {
        if(text.size()>kMaximumCache)throw std::runtime_error("event cache capacity exceeded");
        const auto root=json::Parser(text).Parse();if(root.At("schemaVersion").Int()!=1)throw std::runtime_error("unsupported event schema");
        EventSourceState loaded;const auto& s=root.At("state");loaded.etag=s.At("etag").String();
        loaded.lastModified=s.At("lastModified").String();loaded.newestMessageId=s.At("newestMessageId").String();
        loaded.lastSuccessfulRefresh=Time(s.At("lastSuccessfulRefresh"));if(!ValidState(loaded))throw std::runtime_error("invalid source state");
        const auto& records=root.At("events").Array();if(records.size()>kMaximumEvents)throw std::runtime_error("event capacity exceeded");
        std::vector<EventRecord> events;for(const auto& v:records) {
            EventRecord e;e.eventId=v.At("eventId").String();e.title=v.At("title").String();e.summary=v.At("summary").String();
            e.sourceStatus=Enum<EventStatus>(v.At("sourceStatus"),3);e.announcedAt=Time(v.At("announcedAt"));
            e.startsAt=Time(v.At("startsAt"));e.endsAt=Time(v.At("endsAt"));e.lastUpdatedAt=Time(v.At("lastUpdatedAt"));
            e.partial=v.At("partial").Bool();const auto& titles=v.At("localizedTitles");
            if(titles.type!=Value::Type::Object)throw std::runtime_error("invalid localized titles");
            for(const auto& [locale,title]:titles.object)e.localizedTitles.emplace(locale,title.String());
            if(v.At("modes").Array().size()>3)throw std::runtime_error("mode capacity exceeded");
            for(const auto& mode:v.At("modes").Array())e.modes.push_back(Enum<EventMode>(mode,2));
            e.taskIds=Strings(v.At("taskIds"));e.itemIds=Strings(v.At("itemIds"));e.mapIds=Strings(v.At("mapIds"));e.bossIds=Strings(v.At("bossIds"));
            if(v.At("evidence").Array().size()>kMaximumEvidence)throw std::runtime_error("evidence capacity exceeded");
            for(const auto& item:v.At("evidence").Array()) {
                EventEvidence ev;ev.evidenceId=item.At("id").String();ev.sourceRecordId=item.At("sourceRecordId").String();
                ev.sourceUrl=item.At("sourceUrl").String();ev.sourceKind=Enum<SourceKind>(item.At("kind"),2);
                ev.type=Enum<EvidenceType>(item.At("type"),3);ev.publishedAt=Time(item.At("publishedAt"));
                ev.linkedChangeRecordIds=Strings(item.At("linkedChanges"));
                ev.taskIds=Strings(item.At("taskIds"));ev.itemIds=Strings(item.At("itemIds"));ev.mapIds=Strings(item.At("mapIds"));ev.bossIds=Strings(item.At("bossIds"));
                ev.changedKey=item.At("changedKey").String();ev.oldValue=item.At("oldValue").String();ev.newValue=item.At("newValue").String();e.sourceEvidence.push_back(std::move(ev));
            }events.push_back(std::move(e));
        }
        EventCatalog next;if(!next.Restore(std::move(events),error))return false;
        catalog=std::move(next);state=std::move(loaded);return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
EventCache::~EventCache(){if(lease_!=INVALID_HANDLE_VALUE)CloseHandle(lease_);}
bool EventCache::Load(const std::filesystem::path& file, EventCatalog& catalog, EventSourceState& state, std::string& error) {
    error.clear();writable_=false;file_=file;
    if(lease_!=INVALID_HANDLE_VALUE){CloseHandle(lease_);lease_=INVALID_HANDLE_VALUE;}
    try {
        std::filesystem::create_directories(file.parent_path());
        lease_=CreateFileW((file.wstring()+L".lock").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(lease_==INVALID_HANDLE_VALUE)throw std::runtime_error("event cache owned/unavailable");
        if(std::filesystem::exists(file)) {
            const auto size=std::filesystem::file_size(file);if(size>kMaximumCache)throw std::runtime_error("event cache too large");
            std::ifstream in(file,std::ios::binary);std::string text(static_cast<std::size_t>(size),'\0');
            if(!in.read(text.data(),static_cast<std::streamsize>(size)))throw std::runtime_error("event cache read failed");
            if(!Decode(text,catalog,state,error))return false;
        }else{catalog=EventCatalog{};state=EventSourceState{};}
        writable_=true;return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
bool EventCache::Save(const EventCatalog& catalog, const EventSourceState& state, std::string& error) {
    error.clear();if(!writable_){error="event cache not writable";return false;}
    const auto text=Encode(catalog,state);EventCatalog verify;EventSourceState verified;
    if(!Decode(text,verify,verified,error))return false;
    const auto temp=std::filesystem::path(file_.wstring()+L".tmp");
    HANDLE h=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE){error="event temp open failed";return false;}
    DWORD written{};const bool ok=WriteFile(h,text.data(),static_cast<DWORD>(text.size()),&written,nullptr)
        && written==text.size() && FlushFileBuffers(h);CloseHandle(h);
    if(!ok){DeleteFileW(temp.c_str());error="event temp write failed";return false;}
    const bool replaced=GetFileAttributesW(file_.c_str())!=INVALID_FILE_ATTRIBUTES
        ? ReplaceFileW(file_.c_str(),temp.c_str(),nullptr,0,nullptr,nullptr)!=0
        : MoveFileExW(temp.c_str(),file_.c_str(),MOVEFILE_WRITE_THROUGH)!=0;
    if(!replaced){DeleteFileW(temp.c_str());error="event atomic replace failed";return false;}return true;
}
}
