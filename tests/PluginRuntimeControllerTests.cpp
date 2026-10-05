#include "plugins/PluginRuntimeController.h"
#include "plugins/PluginPipe.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace noven::plugins;
void Check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
struct Temp {std::filesystem::path path=std::filesystem::temp_directory_path()/("noven-consent-"+ipc::RandomSecret());Temp(){std::filesystem::create_directory(path);}~Temp(){std::error_code error;std::filesystem::remove_all(path,error);}};
void Manifest(const std::filesystem::path& directory,int version=2,std::string permissions="[]"){
    std::ofstream file(directory/"manifest.json",std::ios::binary|std::ios::trunc);
    file<<"{\"manifestVersion\":"<<version<<",\"id\":\"com.example.consent\",\"name\":\"Consent\",\"version\":\"1.0.0\",\"apiVersion\":1,\"runtime\":{\"kind\":\"native-dll\",\"entry\":\"plugin.dll\"},\"permissions\":"<<permissions<<"}";
}
int wmain(int argc,wchar_t** argv) try {
    Check(argc==4,"host and two permission fixtures required");Temp temp;
    std::filesystem::copy_file(argv[1],temp.path/"NovenPluginHost.exe");
    const auto directory=temp.path/"plugins"/"com.example.consent",path=temp.path/"data"/"plugin-state.json";
    std::filesystem::create_directories(directory);std::filesystem::copy_file(argv[3],directory/"plugin.dll");Manifest(directory);
    PluginDiscovery discovery(temp.path);discovery.Refresh();
    {
        PluginRuntimeManager runtime(temp.path);PluginRuntimeController controller(discovery,runtime,path);
        controller.StartEnabled();controller.Refresh();Check(runtime.SessionCount()==0,"discovery/startup/refresh cannot enable a new plugin");
        Check(controller.Enable("com.example.consent",[](const auto&){return false;})==ControlResult::ConsentDeclined&&runtime.SessionCount()==0,"explicit denial executes nothing");
        std::filesystem::rename(directory/"plugin.dll",directory/"missing.dll");
        Check(controller.Enable("com.example.consent",[](const auto&){return true;})==ControlResult::Rejected&&runtime.SessionCount()==0,"missing DLL is diagnostic, not an exception or execution");
        std::filesystem::rename(directory/"missing.dll",directory/"plugin.dll");
        Manifest(directory,1);bool asked=false;Check(controller.Enable("com.example.consent",[&](const auto&){asked=true;return true;})==ControlResult::Rejected&&!asked,"V1 unknown runtime never executes");
        Manifest(directory,2,"[\"network.http\"]");Check(controller.Enable("com.example.consent",[](const auto&){return true;})==ControlResult::Rejected,"unsupported permission blocks consent");
        Manifest(directory);Check(controller.Enable("com.example.consent",[](const auto&){return true;})==ControlResult::Success&&runtime.WaitFor("com.example.consent",HostState::Running,10000),"approval enables after persisted consent");
        Check(PluginStateStore::Load(path).Intent("com.example.consent").enabled,"intent persisted");
        Manifest(directory,2,"[\"ui.page.register\"]");controller.Refresh();Check(!controller.State().Intent("com.example.consent").enabled&&runtime.WaitForTerminal("com.example.consent",6000),"expanded permissions revoke consent and stop");
        const auto count=runtime.SessionCount();controller.Refresh();Check(runtime.SessionCount()==count&&Terminal(runtime.Snapshot("com.example.consent")->state),"refresh never restarts");
        std::filesystem::copy_file(argv[2],directory/"plugin.dll",std::filesystem::copy_options::overwrite_existing);
        Check(controller.Enable("com.example.consent",[](const auto&){return true;})==ControlResult::Success&&runtime.WaitFor("com.example.consent",HostState::Running,10000),"explicit re-consent");
    }
    {
        PluginRuntimeManager runtime(temp.path);PluginRuntimeController controller(discovery,runtime,path);
        controller.StartEnabled();Check(runtime.WaitFor("com.example.consent",HostState::Running,10000),"saved approved intent starts at next launch");
        controller.StartEnabled();Check(runtime.SessionCount()==1,"startup only once");
        ipc::Handle process(OpenProcess(PROCESS_TERMINATE,FALSE,runtime.Snapshot("com.example.consent")->processId));
        Check(process&&TerminateProcess(process.Get(),19)&&runtime.WaitForTerminal("com.example.consent",6000)&&controller.Reconcile(),"crash isolated and reconciled");
        Check(!PluginStateStore::Load(path).Intent("com.example.consent").enabled,"crashed plugin disabled persistently");controller.Refresh();Check(runtime.SessionCount()==1,"no crash restart loop");
        Check(controller.Enable("com.example.consent",[](const auto&){return true;})==ControlResult::Success&&runtime.WaitFor("com.example.consent",HostState::Running,10000),"manual restart after crash");
        Check(controller.Disable("com.example.consent")==ControlResult::Success&&runtime.WaitForTerminal("com.example.consent",6000)&&!PluginStateStore::Load(path).Intent("com.example.consent").enabled,"disable stops and persists");
    }
    {
        std::ofstream file(path,std::ios::trunc);file<<"broken";
    }
    {
        PluginRuntimeManager runtime(temp.path);PluginRuntimeController controller(discovery,runtime,path);controller.StartEnabled();Check(runtime.SessionCount()==0,"corrupt state fails closed");
        std::filesystem::create_directory(temp.path/"blocked");PluginRuntimeController failed(discovery,runtime,temp.path/"blocked");
        Check(failed.Enable("com.example.consent",[](const auto&){return true;})==ControlResult::StateFailure&&runtime.SessionCount()==0,"save failure cannot execute");
        Check(failed.Disable("com.example.consent")==ControlResult::StateFailure&&!failed.Reconcile(),"disable persistence failure remains visible until repaired");
    }
    Check(GetModuleHandleW(L"plugin.dll")==nullptr,"owner never loads DLL");
    std::cout<<"Plugin consent/startup/refresh/crash state PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
