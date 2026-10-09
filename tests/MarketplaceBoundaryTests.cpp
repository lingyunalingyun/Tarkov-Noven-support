#include "plugins/MarketplaceService.h"
#include "plugins/PluginRuntimeManager.h"
#include "plugins/PluginStateStore.h"
#include "plugins/PluginPipe.h"
#include <fstream>
#include <iostream>
using namespace noven::plugins;
void Check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
struct Temp {std::filesystem::path path=std::filesystem::temp_directory_path()/("noven-registry-boundary-"+ipc::RandomSecret());Temp(){std::filesystem::create_directory(path);}~Temp(){std::error_code e;std::filesystem::remove_all(path,e);}};
struct Backend final:IRegistryTransport {std::string text;unsigned calls{};std::string Fetch(std::stop_token) override{++calls;return text;}};
struct Pending final:IRegistryTransport {std::atomic_bool& entered;explicit Pending(std::atomic_bool& flag):entered(flag){}std::string Fetch(std::stop_token stop) override {entered=true;std::mutex mutex;std::condition_variable_any wake;std::unique_lock lock(mutex);wake.wait(lock,stop,[]{return false;});return R"({"registryVersion":1,"plugins":[]})";}};
int wmain(int argc,wchar_t** argv) try {
    Check(argc==2,"fixture argument");std::ifstream file(argv[1],std::ios::binary);const std::string text(std::istreambuf_iterator<char>(file),{});
    Temp temp;PluginRuntimeManager runtime;PluginStateStore grants;const auto original=grants.Encode();
    const auto plugins=temp.path/"plugins";std::filesystem::create_directory(plugins);{std::ofstream manifest(plugins/"manifest.json");manifest<<"local sentinel";}
    const auto before=std::filesystem::last_write_time(plugins/"manifest.json");
    std::mutex mutex;std::condition_variable changed;std::int64_t now=2000000000;
    auto backend=std::make_unique<Backend>();auto* transport=backend.get();backend->text=text;
    MarketplaceService service(temp.path/"cache.json","fixed-fixture",std::move(backend),[&]{std::lock_guard lock(mutex);changed.notify_all();},false,[&]{return now;});
    for(unsigned refresh=0;refresh<2;++refresh){
        Check(service.Refresh(),"explicit registry refresh");std::unique_lock lock(mutex);Check(changed.wait_for(lock,std::chrono::seconds(5),[&]{return service.Snapshot()->state!=MarketplaceState::Loading;}),"bounded refresh completion");lock.unlock();
        Check(runtime.SessionCount()==0&&grants.Encode()==original,"registry fetch cannot create host/grants/enable intent");
        Check(std::filesystem::last_write_time(plugins/"manifest.json")==before&&std::distance(std::filesystem::directory_iterator(plugins),std::filesystem::directory_iterator{})==1,"local plugin files never modified or package downloaded");
        Check(service.Snapshot()->registry->plugins[0].package&&!service.Snapshot()->registry->plugins[0].metadata.runtime,"malicious entry/command fields remain non-executable data");
        Check(!grants.Consent(service.Snapshot()->registry->plugins[0].metadata)&&grants.Encode()==original,"remote metadata cannot pass local runtime consent");now+=6;
    }
    Check(transport->calls==2,"only configured Registry fetched; package reference never followed");
    std::atomic_bool entered{};
    {MarketplaceService pending(temp.path/"pending.json","pending",std::make_unique<Pending>(entered));Check(pending.Refresh(),"pending fetch begins");const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(!entered&&std::chrono::steady_clock::now()<deadline)std::this_thread::yield();Check(entered,"test backend entered");}
    Check(!std::filesystem::exists(temp.path/"pending.json")&&runtime.SessionCount()==0,"teardown cancels pending work without late cache or runtime writes");
    std::cout<<"Registry no-execution/no-install/no-grants/local-state/teardown PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
