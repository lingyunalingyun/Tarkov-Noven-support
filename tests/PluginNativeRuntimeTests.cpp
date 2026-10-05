#include "plugins/PluginRuntimeManager.h"
#include "plugins/PluginPipe.h"
#include <iostream>
#include <stdexcept>
using namespace noven::plugins;
void Check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
struct Temp {std::filesystem::path path=std::filesystem::temp_directory_path()/("noven-native-runtime-"+ipc::RandomSecret());Temp(){std::filesystem::create_directory(path);}~Temp(){std::error_code error;std::filesystem::remove_all(path,error);}};
PluginRecord Record(const std::filesystem::path& root,std::string id){
    PluginRecord record;record.directory=root/"plugins"/id;record.state=PluginState::Valid;record.manifest=PluginManifest{};
    auto& manifest=*record.manifest;manifest.manifestVersion=2;manifest.apiVersion=1;manifest.id=std::move(id);manifest.runtime=NativeRuntime{"native-dll","plugin.dll"};manifest.requestedPermissions={"ui.page.register"};return record;
}
int wmain(int argc,wchar_t** argv) try {
    Check(argc==17,"host and fifteen fixtures required");
    Temp temp;std::filesystem::copy_file(argv[1],temp.path/"NovenPluginHost.exe");
    auto one=Record(temp.path,"com.example.one"),two=Record(temp.path,"dev.example.two");
    for(const auto* record:{&one,&two}){std::filesystem::create_directories(record->directory);std::filesystem::copy_file(argv[2],record->directory/"plugin.dll");}
    PluginStateStore grants;ipc::Handle changed(CreateEventW(nullptr,FALSE,FALSE,nullptr));PluginRuntimeManager manager(temp.path);
    manager.SetChangeHandler([&]{SetEvent(changed.Get());});
    Check(!manager.StartNative(one,grants)&&manager.SessionCount()==0,"discovery without consent cannot start native code");
    Check(grants.Consent(*one.manifest)&&grants.Consent(*two.manifest),"explicit first-party consent");
    Check(manager.StartNative(one,grants)&&manager.StartNative(two,grants),"independent authorized sessions");
    for(const auto* record:{&one,&two})Check(manager.WaitFor(record->manifest->id,HostState::Running,10000),"native init reaches Running");
    const auto running=manager.Snapshot(one.manifest->id);Check(running->pages.size()==1&&running->pages[0].localId=="dashboard"&&running->pages[0].document.HasAction("refresh"),"bounded owned UI snapshot");
    Check(GetModuleHandleW(L"plugin.dll")==nullptr,"owner never loads plugin DLL");
    Check(!manager.Action(one.manifest->id,running->generation+1,"dashboard","refresh")&&!manager.Action(one.manifest->id,running->generation,"builtin.plugins","refresh")&&!manager.Action(one.manifest->id,running->generation,"dashboard","unknown"),"generation namespace and current document checks");
    Check(manager.Action(one.manifest->id,running->generation,"dashboard","refresh"),"authorized button action");
    const auto deadline=ipc::After(5000);bool updated=false;
    while(std::chrono::steady_clock::now()<deadline){
        const auto snapshot=manager.Snapshot(one.manifest->id);
        if(snapshot->pages.size()==1&&snapshot->pages[0].document.blocks[0].text=="Clicked"){updated=true;break;}
        WaitForSingleObject(changed.Get(),100);
    }
    Check(updated&&manager.Snapshot(two.manifest->id)->pages[0].document.blocks[0].text=="Fixture","correct plugin receives action, independent plugin does not");
    Check(manager.Stop(one.manifest->id)&&!manager.Action(one.manifest->id,running->generation,"dashboard","refresh")&&manager.Snapshot(one.manifest->id)->pages.empty(),"stop invalidates page/action immediately");
    Check(manager.WaitForTerminal(one.manifest->id,6000),"bounded clean shutdown");
    Check(manager.StartNative(one,grants)&&manager.WaitFor(one.manifest->id,HostState::Running,10000),"explicit re-enable after terminal session");
    Check(!manager.Action(one.manifest->id,running->generation,"dashboard","refresh"),"previous-session page cannot target replacement host");
    ipc::Handle child(OpenProcess(PROCESS_TERMINATE|SYNCHRONIZE,FALSE,manager.Snapshot(one.manifest->id)->processId));
    Check(child&&TerminateProcess(child.Get(),19)&&manager.WaitForTerminal(one.manifest->id,6000),"host crash isolated");
    Check(manager.Snapshot(one.manifest->id)->pages.empty()&&manager.Ping(two.manifest->id)&&manager.WaitForPong(two.manifest->id,1,5000),"crash removes pages without corrupting another session");
    grants.Disable(one.manifest->id);Check(!manager.StartNative(one,grants),"disabled/crashed intent does not restart implicitly");
    auto unsupported=one;unsupported.manifest->requestedPermissions.push_back("network.http");Check(!manager.StartNative(unsupported,grants),"unsupported/new permissions fail closed");
    auto v1=one;v1.manifest->manifestVersion=1;Check(!manager.StartNative(v1,grants),"V1 never enters native runtime");
    Check(manager.Stop(two.manifest->id)&&manager.WaitForTerminal(two.manifest->id,6000),"neighbor shutdown");
    for(int mode:{1,2,3,4,5,6,7,8,9,10,12}){
        auto record=Record(temp.path,"com.example.mode"+std::to_string(mode));std::filesystem::create_directories(record.directory);std::filesystem::copy_file(argv[mode+2],record.directory/"plugin.dll");
        Check(grants.Consent(*record.manifest)&&manager.StartNative(record,grants),"controlled ABI fixture start");
        if(mode==7||mode==9||mode==10){
            Check(manager.WaitFor(record.manifest->id,HostState::Running,10000),"fault fixture initialized");
            if(mode==7)manager.Stop(record.manifest->id);
            else Check(manager.Action(record.manifest->id,manager.Snapshot(record.manifest->id)->generation,"dashboard","refresh"),"fault callback invoked");
        }
        Check(manager.WaitForTerminal(record.manifest->id,10000)&&manager.Snapshot(record.manifest->id)->pages.empty(),"ABI failure/crash/hang bounded and pages removed");
        const auto snapshot=manager.Snapshot(record.manifest->id);
        if(mode==7)Check(snapshot->error==HostError::ShutdownTimeout,"shutdown hang diagnostic");
        if(mode==8)Check(snapshot->error==HostError::LoadTimeout,"init hang diagnostic");
        if(mode==9)Check(snapshot->error==HostError::ActionTimeout,"action hang diagnostic");
    }
    std::cout<<"Native runtime consent/actions/fault isolation PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
