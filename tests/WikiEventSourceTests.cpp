#include "events/WikiEventSource.h"
#include "raid/RaidJson.h"
#include <iostream>
#include <stdexcept>
using namespace noven::events;
void Check(bool value){if(!value)throw std::runtime_error("wiki source assertion");}
// 最小合成片段复现已验证表格/修订结构，不镜像实际网页。
// Minimal synthetic fragments reproduce the verified table/revision structure, not mirrored pages.
std::string Page(int id,std::string_view title,std::string_view content) {
    return "{\"pageid\":"+std::to_string(id)+",\"title\":"+noven::raid::json::Quote(title)+
        ",\"revisions\":[{\"revid\":100,\"timestamp\":\"2026-09-29T22:45:34Z\",\"slots\":{\"main\":{\"contentmodel\":\"wikitext\",\"contentformat\":\"text/x-wiki\",\"content\":"+
        noven::raid::json::Quote(content)+"}}}]}";
}
HttpResponse Fixture(std::string_view current,std::string_view article,int id=3552) {
    HttpResponse r;r.status=200;r.contentType="application/json; charset=utf-8";
    r.body="{\"query\":{\"pages\":["+Page(26936,"Events",article)+","+Page(id,"Escape from Tarkov Wiki/Section 3",current)+"]}}";return r;
}
int main(int argc,char** argv) {
    try {
        if(argc==2 && std::string_view(argv[1])=="--live") {
            WinHttpEventClient http;WikiEventSource source(http);auto r=source.Fetch({});
            if(!r.success)throw std::runtime_error(r.error);
            std::cout<<"Native live Wiki events="<<r.announcements.size()<<'\n';
            for(const auto& a:r.announcements)std::cout<<a.sourceRecordId<<" | "<<a.title<<'\n';return 0;
        }
        const std::string current="<div id=\"fpevents\">Current Events\n====Tarkov====\n{|\n|[[File:Test Banner.png|225px|Test Balance]]\n|-\n|• [[Glukhar]] moved to [[Lighthouse]]<br/>• New questline: [[Fog of War]]\n|}\n====Arena====\n• Currently none";
        const std::string article="==Test Balance (8 September 2026)==\nFile:Test Banner.png\n* Quest [[Fog of War]] has been added.\n==Unrelated==\n* no\n";
        auto r=WikiEventSource::Parse(Fixture(current,article));Check(r.success && r.announcements.size()==1);
        const auto a=r.announcements.front();Check(a.title=="Test Balance" && a.modes.empty() && !a.officialRecordId);
        Check(a.sourceRecordId=="26936:Test_Balance_%288_September_2026%29");
        Check(a.summary.find("Fog of War")!=a.summary.npos && a.summary.find("Unrelated")==a.summary.npos);
        Check(WikiEventSource::Parse(Fixture(current,article)).announcements.front().sourceRecordId==a.sourceRecordId);
        Check(!WikiEventSource::Parse(Fixture(current,article,1)).success);
        Check(!WikiEventSource::Parse(Fixture(current,article+"==Duplicate==\nFile:Test Banner.png\n")).success);
        Check(!WikiEventSource::Parse(Fixture(current,"==Unknown==\n")).success);
        auto response=Fixture(current,article);response.body="<html>Login</html>";Check(!WikiEventSource::Parse(response).success);
        response=Fixture(current,article);response.body=std::string(kMaximumResponseBytes+1,' ');Check(!WikiEventSource::Parse(response).success);
        response=Fixture(current,article);response.contentType="text/html";Check(!WikiEventSource::Parse(response).success);
        response=Fixture(current,article);response.status=403;Check(!WikiEventSource::Parse(response).success);
        response=Fixture(current,article);response.body.back()='x';Check(!WikiEventSource::Parse(response).success);
        Check(WikiEventSource::Parse(Fixture("<div id=\"fpevents\">Current Events\n====Tarkov====\n• Currently none\n====Arena====",article)).success);
        Check(!WikiEventSource::Parse(Fixture("<div id=\"fpevents\">Current Events\n====Tarkov====\nunknown\n====Arena====",article)).success);
        r=WikiEventSource::Parse(Fixture(current,article+"https://t.me/escapefromtarkovEN/60\n"));Check(!r.announcements.front().officialRecordId);
        auto linked=article;linked.insert(linked.find("==Unrelated=="),"https://t.me/escapefromtarkovEN/60\n");
        r=WikiEventSource::Parse(Fixture(current,linked));Check(r.success && r.announcements.front().officialRecordId=="60");
        linked.insert(linked.find("==Unrelated=="),"https://t.me/escapefromtarkovEN/61\n");
        Check(!WikiEventSource::Parse(Fixture(current,linked)).announcements.front().officialRecordId);
        auto seasonal=current;seasonal.insert(seasonal.find("\n|}"),"<br/>Only available in the [[Game_modes#Seasonal_PvP_game_mode|seasonal game mode]]");
        Check(WikiEventSource::Parse(Fixture(seasonal,article)).announcements.front().modes==std::vector<EventMode>{EventMode::Seasonal});
        std::cout<<"Wiki bounded identity/structure/current-state contract PASS\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
