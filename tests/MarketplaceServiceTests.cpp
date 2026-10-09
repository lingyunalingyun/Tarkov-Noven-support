#include "plugins/MarketplaceService.h"
#include "plugins/PluginPipe.h"
#include <fstream>
#include <iostream>
#include <atomic>
using namespace noven::plugins;
void Check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
struct Temp {std::filesystem::path path=std::filesystem::temp_directory_path()/("noven-marketplace-"+ipc::RandomSecret());Temp(){std::filesystem::create_directory(path);}~Temp(){std::error_code e;std::filesystem::remove_all(path,e);}};
struct Backend final:IRegistryTransport {
    std::string text;bool fail{};std::atomic<unsigned> calls{};
    std::string Fetch(std::stop_token) override{++calls;if(fail)throw std::runtime_error("offline");return text;}
};
int wmain(int argc,wchar_t** argv) try{
    Check(argc==2,"fixture required");std::ifstream file(argv[1],std::ios::binary);const std::string text(std::istreambuf_iterator<char>(file),{});
    Temp temp;const auto cache=temp.path/"cache.json";std::atomic<std::int64_t> now{2000000000};
    std::mutex mutex;std::condition_variable changed;
    const auto notify=[&]{std::lock_guard lock(mutex);changed.notify_all();};
    const auto clock=[&]{return now.load();};
    const auto wait=[&](MarketplaceService& service){std::unique_lock lock(mutex);Check(changed.wait_for(lock,std::chrono::seconds(5),[&]{return service.Snapshot()->state!=MarketplaceState::Loading;}),"bounded deterministic completion");};
    auto backend=std::make_unique<Backend>();auto* transport=backend.get();backend->text=text;
    {
        MarketplaceService service(cache,"fixture",std::move(backend),notify,true,clock);
        Check(service.Snapshot()->state==MarketplaceState::Empty&&transport->calls==0,"no implicit network on construction");
        Check(service.Refresh(),"first fetch");wait(service);auto fresh=service.Snapshot();
        Check(fresh->state==MarketplaceState::Live&&fresh->registry->plugins.size()==1&&fresh->fetchedAt==now&&fresh->reviewFixture,"validated immutable snapshot");
        Check(std::filesystem::exists(cache)&&!service.Refresh(),"successful cache and anti-hammer cooldown");
        now+=6;transport->fail=true;Check(service.Refresh(),"explicit refresh after cooldown");wait(service);
        Check(service.Snapshot()->state==MarketplaceState::Cached&&service.Snapshot()->error==MarketplaceError::Fetch&&service.Snapshot()->fetchedAt==fresh->fetchedAt,"failed fetch preserves last good immutable snapshot");
        transport->fail=false;transport->text="{}";now+=6;Check(service.Refresh(),"invalid remote refresh");wait(service);
        Check(service.Snapshot()->state==MarketplaceState::Cached&&service.Snapshot()->error==MarketplaceError::InvalidRegistry,"bad remote metadata cannot replace good data");
        transport->text=R"({"registryVersion":1,"plugins":[]})";now+=6;
        ipc::Handle held(CreateFileW(cache.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr));Check(static_cast<bool>(held),"hold cache against replacement");
        Check(service.Refresh(),"refresh with replacement failure");wait(service);
        Check(service.Snapshot()->state==MarketplaceState::Live&&service.Snapshot()->error==MarketplaceError::CacheWrite&&service.Snapshot()->registry->plugins.empty(),"cache failure does not corrupt live snapshot");held.Reset();
        Check(std::distance(std::filesystem::directory_iterator(temp.path),std::filesystem::directory_iterator{})==1,"no abandoned temporary cache files");
    }
    {
        auto offline=std::make_unique<Backend>();offline->fail=true;
        MarketplaceService service(cache,"fixture",std::move(offline),notify,true,clock);
        Check(service.Snapshot()->state==MarketplaceState::Cached&&service.Snapshot()->registry->plugins.size()==1,"cache reload after failed fetch");
        Check(service.Refresh(),"offline refresh");wait(service);Check(service.Snapshot()->state==MarketplaceState::Cached,"cached offline usable");
    }
    {
        auto other=std::make_unique<Backend>();other->fail=true;MarketplaceService service(cache,"different-source",std::move(other),notify,false,clock);
        Check(!service.Snapshot()->registry,"source-bound cache");Check(service.Refresh(),"empty fetch failure");wait(service);Check(service.Snapshot()->state==MarketplaceState::Error&&!service.Snapshot()->registry,"clean empty error");
    }
    {std::ofstream out(cache,std::ios::binary|std::ios::trunc);out<<"corrupt";}
    {
        MarketplaceService service(cache,"fixture",std::make_unique<Backend>(),notify,false,clock);Check(service.Snapshot()->state==MarketplaceState::Empty&&!service.Snapshot()->registry,"corrupt cache ignored");
    }
    {MarketplaceService service(cache,"",{},notify,false,clock);Check(service.Snapshot()->state==MarketplaceState::Unconfigured&&!service.Refresh(),"no production source invented");}
    std::cout<<"Marketplace offline fetch/cache/failure/source binding/freshness PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
