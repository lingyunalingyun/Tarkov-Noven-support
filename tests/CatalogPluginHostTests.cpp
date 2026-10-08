#include "plugins/CatalogPluginService.h"
#include "plugins/PluginPipe.h"
#include <iostream>
#include <stdexcept>
#include <set>
using namespace noven::plugins;
using namespace noven::plugins::ipc;
void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
struct Temp {
    std::filesystem::path path=std::filesystem::temp_directory_path()/("noven-catalog-host-"+RandomSecret());
    Temp(){std::filesystem::create_directory(path);}
    ~Temp(){std::error_code error;std::filesystem::remove_all(path,error);}
};
int wmain(int argc,wchar_t** argv) try {
    Check(argc==3,"host and first-party catalog fixture required");Temp temp;
    std::filesystem::copy_file(argv[1],temp.path/"NovenPluginHost.exe");
    const auto directory=temp.path/"plugins"/"com.example.catalog";std::filesystem::create_directories(directory);
    std::filesystem::copy_file(argv[2],directory/"plugin.dll");
    auto job=CreateHostJob();const auto name=PipeName(RandomSecret());const auto secret=RandomSecret();auto pipe=CreateServer(name);
    auto host=LaunchHost(temp.path/"NovenPluginHost.exe",job.Get(),name,"com.example.catalog",secret);
    Check(ConnectServer(pipe.Get(),After(5000),nullptr,host.process.Get())==IoResult::Complete,"catalog Host connects");
    Channel channel(pipe.Get());Message incoming;
    Check(channel.Read(incoming,After(5000),nullptr,host.process.Get())==IoResult::Complete&&MatchesSession(incoming,"com.example.catalog",secret),"catalog Host authenticates");
    incoming.type=MessageType::HelloAck;Check(channel.Write(incoming,After(2000),host.process.Get())==IoResult::Complete,"handshake ack");
    Message access{MessageType::CatalogAccess};access.catalogMask=7;Check(channel.Write(access,After(2000),host.process.Get())==IoResult::Complete,"scoped pre-load grants");
    Message load{MessageType::LoadPlugin};const auto utf8=directory.u8string();load.directory.assign(utf8.begin(),utf8.end());load.entry="plugin.dll";
    Check(channel.Write(load,After(2000),host.process.Get())==IoResult::Complete,"load catalog fixture");
    Check(channel.Read(incoming,After(5000),nullptr,host.process.Get())==IoResult::Complete&&incoming.type==MessageType::LoadPluginResult&&incoming.result==0,"optional extension initializes before requests flush");
    noven::data::ItemRecord item;item.id="item_1";item.nameEn="Item";
    noven::data::TaskRecord task;task.mode="regular";task.id="task_1";task.nameEn="Task";
    noven::data::MapRecord map;map.id="interchange";map.nameEn="Interchange";
    std::vector items{item};std::vector tasks{task};std::vector maps{map};CatalogPluginService service(items,tasks,maps);
    CatalogGrants grants{true,{"catalog.items.read","catalog.tasks.read","catalog.maps.read"},{"catalog.items.read","catalog.tasks.read","catalog.maps.read"}};
    std::set<std::uint64_t> requests,acks;unsigned callbacks=0;const auto deadline=After(5000);
    while(acks.size()<6){
        Check(channel.Read(incoming,deadline,nullptr,host.process.Get())==IoResult::Complete,"bounded async callback round-trip");
        if(incoming.type==MessageType::DataRequest){
            Check(requests.insert(incoming.dataRequest.requestId).second,"only accepted requests reach IPC");
            Message reply{MessageType::DataResult};reply.dataResult=service.Query(incoming.dataRequest,grants);
            Check(reply.dataResult.status==DataStatus::Ok&&channel.Write(reply,After(2000),host.process.Get())==IoResult::Complete,"read-only projected response");
        }else if(incoming.type==MessageType::DataResultAck){Check(requests.contains(incoming.dataResult.requestId)&&acks.insert(incoming.dataResult.requestId).second,"callback ack scoped to accepted ID");}
        else if(incoming.type==MessageType::Log){Check(incoming.text=="Catalog callback","data callback receives original plugin context");++callbacks;}
        else Check(false,"unexpected catalog fixture message");
    }
    Check(callbacks==6&&requests==acks,"all three list/get callbacks complete");
    Check(GetModuleHandleW(L"plugin.dll")==nullptr,"native fixture never loaded into owner");
    Check(channel.Write({MessageType::Shutdown},After(2000),host.process.Get())==IoResult::Complete,"shutdown catalog session");
    Check(channel.Read(incoming,After(2000),nullptr,host.process.Get())==IoResult::Complete&&incoming.type==MessageType::ShutdownAck,"catalog shutdown acknowledges");
    Check(WaitForSingleObject(host.process.Get(),2000)==WAIT_OBJECT_0,"catalog Host not orphaned");
    std::cout<<"Catalog optional ABI extension and async Host PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
