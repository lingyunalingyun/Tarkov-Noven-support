#include "events/OfficialEventSource.h"
#include "events/EventHtml.h"
#include <algorithm>
#include <set>

namespace noven::events {
namespace {
bool Id(std::string_view id){return !id.empty() && id.size()<=20 && id.front()!='0' && id.find_first_not_of("0123456789")==id.npos;}
bool Larger(std::string_view a,std::string_view b){return a.size()!=b.size()?a.size()>b.size():a>b;}
std::optional<Timestamp> ExplicitTime(std::string_view text,std::string_view label) {
    const auto p=text.find(label);if(p==text.npos)return {};
    const auto start=p+label.size();
    for(const std::size_t length:{25U,20U})if(start+length<=text.size())if(auto t=ParseTimestamp(text.substr(start,length)))return t;
    return {};
}
std::vector<EventMode> DeclaredModes(std::string_view lead) {
    std::string_view declaration;
    for(auto prefix:{"An in-game event has started in ","The in-game event has started in ",
        "An in-game event will start in ","The in-game event will start in "})
        if(lead.starts_with(prefix))declaration=lead.substr(std::string_view(prefix).size());
    for(auto prefix:{"An in-game event will start at ","The in-game event will start at "})if(lead.starts_with(prefix)) {
        const auto rest=lead.substr(std::string_view(prefix).size());
        for(std::size_t length:{25U,20U})if(rest.size()>length && ParseTimestamp(rest.substr(0,length))
            && rest.substr(length).starts_with(" in "))declaration=rest.substr(length+4);
    }
    std::vector<EventMode> modes;
    if(declaration.starts_with("the seasonal "))modes.push_back(EventMode::Seasonal);
    if(declaration.starts_with("PvE mode")) {
        modes.push_back(EventMode::PvE);
        if(declaration.starts_with("PvE mode and in PvP mode"))modes.push_back(EventMode::PvP);
    }else if(declaration.starts_with("PvP mode")) {
        modes.push_back(EventMode::PvP);
        if(declaration.starts_with("PvP mode and in PvE mode"))modes.push_back(EventMode::PvE);
    }
    return modes;
}
std::optional<std::string> OriginalLink(std::string_view body) {
    constexpr std::string_view prefix="https://t.me/escapefromtarkovEN/";
    std::optional<std::string> result;std::size_t cursor{};
    while((cursor=body.find("href=\"",cursor))!=body.npos) {
        const auto end=body.find('>',cursor);if(end==body.npos)throw std::runtime_error("broken event link");
        const auto link=html::Attribute(body.substr(cursor,end-cursor+1),"href");
        if(link.starts_with(prefix) && Id(link.substr(prefix.size()))) {
            if(result && *result!=link.substr(prefix.size()))return {};
            result=link.substr(prefix.size());
        }cursor=end+1;
    }return result;
}
}
EventSourceResult OfficialEventSource::Parse(const HttpResponse& response,const EventSourceState& previous) {
    EventSourceResult result;result.state=previous;
    if(response.status==304 && response.error.empty()) {
        if(!previous.lastSuccessfulRefresh || (previous.etag.empty() && previous.lastModified.empty())) {
            result.error="unsolicited event HTTP 304";return result;
        }result.success=true;result.notModified=true;return result;
    }
    if(!ValidateHtml(response,result.error))return result;
    try {
        const std::string_view page=response.body;
        // 来源身份与消息结构同时校验；空壳/登录页不能成功发布空目录。
        // Validate source identity AND message shape; shells/login pages cannot publish empty catalogs.
        if(page.find("<meta property=\"og:title\" content=\"Escape from Tarkov Official\">")==page.npos
            || page.find("tg://resolve?domain=escapefromtarkovEN")==page.npos)
            throw std::runtime_error("official channel identity missing");
        constexpr std::string_view marker="data-post=\"escapefromtarkovEN/";
        std::size_t cursor=page.find(marker),count{},textCount{};std::set<std::string> seen;
        if(cursor==page.npos)throw std::runtime_error("official message structure missing");
        while(cursor!=page.npos) {
            if(++count>100)throw std::runtime_error("official page exceeds message window");
            const auto idStart=cursor+marker.size(),idEnd=page.find('"',idStart);
            if(idEnd==page.npos)throw std::runtime_error("official message ID missing");
            const auto id=page.substr(idStart,idEnd-idStart);if(!Id(id))throw std::runtime_error("invalid official message ID");
            const auto next=page.find(marker,idEnd);const auto block=page.substr(idEnd,next==page.npos?page.size()-idEnd:next-idEnd);
            const auto tp=block.find("datetime=\"");if(tp==block.npos)throw std::runtime_error("official publication time missing");
            const auto time=ParseTimestamp(html::Attribute(block.substr(tp),"datetime"));if(!time)throw std::runtime_error("invalid official publication time");
            if(Larger(id,result.state.newestMessageId))result.state.newestMessageId=id;
            const auto textMarker=block.find("class=\"tgme_widget_message_text js-message_text\"");
            if(textMarker==block.npos && block.find("tgme_widget_message_text")!=block.npos)
                throw std::runtime_error("official text structure changed");
            if(textMarker!=block.npos && seen.insert(std::string(id)).second) {
                ++textCount;
                const auto begin=block.find('>',textMarker),end=block.find("</div>",begin);
                if(begin==block.npos || end==block.npos)throw std::runtime_error("official text structure missing");
                const auto body=block.substr(begin+1,end-begin-1);auto text=html::Text(body);
                if(text.size()>16384)throw std::runtime_error("official text exceeds capacity");
                const bool started=text.starts_with("An in-game event has started") || text.starts_with("The in-game event has started");
                const bool upcoming=text.starts_with("An in-game event will start") || text.starts_with("The in-game event will start");
                const bool ended=text.starts_with("The in-game event has ended");
                const bool updated=text.starts_with("The in-game event will remain active until");
                if(started || upcoming || ended || updated) {
                    const auto lead=std::string_view(text).substr(0,text.find_first_of(".\n"));
                    if(lead.find("#TarkovArena")!=lead.npos){cursor=next;continue;}
                    const auto link=OriginalLink(body);
                    // 独立结束声明不生成新活动；没有明确原公告链接时保持未关联。
                    // Standalone end notices do not create new events; absent explicit origin links remain unassociated.
                    if((ended || updated) && !link){cursor=next;continue;}
                    // 混合公告只归入首段活动；后续另一个活动的结束声明不污染其状态/实体。
                    // Mixed posts retain the leading event only; another event's end notice cannot contaminate it.
                    const auto mixed=text.find("\n\nThe weekend event has ended");
                    if(mixed!=text.npos)text.resize(mixed);
                    OfficialAnnouncement a;a.sourceRecordId=id;a.sourceUrl="https://t.me/escapefromtarkovEN/"+std::string(id);
                    a.publishedAt=time;a.summary=text;
                    a.titleIsExcerpt=true;
                    constexpr std::string_view changeLink="href=\"https://changes.tarkov-changes.com/view/";
                    std::size_t linkPos{};
                    while((linkPos=body.find(changeLink,linkPos))!=body.npos) {
                        const auto start=linkPos+changeLink.size(),finish=body.find('"',start);
                        if(finish==body.npos || !Id(body.substr(start,finish-start)))throw std::runtime_error("invalid official change link");
                        const std::string changeId(body.substr(start,finish-start));
                        if(std::ranges::find(a.linkedChangeRecordIds,changeId)==a.linkedChangeRecordIds.end())a.linkedChangeRecordIds.push_back(changeId);
                        if(a.linkedChangeRecordIds.size()>4)throw std::runtime_error("official change links exceed capacity");
                        linkPos=finish+1;
                    }
                    // 未提供标题时保留原文首段摘录，而非猜测活动名称。
                    // Without an official title, retain an original leading excerpt, never invent an event name.
                    a.title=text.substr(0,std::min<std::size_t>(text.find_first_of("\n:"),2048));
                    a.status=ended?EventStatus::Ended:started?EventStatus::Active:EventStatus::Unknown;
                    if(ended || updated)a.updatesRecordId=link;
                    a.startsAt=ExplicitTime(text,"will start at ");a.endsAt=ExplicitTime(text,"will remain active until ");
                    // 年份/时区不完整的自然语言日期不以发布时间推测补齐。
                    // Natural-language dates lacking complete year/zone are not guessed from publication time.
                    // 仅首句的肯定范围声明；后文否定/例外规则不能反向成为适用范围。
                    // Only affirmative scope in the leading sentence; later negations/exceptions cannot establish scope.
                    a.modes=DeclaredModes(lead);
                    result.announcements.push_back(std::move(a));
                }
            }cursor=next;
        }
        if(!textCount)throw std::runtime_error("official text contract missing");
        result.state.etag=response.etag;result.state.lastModified=response.lastModified;
        result.success=true;return result;
    }catch(const std::exception& e){result.success=false;result.announcements.clear();result.state=previous;result.error=e.what();return result;}
}
EventSourceResult OfficialEventSource::Fetch(const EventSourceState& previous,std::stop_token stop) {
    if(stop.stop_requested()){EventSourceResult r;r.error="event refresh stopped";return r;}
    // Telegram 当前响应 no-store 且无 ETag；每次只读取一个有限最近窗口，兼顾原消息编辑。
    // Telegram currently sends no-store without ETag; one bounded recent window also captures message edits.
    return Parse(http_.Get(L"t.me",L"/s/escapefromtarkovEN",previous.etag,previous.lastModified),previous);
}
}
