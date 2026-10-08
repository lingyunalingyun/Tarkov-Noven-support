#include "plugins/PluginRuntimeManager.h"
#include "plugins/PluginPipe.h"
#include <iostream>
#include <stdexcept>
using namespace noven::plugins;
void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
struct Temp {
    std::filesystem::path path=std::filesystem::temp_directory_path()/("noven-catalog-fault-"+ipc::RandomSecret());
    Temp(){std::filesystem::create_directory(path);}
    ~Temp(){std::error_code error;std::filesystem::remove_all(path,error);}
};
int wmain(int argc,wchar_t** argv) try {
    Check(argc==12,"Host and ten catalog fixtures");Temp temp;
    std::filesystem::copy_file(argv[1],temp.path/"NovenPluginHost.exe");
    noven::data::ItemRecord item;item.id="item_1";
    noven::data::TaskRecord task;task.id="task_1";task.mode="regular";
    noven::data::MapRecord map;map.id="interchange";
    std::vector items{item};std::vector tasks{task};std::vector maps{map};
    PluginRuntimeManager runtime(temp.path);runtime.SetCatalogService(std::make_shared<CatalogPluginService>(items,tasks,maps));
    ipc::Handle changed(CreateEventW(nullptr,FALSE,FALSE,nullptr));runtime.SetChangeHandler([&]{SetEvent(changed.Get());});
    PluginStateStore grants;
    const auto wait=[&](const std::string& id,const auto& predicate){
        const auto deadline=ipc::After(5000);
        while(std::chrono::steady_clock::now()<deadline){const auto state=runtime.Snapshot(id);if(state&&predicate(*state))return true;WaitForSingleObject(changed.Get(),100);}return false;
    };
    PluginRecord healthy;healthy.directory=temp.path/"plugins"/"com.example.healthy";healthy.state=PluginState::Valid;healthy.manifest=PluginManifest{};
    healthy.manifest->id="com.example.healthy";healthy.manifest->manifestVersion=2;healthy.manifest->apiVersion=1;
    healthy.manifest->runtime=NativeRuntime{"native-dll","plugin.dll"};healthy.manifest->requestedPermissions={"catalog.items.read","catalog.tasks.read","catalog.maps.read"};
    std::filesystem::create_directories(healthy.directory);std::filesystem::copy_file(argv[2],healthy.directory/"plugin.dll");
    Check(grants.Consent(*healthy.manifest)&&runtime.StartNative(healthy,grants)&&runtime.WaitFor(healthy.manifest->id,HostState::Running,10000),"healthy neighboring catalog Host");
    for(int mode=0;mode<10;++mode){
        PluginRecord record;record.directory=temp.path/"plugins"/("com.example.mode"+std::to_string(mode));record.state=PluginState::Valid;record.manifest=PluginManifest{};
        auto& manifest=*record.manifest;manifest.id=record.directory.filename().string();manifest.manifestVersion=2;manifest.apiVersion=1;manifest.runtime=NativeRuntime{"native-dll","plugin.dll"};
        manifest.requestedPermissions=mode==1?std::vector<std::string>{}:mode==2?std::vector<std::string>{"catalog.items.read"}:std::vector<std::string>{"catalog.items.read","catalog.tasks.read","catalog.maps.read"};
        std::filesystem::create_directories(record.directory);std::filesystem::copy_file(argv[mode+2],record.directory/"plugin.dll");
        Check(grants.Consent(manifest)&&runtime.StartNative(record,grants),"explicit fixture consent and isolated startup");
        if(mode==3||mode==4||mode==5||mode==8||mode==9){
            Check(runtime.WaitForTerminal(manifest.id,8000),"catalog callback faults are bounded");
            const auto state=runtime.Snapshot(manifest.id);
            if(mode==3)Check(state->error==HostError::DataTimeout,"hung result callback deadline");
            if(mode==4)Check(state->state==HostState::Crashed,"native result callback crash isolated");
            if(mode==5||mode==8||mode==9)Check(state->error==HostError::LoadFailed&&state->loadResult==5,"invalid extension size/version/function is never invoked");
            Check(state->pendingData==0&&state->dataResults==0,"fault discards pending data");
        }else{
            Check(runtime.WaitFor(manifest.id,HostState::Running,10000),"valid optional extension");
            const unsigned expected=mode==1?0:mode==2?2:mode==7?16:6;
            Check(wait(manifest.id,[&](const auto& state){return state.dataResults==expected&&state.pendingData==0;}),"declared scopes, short request table and outstanding limit enforced");
            Check(runtime.Ping(manifest.id)&&runtime.WaitForPong(manifest.id,1,5000),"fixture remains responsive");
            Check(runtime.Stop(manifest.id)&&runtime.WaitForTerminal(manifest.id,6000)&&runtime.Snapshot(manifest.id)->shutdownAcknowledged,"clean shutdown after result callbacks");
        }
        Check(GetModuleHandleW(L"plugin.dll")==nullptr,"no plugin DLL in core owner");
        Check(runtime.Ping(healthy.manifest->id)&&runtime.WaitForPong(healthy.manifest->id,static_cast<unsigned>(mode+1),5000),"failed catalog session never corrupts neighboring Host");
    }
    PluginRecord cancel=healthy;cancel.directory=temp.path/"plugins"/"com.example.cancel";cancel.manifest->id="com.example.cancel";
    std::filesystem::create_directories(cancel.directory);std::filesystem::copy_file(argv[5],cancel.directory/"plugin.dll");
    Check(grants.Consent(*cancel.manifest)&&runtime.StartNative(cancel,grants)&&runtime.WaitFor(cancel.manifest->id,HostState::Running,10000),"start bounded hanging callback fixture");
    Check(wait(cancel.manifest->id,[](const auto& state){return state.pendingData>0;}),"result outstanding before Disable");
    Check(runtime.Stop(cancel.manifest->id)&&runtime.Snapshot(cancel.manifest->id)->pendingData==0,"Disable immediately invalidates outstanding requests");
    Check(runtime.WaitForTerminal(cancel.manifest->id,6000)&&runtime.Snapshot(cancel.manifest->id)->pendingData==0&&runtime.Snapshot(cancel.manifest->id)->dataResults==0,"hung callback is terminated without late delivery");
    Check(runtime.Stop(healthy.manifest->id)&&runtime.WaitForTerminal(healthy.manifest->id,6000),"neighbor cleanup");
    std::cout<<"Catalog extension bounds/denial/callback crash and hang PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
