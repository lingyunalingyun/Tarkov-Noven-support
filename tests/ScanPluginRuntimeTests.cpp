#include "plugins/PluginRuntimeManager.h"
#include "plugins/PluginPipe.h"
#include <iostream>
#include <stdexcept>
using namespace noven::plugins;
void Check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
struct Temp {std::filesystem::path path=std::filesystem::temp_directory_path()/("noven-scan-test-"+ipc::RandomSecret());Temp(){std::filesystem::create_directory(path);}~Temp(){std::error_code e;std::filesystem::remove_all(path,e);}};
int wmain(int argc,wchar_t** argv) try {
    Check(argc==10,"Host and eight first-party fixtures required");Temp temp;std::filesystem::copy_file(argv[1],temp.path/"NovenPluginHost.exe");
    PluginRuntimeManager runtime(temp.path);ipc::Handle changed(CreateEventW(nullptr,FALSE,FALSE,nullptr));runtime.SetChangeHandler([&]{SetEvent(changed.Get());});
    auto service=std::make_shared<CatalogPluginService>(std::span<const noven::data::ItemRecord>{},std::span<const noven::data::TaskRecord>{},std::span<const noven::data::MapRecord>{});runtime.SetCatalogService(service);
    noven::data::RecentScanEntry scan;scan.scanId=1;scan.scannedAtUnixMs=100;scan.stableItemId="exact-item";scan.canonicalName="Item";
    runtime.PublishRecentScans(std::vector{scan});runtime.NotifyScanCompleted(scan);
    const auto start=[&](std::string id,unsigned mode,bool permission=true){
        auto directory=temp.path/"plugins"/id;std::filesystem::create_directories(directory);std::filesystem::copy_file(argv[mode+2],directory/"fixture.dll");
        auto manifest=*ParseManifest("{\"manifestVersion\":2,\"id\":\""+id+"\",\"name\":\"Test\",\"version\":\"1.0.0\",\"apiVersion\":1,\"runtime\":{\"kind\":\"native-dll\",\"entry\":\"fixture.dll\"},\"permissions\":[\"ui.page.register\""+(permission?",\"scan.history.read\",\"scan.events.subscribe\"":"")+"]}").manifest;
        PluginRecord record;record.directory=directory;record.state=PluginState::Valid;record.manifest=manifest;PluginStateStore store;
        Check(!runtime.StartNative(record,store),"ungranted fails before any session");Check(store.Consent(manifest)&&runtime.StartNative(record,store),"explicit scoped consent");return id;
    };
    const auto wait=[&](std::string_view id,const auto& predicate){const auto deadline=ipc::After(6000);while(std::chrono::steady_clock::now()<deadline){auto state=runtime.Snapshot(id);if(state&&predicate(*state))return true;WaitForSingleObject(changed.Get(),50);}return false;};
    const auto action=[&](std::string_view id,std::string_view name){const auto state=runtime.Snapshot(id);Check(runtime.Action(id,state->generation,"dashboard",name),"action admission");};
    const auto barrier=[&](std::string_view id){const auto count=runtime.Snapshot(id)->pongs;Check(runtime.Ping(id)&&runtime.WaitForPong(id,count+1,5000),"session protocol barrier");};
    const auto a=start("com.example.scans-a",0),b=start("com.example.scans-b",0),denied=start("com.example.scans-denied",0,false);
    for(const auto& id:{a,b,denied})Check(runtime.WaitFor(id,HostState::Running,10000),"first-party fixture loads");
    action(denied,"subscribe");Check(wait(denied,[](const auto& s){return s.lastLog=="admission:-2";}),"not declared subscription denied");
    action(denied,"history");Check(wait(denied,[](const auto& s){return s.lastLog=="admission:-2";}),"not declared history denied");barrier(denied);
    runtime.NotifyScanCompleted(scan);barrier(a);Check(runtime.Snapshot(a)->scanEvents==0,"no event before subscription");
    action(a,"subscribe");Check(wait(a,[](const auto& s){return s.scanSubscribed&&s.lastLog=="subscription:1:0";}),"subscribe acknowledged");barrier(a);Check(runtime.Snapshot(a)->scanEvents==0,"no historical replay");
    action(a,"subscribe");Check(wait(a,[](const auto& s){return s.lastLog=="subscription:1:0";}),"duplicate subscription idempotent");barrier(a);
    runtime.NotifyScanCompleted(scan);Check(wait(a,[](const auto& s){return s.scanEvents==1;}),"one safe completion event");Check(runtime.Snapshot(b)->scanEvents==0,"other unsubscribed session receives no event");
    action(b,"subscribe");Check(wait(b,[](const auto& s){return s.scanSubscribed&&s.lastLog=="subscription:1:0";}),"independent session subscribes");
    action(a,"unsubscribe");Check(wait(a,[](const auto& s){return !s.scanSubscribed&&s.lastLog=="subscription:0:0";}),"unsubscribe acknowledged");
    runtime.NotifyScanCompleted(scan);Check(wait(b,[](const auto& s){return s.scanEvents==1;}),"neighbor still receives");barrier(a);Check(runtime.Snapshot(a)->scanEvents==1,"no event after unsubscribe");
    action(a,"history");Check(wait(a,[](const auto& s){return s.dataResults==1&&s.lastLog=="history:0";}),"history through existing async data pipeline");
    const auto hung=start("com.example.scans-hung",1);Check(runtime.WaitFor(hung,HostState::Running,10000),"hung fixture loads");
    action(hung,"subscribe");Check(wait(hung,[](const auto& s){return s.scanSubscribed&&s.lastLog=="subscription:1:0";}),"hung fixture subscribed");barrier(hung);
    const auto time=std::chrono::steady_clock::now();for(unsigned i=0;i<200;++i){scan.scanId=i+2;runtime.NotifyScanCompleted(scan);}
    Check(std::chrono::steady_clock::now()-time<std::chrono::seconds(2),"producer never waits for hung callback");
    const auto queued=runtime.Snapshot(hung);Check(queued->pendingScans<=32&&queued->droppedScans>0,"runtime queue bound and overflow accounting");
    Check(runtime.WaitForTerminal(hung,7000)&&!runtime.Snapshot(hung)->scanSubscribed&&runtime.Snapshot(hung)->pendingScans==0,"event callback timeout clears subscription and queue");
    Check(runtime.Snapshot(b)->state==HostState::Running,"hung session cannot corrupt neighbor");barrier(b);
    const auto crashed=start("com.example.scans-crash",2);Check(runtime.WaitFor(crashed,HostState::Running,10000),"crash fixture loads");
    action(crashed,"subscribe");Check(wait(crashed,[](const auto& s){return s.scanSubscribed&&s.lastLog=="subscription:1:0";}),"crash fixture subscribes");
    runtime.NotifyScanCompleted(scan);Check(runtime.WaitForTerminal(crashed,7000)&&!runtime.Snapshot(crashed)->scanSubscribed,"crash removes subscription");barrier(b);
    const auto subHang=start("com.example.scans-subhang",7);Check(runtime.WaitFor(subHang,HostState::Running,10000),"subscription hang fixture loads");action(subHang,"subscribe");Check(runtime.WaitForTerminal(subHang,7000),"subscription callback timeout bounded");
    for(unsigned mode=3;mode<=6;++mode){auto id=start("com.example.scans-abi-"+std::to_string(mode),mode);Check(runtime.WaitForTerminal(id,10000)&&runtime.Snapshot(id)->state!=HostState::Running,"invalid optional scan extension rejected");}
    for(const auto& id:{a,b,denied}){Check(runtime.Stop(id),"stop admitted");Check(!runtime.Snapshot(id)->scanSubscribed&&runtime.Snapshot(id)->pendingScans==0,"Disable immediately clears subscription");Check(runtime.WaitForTerminal(id,7000),"bounded clean cleanup");}
    const auto delivered=runtime.Snapshot(b)->scanEvents;runtime.NotifyScanCompleted(scan);Check(runtime.Snapshot(b)->scanEvents==delivered,"late completion discarded after teardown");
    Check(GetModuleHandleW(L"fixture.dll")==nullptr,"owner never loads plugin DLL");std::cout<<"Scan runtime/isolation/backpressure/ABI PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
