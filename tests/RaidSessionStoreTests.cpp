#include "raid/RaidSessionStore.h"
#include "raid/RaidJson.h"
#include <fstream>
#include <iostream>
#include <cstdlib>
using namespace noven::raid;
void Require(bool ok,const char* text) {if(!ok) {std::cerr<<text<<'\n';std::exit(1);}}
void Feed(RaidSessionDetector& d,const char* text) {for(const auto& e:ParseRaidEvents(text,"synthetic",10))d.Consume(e);}
int main() {
    const auto root=std::filesystem::temp_directory_path()/("noven-store-"+std::to_string(GetCurrentProcessId()));
    const auto file=root/"raid-history.json";std::filesystem::create_directories(root);std::string error;
    RaidCheckpoint cp;RaidSessionDetector d;
    Feed(d,"[Transit] RaidId:synthetic-one, Locations:RezervBase -> ");Feed(d,"GameStarted");Feed(d,"/client/match/local/end");
    cp.detector=d.Snapshot();cp.cursors.push_back({root/L"中文.log","123-456",100,0});cp.sourceGroup=root;
    const auto id=d.CompletedSessions()[0].localSessionId;
    {
        RaidSessionStore store;RaidCheckpoint empty;Require(store.Load(file,empty,error)&&empty.detector.completed.empty(),"missing empty");
        Require(store.Save(cp,error),"save complete checkpoint");
        RaidSessionStore competing;Require(!competing.Load(file,empty,error),"single writer lease");
    }
    {
        RaidSessionStore store;RaidCheckpoint read;Require(store.Load(file,read,error),"load");
        Require(read.detector.completed[0].localSessionId==id&&read.cursors[0].path==cp.cursors[0].path,"durable IDs/unicode cursors");
        RaidSessionDetector replay;replay.Restore(read.detector);Feed(replay,"[Transit] RaidId:synthetic-one, Locations:RezervBase -> ");
        Feed(replay,"GameStarted");Feed(replay,"/client/match/local/end");Require(replay.CompletedSessions().size()==1,"restart dedup");
        Feed(replay,"FinishScavSession");cp.detector=replay.Snapshot();Require(store.Save(cp,error),"atomic late role update");
        cp.detector.completed.push_back(cp.detector.completed[0]);Require(!store.Save(cp,error),"reject duplicate before overwrite");
    }
    {RaidSessionStore store;RaidCheckpoint read;Require(store.Load(file,read,error)&&read.detector.completed.size()==1
        &&read.detector.completed[0].raidType==RaidType::Scav,"failed write preserved valid old data");}
    // 改变真实存储的顶层键顺序，验证 schema 不依赖序列化顺序。
    // Reorder actual storage top-level keys to prove schema parsing is order-independent.
    {std::ifstream input(file);std::string text((std::istreambuf_iterator<char>(input)),{});input.close();
        Require(text.starts_with("{\"schemaVersion\":1,"),"schema prefix");
        text.erase(1,18);text.pop_back();text+=",\"schemaVersion\":1}";
        std::ofstream output(file);output<<text;}
    {RaidSessionStore store;RaidCheckpoint read;Require(store.Load(file,read,error),"reordered schema loads");}
    const auto parsed=json::Parser("{\"z\":1,\"a\":\"\\u4e2d\\ud83d\\ude00\"}").Parse();
    Require(parsed.At("a").String()=="中😀","unicode escape and surrogate");
    for(const auto* bad:{"{\"a\":1,\"a\":2}","{\"a\":01}","{\"a\":\"\\ud800\"}","{\"a\":1} trailing"}) {
        bool rejected=false;try {json::Parser(bad).Parse();}catch(...) {rejected=true;}Require(rejected,"invalid JSON rejected");}
    {std::ofstream out(file);out<<"{\"schemaVersion\":99}";}
    {RaidSessionStore store;RaidCheckpoint read;Require(!store.Load(file,read,error)&&!store.Save(read,error),"corruption cannot overwrite history");}
    std::ifstream in(file);std::string raw((std::istreambuf_iterator<char>(in)),{});in.close();
    Require(raw=="{\"schemaVersion\":99}","corrupt bytes left intact");
    std::filesystem::remove_all(root);std::cout<<"Raid store contracts PASS\n";
}
