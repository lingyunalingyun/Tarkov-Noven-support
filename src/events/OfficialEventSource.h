#pragma once
#include "events/IEventSource.h"
#include "events/EventHttp.h"

namespace noven::events {
// 仅英语官方公开页的明确游戏内活动措辞；HTML 结构全部封装于此。
// Only explicit in-game event wording on the official English public page; HTML structure stays here.
class OfficialEventSource final : public IEventSource {
public:
    explicit OfficialEventSource(IEventHttp& http):http_(http){}
    EventSourceResult Fetch(const EventSourceState&,std::stop_token) override;
    static EventSourceResult Parse(const HttpResponse&,const EventSourceState& previous={});
private:
    IEventHttp& http_;
};
}
