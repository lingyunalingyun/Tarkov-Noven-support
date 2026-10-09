#include "plugins/PluginRuntimeManager.h"
#include "plugins/PluginPipe.h"
#include <iostream>
#include <stdexcept>
using namespace noven::plugins;
void Check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
struct Temp {std::filesystem::path path=std::filesystem::temp_directory_path()/("noven-storage-runtime-"+ipc::RandomSecret());Temp(){std::filesystem::create_directory(path);}~Temp(){std::error_code error;std::filesystem::remove_all(path,error);}};
int wmain(int argc,wchar_t** argv) try {
    Check(argc==8,"Host and six first-party fixtures required");Temp temp;std::filesystem::copy_file(argv[1],temp.path/"NovenPluginHost.exe");
    PluginRuntimeManager runtime(temp.path);ipc::Handle changed(CreateEventW(nullptr,FALSE,FALSE,nullptr));runtime.SetChangeHandler([&]{SetEvent(changed.Get());});
    const auto start=[&](std::string id,unsigned mode,bool permission=true){
        const auto directory=temp.path/"plugins"/id;std::filesystem::create_directories(directory);std::filesystem::copy_file(argv[mode+2],directory/"fixture.dll");
        auto manifest=*ParseManifest("{\"manifestVersion\":2,\"id\":\""+id+"\",\"name\":\"Fixture\",\"version\":\"1.0.0\",\"apiVersion\":1,\"runtime\":{\"kind\":\"native-dll\",\"entry\":\"fixture.dll\"},\"permissions\":[\"ui.page.register\""+(permission?",\"storage.plugin\"":"")+"]}").manifest;
        PluginRecord record;record.directory=directory;record.state=PluginState::Valid;record.manifest=manifest;PluginStateStore store;
        Check(!runtime.StartNative(record,store),"declared ungranted rejected before launch");Check(store.Consent(manifest)&&runtime.StartNative(record,store),"consented fixture starts");return id;
    };
    const auto wait=[&](std::string_view id,std::string_view log){const auto deadline=ipc::After(6000);while(std::chrono::steady_clock::now()<deadline){if(runtime.Snapshot(id)->lastLog==log)return true;WaitForSingleObject(changed.Get(),50);}return false;};
    const auto action=[&](std::string_view id,std::string_view name){const auto deadline=ipc::After(5000);while(std::chrono::steady_clock::now()<deadline){auto state=runtime.Snapshot(id);if(runtime.Action(id,state->generation,"dashboard",name))return;WaitForSingleObject(changed.Get(),50);}throw std::runtime_error("fixture action dispatch");};
    const auto barrier=[&](std::string_view id){auto count=runtime.Snapshot(id)->pongs;Check(runtime.Ping(id)&&runtime.WaitForPong(id,count+1,5000),"independent responsive Host");};
    // ping 可先于异步按钮处理返回；等待回调实际发布的状态，不把 pong 当作动作屏障。
    // Ping may precede asynchronous action processing; wait for the published callback state instead.
    const auto document=[&](std::string_view id,std::string_view text){const auto deadline=ipc::After(6000);while(std::chrono::steady_clock::now()<deadline){const auto state=runtime.Snapshot(id);if(state->state==HostState::Running&&!state->pages.empty()&&!state->pages[0].document.blocks.empty()&&state->pages[0].document.blocks[0].text==text)return true;WaitForSingleObject(changed.Get(),50);}return false;};
    const auto a=start("com.example.store-a",0),b=start("com.example.store-b",0),denied=start("com.example.store-denied",0,false);
    for(const auto& id:{a,b,denied})Check(runtime.WaitFor(id,HostState::Running,10000),"running first-party Host");
    action(a,"set");Check(wait(a,"result:0:0:0"),"own SET success");action(b,"get");Check(wait(b,"result:2:0:0"),"B cannot GET A");
    action(b,"list");Check(wait(b,"result:0:0:0"),"B cannot LIST A");action(b,"delete");Check(wait(b,"result:2:0:0"),"B cannot DELETE A");
    action(a,"get");Check(wait(a,"result:0:7:0"),"foreign DELETE never modified A");
    action(denied,"get");Check(wait(denied,"admission:-2"),"undeclared denied by Host");
    action(a,"bad");Check(wait(a,"admission:-1"),"path-like key rejected before queue");
    action(a,"duplicate");Check(document(a,"admission:-4"),"duplicate rejected without corrupting session");barrier(a);
    action(a,"flood");Check(document(a,"admission:-3"),"bounded outstanding/rate rejection without corruption");barrier(a);
    const auto large=start("com.example.store-large",0);Check(runtime.WaitFor(large,HostState::Running,10000),"large frame fixture loads");action(large,"batch");Check(wait(large,"batch:16"),"sixteen queued large SETs complete without duplex pipe saturation");barrier(large);
    // 批量刚用完一秒接纳额度；等待窗口后验证大 GET，不把合法限流误判为传输失败。
    // Batch consumed the admission window; wait before testing large GET, not mistaking valid throttling for transport failure.
    Sleep(1100);action(large,"get");Check(wait(large,"result:0:32768:0"),"maximum binary GET fits real pipe and ABI callback");
    const auto hung=start("com.example.store-hung",1);Check(runtime.WaitFor(hung,HostState::Running,10000),"hang fixture loads");action(hung,"get");Check(wait(hung,"callback-entered"),"result callback entered");barrier(b);
    Check(runtime.WaitForTerminal(hung,7000)&&runtime.Snapshot(hung)->pages.empty(),"hung storage result ACK bounded and pages removed");
    const auto disable=start("com.example.store-disable",1);Check(runtime.WaitFor(disable,HostState::Running,10000),"Disable pending fixture loads");action(disable,"get");Check(wait(disable,"callback-entered"),"pending callback");
    Check(runtime.Stop(disable)&&runtime.Snapshot(disable)->pages.empty()&&runtime.WaitForTerminal(disable,7000),"Disable pending callback bounded, late routing removed");
    Check(!runtime.Action(disable,runtime.Snapshot(disable)->generation,"dashboard","get"),"stale storage action denied");
    const auto crash=start("com.example.store-crash",2);Check(runtime.WaitFor(crash,HostState::Running,10000),"crash fixture loads");action(crash,"set");Check(runtime.WaitForTerminal(crash,7000)&&runtime.Snapshot(crash)->pages.empty(),"crash isolates and clears pages");barrier(b);
    for(unsigned mode=3;mode<6;++mode){auto id=start("com.example.store-abi-"+std::to_string(mode),mode);Check(runtime.WaitForTerminal(id,10000)&&runtime.Snapshot(id)->state!=HostState::Running,"optional extension size/schema/callback validated");}
    for(const auto& id:{a,b,denied,large})Check(runtime.Stop(id)&&runtime.WaitForTerminal(id,7000),"clean Host shutdown");
    Check(GetModuleHandleW(L"fixture.dll")==nullptr,"DLL never loaded in owner");std::cout<<"Storage Host isolation/limits/crash/hang/teardown PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
