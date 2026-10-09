#include "plugins/PluginRuntimeManager.h"
#include "plugins/PluginPipe.h"
#include <iostream>
#include <stdexcept>
using namespace noven::plugins;
void Check(bool ok,const char* label){if(!ok)throw std::runtime_error(label);}
struct Temp {std::filesystem::path path=std::filesystem::temp_directory_path()/("noven-http-life-"+ipc::RandomSecret());Temp(){std::filesystem::create_directory(path);}~Temp(){std::error_code error;std::filesystem::remove_all(path,error);}};
int wmain(int argc,wchar_t** argv) try {
    Check(argc==9,"offline Host and seven fixtures required");Temp temp;std::filesystem::copy_file(argv[1],temp.path/"NovenPluginHost.exe");
    PluginRuntimeManager runtime(temp.path);PluginStateStore grants;ipc::Handle changed(CreateEventW(nullptr,FALSE,FALSE,nullptr));runtime.SetChangeHandler([&]{SetEvent(changed.Get());});
    const auto record=[&](int mode,std::string id){PluginRecord r;r.state=PluginState::Valid;r.directory=temp.path/"plugins"/id;std::filesystem::create_directories(r.directory);std::filesystem::copy_file(argv[mode+2],r.directory/"plugin.dll");r.manifest=PluginManifest{};auto& m=*r.manifest;m.id=id;m.manifestVersion=2;m.apiVersion=1;m.runtime=NativeRuntime{"native-dll","plugin.dll"};m.requestedPermissions={"ui.page.register","network.http"};m.networkOrigins={"https://api.example.com"};return r;};
    auto neighbor=record(0,"com.example.neighbor");Check(grants.Consent(*neighbor.manifest)&&runtime.StartNative(neighbor,grants)&&runtime.WaitFor(neighbor.manifest->id,HostState::Running,10000),"healthy independent session");
    for(int mode=0;mode<=6;++mode){
        auto r=record(mode,"com.example.mode"+std::to_string(mode));const auto id=r.manifest->id;
        if(mode==0){r.manifest->requestedPermissions={"ui.page.register"};r.manifest->networkOrigins.clear();}
        Check(!runtime.StartNative(r,grants),"declared but ungranted cannot execute");
        Check(grants.Consent(*r.manifest)&&runtime.StartNative(r,grants),"first-party explicit grants");
        if(mode>=4){Check(runtime.WaitForTerminal(id,10000)&&runtime.Snapshot(id)->loadResult==5,"invalid optional HTTP table rejected");continue;}
        Check(runtime.WaitFor(id,HostState::Running,10000),"valid fixture Running");
        Check(runtime.Action(id,runtime.Snapshot(id)->generation,"dashboard","get"),"scoped action");
        if(mode==0||mode==3){
            bool observed=false;const auto deadline=ipc::After(5000);while(std::chrono::steady_clock::now()<deadline){const auto state=runtime.Snapshot(id);if(!state->pages.empty()&&state->pages[0].document.blocks[0].text==(mode==0?"Admission: -2":"Admission: 0")){observed=true;break;}WaitForSingleObject(changed.Get(),50);}Check(observed,"missing declaration denied / pending admitted");
            Check(runtime.Stop(id)&&runtime.WaitForTerminal(id,7000)&&runtime.Snapshot(id)->shutdownAcknowledged,"Disable cancels pending HTTP without waiting for twenty-second deadline");
        }else{
            Check(runtime.WaitForTerminal(id,8000),"HTTP callback crash/hang bounded");
            if(mode==2)Check(runtime.Snapshot(id)->error==HostError::DataTimeout,"three-second HTTP callback watchdog");
        }
        Check(runtime.Snapshot(id)->pages.empty(),"terminal drops pages and late delivery");
        Check(runtime.Ping(neighbor.manifest->id)&&runtime.WaitForPong(neighbor.manifest->id,static_cast<unsigned>(mode+1),5000),"another Host stays healthy");
    }
    Check(runtime.Stop(neighbor.manifest->id)&&runtime.WaitForTerminal(neighbor.manifest->id,7000)&&GetModuleHandleW(L"plugin.dll")==nullptr,"owned shutdown, no owner DLL load");
    std::cout<<"HTTP permissions/optional ABI/crash/hang/pending Disable/session isolation PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
