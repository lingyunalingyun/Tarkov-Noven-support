#pragma once
#include "events/EventHttp.h"
#include "events/EventTypes.h"
#include <stop_token>

namespace noven::events {
inline constexpr std::wstring_view kWikiEventPath=L"/api.php?action=query&titles=Escape_from_Tarkov_Wiki/Section_3%7CEvents&prop=revisions&rvprop=ids%7Ctimestamp%7Ccontent&rvslots=main&format=json&formatversion=2";
struct CommunitySourceResult {bool success{};std::vector<CommunityAnnouncement> announcements;std::string error;};
class ICommunityEventSource {
public:
    virtual ~ICommunityEventSource()=default;
    virtual CommunitySourceResult Fetch(std::stop_token)=0;
};
// 仅解析已核实的公开 MediaWiki 修订契约；社区记录不进入官方公告类型。
// Parse only the verified public MediaWiki revision contract; community records never become official announcements.
class WikiEventSource final : public ICommunityEventSource {
public:
    explicit WikiEventSource(IEventHttp& http):http_(http){}
    CommunitySourceResult Fetch(std::stop_token) override;
    static CommunitySourceResult Parse(const HttpResponse&);
private:
    IEventHttp& http_;
};
}
