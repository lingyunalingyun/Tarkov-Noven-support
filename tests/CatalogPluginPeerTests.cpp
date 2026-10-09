#include "plugins/PluginRuntimeManager.h"
#include "plugins/PluginPipe.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace noven::plugins;
void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
struct Temp {
    std::filesystem::path path=std::filesystem::temp_directory_path()/("noven-catalog-peer-"+ipc::RandomSecret());
    Temp(){std::filesystem::create_directory(path);}
    ~Temp(){std::error_code error;std::filesystem::remove_all(path,error);}
};
int wmain(int argc,wchar_t** argv) try {
    Check(argc==3,"first-party fault peer and native fixture required");Temp temp;
    std::filesystem::copy_file(argv[1],temp.path/"NovenPluginHost.exe");
    noven::data::ItemRecord item;item.id="item_1";std::vector items{item};
    PluginRuntimeManager runtime(temp.path);runtime.SetCatalogService(std::make_shared<CatalogPluginService>(items,std::span<const noven::data::TaskRecord>{},std::span<const noven::data::MapRecord>{}));
    ipc::Handle changed(CreateEventW(nullptr,FALSE,FALSE,nullptr));runtime.SetChangeHandler([&]{SetEvent(changed.Get());});
    PluginRecord healthy;healthy.state=PluginState::Valid;healthy.manifest=PluginManifest{};healthy.manifest->id="dev.example.healthy";
    healthy.manifest->manifestVersion=healthy.manifest->apiVersion=1;
    Check(runtime.Start(healthy)&&runtime.WaitFor(healthy.manifest->id,HostState::Ready,10000),"healthy first-party session");
    PluginStateStore grants;unsigned pongs=0;
    for(const auto mode:{"catalog-denied","catalog-history-denied","catalog-events-denied","catalog-scan-denied","catalog-duplicate","catalog-overflow","catalog-ack","catalog-spoof","scan-denied","scan-ack","scan-suback","scan-spoof"}){
        {std::ofstream output(temp.path/"fault.txt");output<<mode;}
        PluginRecord record;record.state=PluginState::Valid;record.directory=temp.path/"plugins"/(std::string("com.example.")+mode);record.manifest=PluginManifest{};
        auto& manifest=*record.manifest;manifest.id=record.directory.filename().string();manifest.manifestVersion=2;manifest.apiVersion=1;
        manifest.runtime=NativeRuntime{"native-dll","plugin.dll"};manifest.requestedPermissions={"catalog.tasks.read"};
        std::filesystem::create_directories(record.directory);std::filesystem::copy_file(argv[2],record.directory/"plugin.dll");
        Check(grants.Consent(manifest)&&runtime.StartNative(record,grants),"authenticated fault session");
        if(std::string_view(mode).ends_with("denied")){
            Check(runtime.WaitFor(manifest.id,HostState::Running,5000),"fault peer simulates load only");
            if(std::string_view(mode)=="scan-denied"){
                bool answered=false;const auto deadline=ipc::After(5000);
                while(std::chrono::steady_clock::now()<deadline){if(runtime.Snapshot(manifest.id)->lastLog=="scan-denied-ack"){answered=true;break;}WaitForSingleObject(changed.Get(),50);}
                Check(answered,"bounded explicit subscription denial result acknowledged");
                Check(runtime.Ping(manifest.id)&&runtime.WaitForPong(manifest.id,1,5000)&&!runtime.Snapshot(manifest.id)->scanSubscribed,"core denies subscription despite Host bypass and foreign grant");
                Check(runtime.Stop(manifest.id)&&runtime.WaitForTerminal(manifest.id,6000)&&runtime.Snapshot(manifest.id)->shutdownAcknowledged,"denied subscription clean shutdown");
                Check(runtime.Ping(healthy.manifest->id)&&runtime.WaitForPong(healthy.manifest->id,++pongs,5000),"subscription denial leaves neighbor healthy");continue;
            }
            const auto deadline=ipc::After(5000);bool acknowledged=false;
            while(std::chrono::steady_clock::now()<deadline){
                const auto state=runtime.Snapshot(manifest.id);if(state->dataResults==1&&state->pendingData==0){acknowledged=true;break;}
                Check(state->state==HostState::Running,"denied request does not kill session");WaitForSingleObject(changed.Get(),100);
            }
            Check(acknowledged,"core denies Items despite Host bypass and Tasks grant");
            Check(runtime.Stop(manifest.id)&&runtime.WaitForTerminal(manifest.id,6000)&&runtime.Snapshot(manifest.id)->shutdownAcknowledged,"denied result and clean shutdown");
        }else{
            Check(runtime.WaitForTerminal(manifest.id,6000),"malicious protocol is bounded");
            std::cout<<mode<<" state="<<static_cast<int>(runtime.Snapshot(manifest.id)->state)<<" error="<<static_cast<int>(runtime.Snapshot(manifest.id)->error)<<'\n';
            Check(runtime.Snapshot(manifest.id)->state==HostState::ProtocolError&&runtime.Snapshot(manifest.id)->error==HostError::InvalidProtocol&&runtime.Snapshot(manifest.id)->pendingData==0,"duplicates, overflow, foreign ACK and spoof rejected by core");
        }
        Check(runtime.Ping(healthy.manifest->id)&&runtime.WaitForPong(healthy.manifest->id,++pongs,5000),"fault leaves another authenticated session healthy");
    }
    Check(runtime.Stop(healthy.manifest->id)&&runtime.WaitForTerminal(healthy.manifest->id,6000),"owned peer cleanup");
    std::cout<<"Catalog core authorization and hostile wire bounds PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
