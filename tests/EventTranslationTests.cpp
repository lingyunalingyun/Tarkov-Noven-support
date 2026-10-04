#include "events/EventTranslation.h"
#include "raid/RaidJson.h"
#include <iostream>
#include <fstream>
#include <stdexcept>

using namespace noven::events;
void Check(bool value){if(!value)throw std::runtime_error("translation assertion failed");}
struct Http final:IEventHttp {
    int calls{};bool fail{};
    HttpResponse Get(std::wstring_view host,std::wstring_view path,std::string_view={},std::string_view={}) override {
        Check(host==L"api.mymemory.translated.net"&&path.ends_with(L"&langpair=en%7Czh-CN"));++calls;
        if(fail)return {200,"application/json","{\"responseStatus\":\"403\",\"quotaFinished\":true}"};
        return {200,"application/json; charset=utf-8","{\"responseStatus\":200,\"quotaFinished\":false,\"responseData\":{\"translatedText\":\"Glukhar 在 Reserve\",\"match\":0.85},\"ignored\":1.2e-3}"};
    }
};
int main(int argc,char** argv){try {
    if(argc==2&&std::string_view(argv[1])=="--live") {
        WinHttpEventClient http;std::string text,error;
        const bool ok=EventTranslation::Parse(http.Get(L"api.mymemory.translated.net",L"/get?q=An%20in-game%20event%20has%20started.&langpair=en%7Czh-CN"),text,error);
        std::cout<<(ok?text:error)<<'\n';return ok?0:1;
    }
    const auto dir=std::filesystem::temp_directory_path()/L"noven-translation-tests"/std::to_wstring(GetCurrentProcessId());
    std::filesystem::create_directories(dir);Http http;std::string error;
    {
        EventTranslation translator(http);Check(translator.Load(dir/L"translations.json",error));
        std::string unicode;for(int i=0;i<400;++i)unicode+="中";
        const auto parts=EventTranslation::Split(unicode);std::string joined;
        for(const auto& part:parts){Check(part.size()<=500);Check(noven::raid::json::Parser(noven::raid::json::Quote(part)).Parse().String()==part);joined+=part;}
        Check(joined==unicode);Check(EventTranslation::Split(std::string("\xff")).empty());
        EventRecord event;event.eventId="official-telegram:1";event.title="Event";event.summary=std::string(1100,'a');
        Check(translator.Refresh({event},{},error));Check(http.calls==4);
        std::vector<EventRecord> records{event};translator.Apply(records);
        Check(records[0].title==event.title&&records[0].summary==event.summary&&!records[0].startsAt);
        Check(records[0].machineText.at(event.title)=="格鲁哈 在 储备站");
        Check(translator.Refresh(records,{},error)&&http.calls==4);
        event.title="Changed event";http.fail=true;Check(!translator.Refresh({event},{},error));
        records={event};translator.Apply(records);Check(!records[0].machineText.contains(event.title)&&records[0].machineText.contains(event.summary));
    }
    {
        EventTranslation restart(http);Check(restart.Load(dir/L"translations.json",error));
        EventRecord event;event.title="Event";std::vector<EventRecord> records{event};restart.Apply(records);Check(records[0].machineText.contains("Event"));
    }
    {
        std::ofstream out(dir/L"bad.json");out<<"{}";out.close();EventTranslation corrupt(http);Check(!corrupt.Load(dir/L"bad.json",error));
        Check(!corrupt.Refresh({}, {},error));
    }
    std::string translated;
    Check(EventTranslation::Parse({200,"application/json","{\"responseStatus\":200,\"quotaFinished\":false,\"responseData\":{\"translatedText\":\"水处理厂；污水处理厂；拾荒者；玩家扫荡者\"}}"},translated,error));
    Check(translated=="污水处理厂；污水处理厂；Scav；玩家Scav");
    {
        std::ofstream out(dir/L"old-terms.json",std::ios::binary);
        out<<"{\"schemaVersion\":1,\"provider\":\"MyMemory\",\"locale\":\"zh-CN\",\"entries\":[{\"source\":\"Old text\",\"translated\":\"水处理厂的拾荒者\"}]}";out.close();
        EventTranslation cached(http);const auto calls=http.calls;Check(cached.Load(dir/L"old-terms.json",error));
        EventRecord event;event.title="Old text";std::vector<EventRecord> records{event};cached.Apply(records);
        Check(records[0].machineText.at(event.title)=="污水处理厂的Scav");
        Check(cached.Refresh(records,{},error)&&http.calls==calls);
        cached.Apply(records);Check(records[0].machineText.at(event.title)=="污水处理厂的Scav");
    }
    Check(!EventTranslation::Parse({200,"text/html","<html>error</html>"},translated,error));
    Check(!EventTranslation::Parse({200,"application/json",std::string(128*1024+1,'x')},translated,error));
    Check(!EventTranslation::Parse({200,"application/json","{}"},translated,error));
    bool strict=false;try{noven::raid::json::Parser("0.85").Parse();}catch(...){strict=true;}Check(strict);
    for(auto value:{"0.","1e+","01.2","1.2.3"}){bool reject=false;try{noven::raid::json::Parser(value,true).Parse();}catch(...){reject=true;}Check(reject);}
    std::filesystem::remove_all(dir);std::cout<<"Translation fixtures/cache/bounds PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
