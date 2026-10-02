#include "raid/LocalRaidService.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <atomic>
using namespace noven::raid;
void Require(bool ok,const char* text) {if(!ok) {std::cerr<<text<<'\n';std::exit(1);}}
void Write(const std::filesystem::path& path,std::string_view text,bool append=false) {
    std::ofstream out(path,std::ios::binary|(append?std::ios::app:std::ios::trunc));out<<text;
}
template<class F>void Wait(LocalRaidService& service,F predicate) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(!predicate()) {
        const auto status=service.Status();Require(status.error.empty(),status.error.c_str());
        Require(std::chrono::steady_clock::now()<deadline,"service timeout");std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
int main(int argc,char** argv) {
    const auto root=std::filesystem::temp_directory_path()/("noven-service-"+std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(root);const auto history=root/"data"/"raid-history.json";
    if(argc==3&&std::string_view(argv[1])=="--replay-root") {
        // 本机历史日志仅只读回放；临时目录仅存结构化结果，输出只含计数。
        // Read-only replay of local historical logs; temp storage is structured only, output contains counts.
        LocalRaidService service;Require(service.Start(std::filesystem::path(std::u8string(argv[2],argv[2]+std::char_traits<char>::length(argv[2]))),history),"start replay");
        Wait(service,[&]{return service.Status().directoryPasses>0;});
        const auto sessions=service.CompletedSessions();const auto stats=service.Status();service.Stop();
        std::size_t scav{},pve{},unknown{};for(const auto& s:sessions) {scav+=s.raidType==RaidType::Scav;pve+=s.gameMode==GameMode::PvE;unknown+=s.outcome==RaidOutcome::Unknown;}
        std::cout<<"Historical replay: completed="<<sessions.size()<<" pve="<<pve<<" scav="<<scav
            <<" unknownOutcome="<<unknown<<" bytes="<<stats.bytesRead<<" events="<<stats.events<<'\n';
        std::filesystem::remove_all(root);return 0;
    }
    const auto logs=root/"Logs";const auto group=logs/"log_2026.01.01";std::filesystem::create_directories(group);
    const auto app=group/"synthetic application_000.log",backend=group/"synthetic backend_000.log";
    Write(app,"2026-01-01 12:00:00.000|Session mode: Pve\n"
        "2026-01-01 12:01:00.000|[Transit] RaidId:one, Locations:RezervBase -> \n"
        "2026-01-01 12:02:00.000|GameStarted\n"
        "2026-01-01 12:10:00.000|[Transit] RaidId:two, Locations:RezervBase -> \n"
        "2026-01-01 12:11:00.000|GameStarted\n");
    Write(backend,"2026-01-01 12:03:00.000|/client/match/local/end\n"
        "2026-01-01 12:03:01.000|/client/match/local/end\n"
        "2026-01-01 12:04:00.000|FinishScavSession\n"
        "2026-01-01 12:12:00.000|/client/match/local/end\n");
    Write(group/"synthetic output_000.log",std::string(1024*1024,'x'));
    LocalRaidService service;std::atomic<unsigned> notifications{};
    service.SetChangedCallback([&]{++notifications;});
    Require(service.Start(logs,history),"start service");
    Wait(service,[&]{return service.CompletedSessions().size()==2;});
    auto completed=service.CompletedSessions();const auto first=completed[0].localSessionId;
    Require(completed[0].eftRaidId=="one"&&completed[0].raidType==RaidType::Scav&&completed[1].eftRaidId=="two"
        &&completed[1].raidType==RaidType::Unknown,"application/backend timestamp merge");
    const auto idle=service.Status();std::this_thread::sleep_for(std::chrono::milliseconds(750));
    std::cout<<"idle passes "<<idle.directoryPasses<<" -> "<<service.Status().directoryPasses
        <<", bytes "<<idle.bytesRead<<" -> "<<service.Status().bytesRead<<'\n';
    Require(service.Status().directoryPasses==idle.directoryPasses&&service.Status().bytesRead==idle.bytesRead,"idle does not poll/read");
    const auto before=notifications.load();
    Write(app,"2026-01-01 12:19:00.000|unrelated synthetic diagnostic\n",true);
    Wait(service,[&]{return service.Status().bytesRead>idle.bytesRead;});
    Require(notifications==before,"irrelevant appends do not rebuild UI history");
    Write(app,"2026-01-01 12:20:00.000|[Transit] RaidId:three, Locations:RezervBase -> \n"
        "2026-01-01 12:21:00.000|GameSta",true);
    Wait(service,[&]{return service.Status().bytesRead>idle.bytesRead;});Require(!service.ActiveSession(),"partial start not active");
    service.Stop();Require(service.Start(logs,history),"restart");Wait(service,[&]{return service.Status().directoryPasses>0;});
    Require(service.CompletedSessions().size()==2&&service.CompletedSessions()[0].localSessionId==first,"restart stable sessions");
    Write(app,"rted\n",true);Wait(service,[&]{return service.ActiveSession().has_value();});
    const auto activeId=service.ActiveSession()->localSessionId;
    service.Stop();Require(service.Start(logs,history),"active restart");Wait(service,[&]{return service.ActiveSession().has_value();});
    Require(service.ActiveSession()->localSessionId==activeId,"unfinished session survives restart");
    Write(backend,"2026-01-01 12:22:00.000|/client/match/local/end\n",true);
    Wait(service,[&]{return service.CompletedSessions().size()==3;});
    const auto next=logs/"log_2026.01.02";std::filesystem::create_directories(next);
    Write(next/"synthetic application_000.log","2026-01-02 12:00:00.000|GameStarted\n");
    Write(next/"synthetic backend_000.log","2026-01-02 12:01:00.000|/client/match/local/end\n");
    Wait(service,[&]{return service.CompletedSessions().size()==4;});
    Require(service.CompletedSessions().back().gameMode==GameMode::Unknown,"rotation does not carry old session mode");
    Require(service.FindSession(first).has_value(),"query by local identity");service.Stop();
    Require(service.Start({},history),"history browsing without configured log root");
    Wait(service,[&]{return !service.Status().running;});
    Require(service.CompletedSessions().size()==4&&!service.ActiveSession(),"saved history loads without inventing active raid");
    service.Stop();
    const auto config=root/"eft-log-root.txt";const auto path=logs.u8string();
    const auto forbidden=logs/"private-output"/"raid-history.json";
    Require(service.Start(logs,forbidden),"start invalid storage boundary check");
    Wait(service,[&]{return !service.Status().error.empty();});service.Stop();
    Require(!std::filesystem::exists(forbidden.parent_path()),"reject storage under EFT logs before creating any files");
    Write(config,std::string(path.begin(),path.end())+"\n");Require(ReadEftLogRoot(config)==logs,"explicit path config");
    Write(config,"relative/Logs");Require(!ReadEftLogRoot(config),"no implicit disk scanning");
    std::ifstream in(history);const std::string persisted((std::istreambuf_iterator<char>(in)),{});in.close();
    Require(persisted.find("GameStarted")==std::string::npos&&persisted.find("Session mode:")==std::string::npos,"no raw logs persisted");
    std::filesystem::remove_all(root);std::cout<<"Local raid service synthetic integration PASS\n";
}
