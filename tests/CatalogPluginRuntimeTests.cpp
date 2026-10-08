#include "plugins/PluginRuntimeManager.h"
#include "plugins/PluginPipe.h"
#include <iostream>
#include <stdexcept>
using namespace noven::plugins;
void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
struct Temp {
    std::filesystem::path path=std::filesystem::temp_directory_path()/("noven-catalog-runtime-"+ipc::RandomSecret());
    Temp(){std::filesystem::create_directory(path);}
    ~Temp(){std::error_code error;std::filesystem::remove_all(path,error);}
};
PluginRecord Record(const std::filesystem::path& root,std::string id){
    PluginRecord record;record.directory=root/"plugins"/id;record.state=PluginState::Valid;record.manifest=PluginManifest{};
    auto& manifest=*record.manifest;manifest.manifestVersion=2;manifest.apiVersion=1;manifest.id=std::move(id);
    manifest.runtime=NativeRuntime{"native-dll","plugin.dll"};manifest.requestedPermissions={"catalog.items.read","catalog.tasks.read","catalog.maps.read"};return record;
}
int wmain(int argc,wchar_t** argv) try {
    Check(argc==3,"Host/catalog fixture paths");Temp temp;
    std::filesystem::copy_file(argv[1],temp.path/"NovenPluginHost.exe");
    auto first=Record(temp.path,"com.example.first"),second=Record(temp.path,"com.example.second");
    for(const auto* record:{&first,&second}){std::filesystem::create_directories(record->directory);std::filesystem::copy_file(argv[2],record->directory/"plugin.dll");}
    noven::data::ItemRecord item;item.id="item_1";
    noven::data::TaskRecord task;task.id="task_1";task.mode="regular";
    noven::data::MapRecord map;map.id="interchange";
    std::vector items{item};std::vector tasks{task};std::vector maps{map};
    auto service=std::make_shared<CatalogPluginService>(items,tasks,maps);
    PluginRuntimeManager runtime(temp.path);runtime.SetCatalogService(service);
    ipc::Handle changed(CreateEventW(nullptr,FALSE,FALSE,nullptr));runtime.SetChangeHandler([&]{SetEvent(changed.Get());});
    PluginStateStore grants;
    Check(!runtime.StartNative(first,grants)&&runtime.SessionCount()==0,"new catalog manifests never execute without consent");
    Check(grants.Consent(*first.manifest)&&!runtime.StartNative(second,grants),"foreign ID cannot use first grants");
    Check(grants.Consent(*second.manifest)&&runtime.StartNative(first,grants)&&runtime.StartNative(second,grants),"two independently authorized catalog hosts");
    for(const auto* record:{&first,&second})Check(runtime.WaitFor(record->manifest->id,HostState::Running,10000),"catalog plugin reaches Running");
    const auto deadline=ipc::After(5000);
    bool complete=false;
    while(std::chrono::steady_clock::now()<deadline){
        const auto a=runtime.Snapshot(first.manifest->id),b=runtime.Snapshot(second.manifest->id);
        if(a->dataResults==6&&b->dataResults==6&&a->pendingData==0&&b->pendingData==0){complete=true;break;}
        WaitForSingleObject(changed.Get(),100);
    }
    Check(complete,"six asynchronous results each, identical request IDs remain session-isolated");
    Check(GetModuleHandleW(L"plugin.dll")==nullptr,"core side never loads DLL");
    runtime.SetCatalogLocale("en-US");
    Check(runtime.Stop(first.manifest->id)&&runtime.Snapshot(first.manifest->id)->pendingData==0&&runtime.WaitForTerminal(first.manifest->id,6000),"disable invalidates pending state and terminates owned Host");
    Check(runtime.Snapshot(first.manifest->id)->shutdownAcknowledged,"pending result cleanup permits clean shutdown");
    Check(runtime.Ping(second.manifest->id)&&runtime.WaitForPong(second.manifest->id,1,5000),"stopping neighbor preserves healthy session");
    auto expanded=second;expanded.manifest->requestedPermissions.push_back("ui.page.register");
    Check(!grants.Authorized(*expanded.manifest),"permission expansion invalidates existing catalog consent");
    Check(runtime.Stop(second.manifest->id)&&runtime.WaitForTerminal(second.manifest->id,6000),"all hosts bounded and cleaned");
    Check(!runtime.StartNative(expanded,grants),"new permission cannot borrow previous session consent");
    std::cout<<"Catalog runtime ownership, consent and isolation PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
