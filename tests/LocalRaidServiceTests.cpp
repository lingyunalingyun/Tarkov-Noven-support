#include "raid/LocalRaidService.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <atomic>
#include <map>
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
    if((argc==3&&std::string_view(argv[1])=="--replay-root")
        ||(argc==4&&std::string_view(argv[1])=="--replay-checkpoint")) {
        // 本机历史日志仅只读回放；临时目录仅存结构化结果，输出只含计数。
        // Read-only replay of local historical logs; temp storage is structured only, output contains counts.
        RaidCheckpoint baseline;std::string baselineError;
        if(argc==4){std::filesystem::create_directories(history.parent_path());std::filesystem::copy_file(argv[3],history);
            RaidSessionStore store;Require(store.Load(history,baseline,baselineError),"read copied baseline checkpoint");}
        LocalRaidService service;Require(service.Start(std::filesystem::path(std::u8string(argv[2],argv[2]+std::char_traits<char>::length(argv[2]))),history),"start replay");
        Wait(service,[&]{return service.Status().directoryPasses>0;});
        const auto sessions=service.CompletedSessions();const auto stats=service.Status();service.Stop();
        for(const auto& old:baseline.detector.completed) {
            const auto found=std::find_if(sessions.begin(),sessions.end(),[&](const auto& s){return s.localSessionId==old.localSessionId;});
            Require(found!=sessions.end(),"recovery preserves every original local session identity");
            auto retained=*found;retained.mapId=old.mapId;
            Require(retained==old,"recovery preserves original session facts and scan-link identity");
        }
        if(argc==4)std::cout<<"Preserved original records="<<baseline.detector.completed.size()<<'\n';
        std::size_t scav{},pve{},unknown{};for(const auto& s:sessions) {scav+=s.raidType==RaidType::Scav;pve+=s.gameMode==GameMode::PvE;unknown+=s.outcome==RaidOutcome::Unknown;}
        std::cout<<"Historical replay: completed="<<sessions.size()<<" pve="<<pve<<" scav="<<scav
            <<" unknownOutcome="<<unknown<<" bytes="<<stats.bytesRead<<" events="<<stats.events<<'\n';
        std::map<std::string,std::size_t> maps;std::size_t partial{};
        for(const auto& s:sessions){++maps[s.mapId];partial+=!s.endObserved;}
        std::cout<<"Incomplete records="<<partial<<'\n';
        for(const auto& [map,count]:maps)std::cout<<"map="<<map<<" records="<<count<<'\n';
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
    const auto manualNotifications=notifications.load();
    Require(service.RequestScan(),"manual scan schedules native worker");
    Wait(service,[&]{return service.Status().manualScans==1&&!service.Status().manualScanPending&&notifications.load()>manualNotifications;});
    Require(service.Status().bytesRead>idle.bytesRead&&service.CompletedSessions().size()==2
        &&service.CompletedSessions()[0].localSessionId==first,"manual committed-prefix recovery deduplicates history");
    const auto manualBytes=service.Status().bytesRead;
    const auto before=notifications.load();
    Write(app,"2026-01-01 12:19:00.000|unrelated synthetic diagnostic\n",true);
    Wait(service,[&]{return service.Status().bytesRead>manualBytes;});
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
    const auto repairLogs=root/"repair-logs",repairHistory=root/"repair-data"/"raid-history.json";
    const auto oldGroup=repairLogs/"log_2025.12.30",newGroup=repairLogs/"log_2025.12.31";
    std::filesystem::create_directories(oldGroup);std::filesystem::create_directories(newGroup);
    const auto oldApp=oldGroup/"synthetic application_000.log";
    std::string appText,backendText;
    const char* locations[]{"Interchange","Shoreline","Woods","Interchange","UnverifiedMap"};
    for(int index=0;index<5;++index) {
        const auto minute=std::to_string(10+index),id="repair-"+std::to_string(index);
        appText+="2025-12-30 12:"+minute+":00.000|[Transit] RaidId:"+id+", Locations:"+locations[index]+" -> \n";
        if(index==3)appText+="2025-12-30 12:"+minute+":01.000|[Transit] RaidId:"+id+", Locations:Woods -> \n";
        appText+="2025-12-30 12:"+minute+":02.000|GameStarted\n";
        backendText+="2025-12-30 12:"+minute+":03.000|/client/match/local/end\n";
    }
    Write(oldApp,appText);Write(oldGroup/"synthetic backend_000.log",backendText);
    Write(newGroup/"synthetic application_000.log","2025-12-31 12:00:00.000|[Transit] RaidId:known, Locations:Terminal -> \n"
        "2025-12-31 12:00:01.000|GameStarted\n");
    Write(newGroup/"synthetic backend_000.log","2025-12-31 12:00:02.000|/client/match/local/end\n");
    Require(service.Start(repairLogs,repairHistory),"prepare legacy map history");
    Wait(service,[&]{return service.Status().directoryPasses>0;});service.Stop();
    RaidCheckpoint legacy;std::string error;
    {RaidSessionStore store;Require(store.Load(repairHistory,legacy,error),"load legacy fixture");
        Require(legacy.detector.completed.size()==6,"all map sessions captured");
        for(std::size_t index=0;index<5;++index)legacy.detector.completed[index].mapId.clear();
        legacy.detector.completed.back().mapId=NormalizeMap("RezervBase");
        Require(store.Save(legacy,error),"simulate old unknown-map cache");}
    auto expected=legacy.detector.completed;
    for(std::size_t index=0;index<3;++index)expected[index].mapId=NormalizeMap(locations[index]);
    Require(service.Start(repairLogs,repairHistory),"restart repairs historical source groups");
    Wait(service,[&]{return service.Status().directoryPasses>0;});
    Require(service.CompletedSessions()==expected,"repair only proven maps; preserve IDs, timestamps, known maps, ambiguous/unknown maps");
    Require(service.RequestScan(),"repeat historical map scan");
    Wait(service,[&]{return service.Status().manualScans==1;});
    Require(service.CompletedSessions()==expected,"map repair is idempotent without duplicate sessions");service.Stop();
    {RaidSessionStore store;RaidCheckpoint repaired;Require(store.Load(repairHistory,repaired,error),"repaired cache roundtrip");
        Require(repaired.detector.completed==expected&&repaired.sourceGroup==legacy.sourceGroup
            &&repaired.cursors.size()==legacy.cursors.size(),"preserve group and cursor count");
        for(std::size_t index=0;index<legacy.cursors.size();++index) {
            const auto& beforeCursor=legacy.cursors[index];const auto& afterCursor=repaired.cursors[index];
            Require(beforeCursor.path==afterCursor.path&&beforeCursor.fileIdentity==afterCursor.fileIdentity
                &&beforeCursor.generation==afterCursor.generation&&beforeCursor.offset==afterCursor.offset,"never rewind durable cursor");
        }
        repaired.detector.completed[0].mapId.clear();Require(store.Save(repaired,error),"prepare unavailable old source");}
    std::filesystem::rename(oldApp,oldApp.string()+".unavailable");
    Require(service.Start(repairLogs,repairHistory),"missing old source safe startup");
    Wait(service,[&]{return service.Status().directoryPasses>0;});
    Require(service.CompletedSessions()[0].mapId.empty(),"missing evidence remains unknown");
    std::filesystem::rename(oldApp.string()+".unavailable",oldApp);
    Require(service.RequestScan(),"manual recovery after old source restored");
    Wait(service,[&]{return service.Status().manualScans==1;});
    Require(service.CompletedSessions()==expected,"manual repair can revisit old groups without rebuilding history");service.Stop();
    const auto remoteLogs=root/"remote-logs",remoteHistory=root/"remote-data"/"raid-history.json";
    std::filesystem::create_directories(remoteLogs);
    Write(remoteLogs/"synthetic application_000.log",
        "2026-01-03 12:00:00.000|Session mode: Pve\n"
        "2026-01-03 12:00:01.000|scene preset path:maps/factory_day_preset.bundle\n"
        "2026-01-03 12:00:02.000|GameStarted\n"
        "2026-01-03 12:00:03.000|Info|application|BEClient exit successfully\n"
        "2026-01-03 12:01:01.000|scene preset path:maps/laboratory_preset.bundle\n"
        "2026-01-03 12:01:02.000|GameStarted\n"
        "2026-01-03 12:02:01.000|scene preset path:maps/shoreline_preset.bundle\n"
        "2026-01-03 12:02:02.000|GameStarted\n"
        "2026-01-03 12:02:03.000|Info|application|BEClient exit successfully\n");
    const auto sentinel=root/"unrelated-user-settings.txt";Write(sentinel,"keep-user-state");
    Require(service.Start(remoteLogs,remoteHistory),"read remote-session lifecycle");
    Wait(service,[&]{return service.Status().directoryPasses>0;});service.Stop();
    auto networkSessions=service.CompletedSessions();
    Require(networkSessions.size()==3,"teardown and successor retain every observed raid");
    for(const auto& s:networkSessions)Require(s.sourceInterrupted&&!s.endObserved&&!s.endedAt&&!s.duration
        &&s.outcome==RaidOutcome::Unknown,"incomplete is not fabricated settlement");
    RaidCheckpoint lost;
    {RaidSessionStore store;Require(store.Load(remoteHistory,lost,error),"load remote checkpoint");
        // 模拟旧版本只保留首局和最后一个 active，中间记录已被覆盖。
        // Simulate the old checkpoint: one retained record, overwritten middle raid, final active context.
        lost.detector.active=lost.detector.completed.back();lost.detector.active->sourceInterrupted=false;
        lost.detector.active->mapId.clear();lost.detector.state=SessionState::ActiveRaid;
        lost.detector.completed.resize(1);lost.detector.completed[0].mapId=NormalizeMap("RezervBase");
        Require(store.Save(lost,error),"seed legacy missing-raid checkpoint");}
    Require(service.Start(remoteLogs,remoteHistory),"recover committed historical sessions");
    Wait(service,[&]{return service.Status().directoryPasses>0;});
    auto recovered=service.CompletedSessions();
    Require(recovered.size()==3&&!service.ActiveSession(),"restore lost sessions including retired legacy active");
    Require(recovered[0]==lost.detector.completed[0]&&recovered[1].mapId==NormalizeMap("laboratory")
        &&recovered[2].mapId==NormalizeMap("Shoreline")
        &&recovered[2].localSessionId==lost.detector.active->localSessionId,"preserve old IDs/known records and recover proven maps");
    Require(service.RequestScan(),"repeat missing-history recovery");
    Wait(service,[&]{return service.Status().manualScans==1;});
    Require(service.CompletedSessions()==recovered,"recovery is idempotent and preserves scan-link identities");service.Stop();
    {RaidSessionStore store;RaidCheckpoint read;Require(store.Load(remoteHistory,read,error),"recovered history roundtrip");
        Require(read.detector.completed==recovered&&read.sourceGroup==lost.sourceGroup
            &&read.cursors.size()==lost.cursors.size(),"recovery persisted without moving source group");
        for(std::size_t i=0;i<lost.cursors.size();++i)Require(read.cursors[i].offset==lost.cursors[i].offset
            &&read.cursors[i].fileIdentity==lost.cursors[i].fileIdentity
            &&read.cursors[i].generation==lost.cursors[i].generation,"recovery never rewinds or changes cursor generation");}
    {std::ifstream sentinelInput(sentinel);std::string value;std::getline(sentinelInput,value);Require(value=="keep-user-state","recovery preserves unrelated user state");}
    Require(service.Start({},remoteHistory),"offline recovered history");Wait(service,[&]{return !service.Status().running;});
    Require(service.CompletedSessions()==recovered,"incomplete history usable without source logs");service.Stop();
    std::filesystem::remove_all(root);std::cout<<"Local raid service synthetic integration PASS\n";
}
