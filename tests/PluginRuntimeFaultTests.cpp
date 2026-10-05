#include "plugins/PluginRuntimeManager.h"
#include "plugins/PluginPipe.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
using namespace noven::plugins;
void Check(bool ok,const char* text){if(!ok){std::cerr<<text<<'\n';std::exit(1);}}
PluginRecord Record(){PluginRecord record;record.state=PluginState::Valid;record.manifest=PluginManifest{};record.manifest->id="com.example.fault";record.manifest->manifestVersion=record.manifest->apiVersion=1;return record;}
struct Temp final {
    std::filesystem::path path=std::filesystem::temp_directory_path()/ ("noven-host-test-"+ipc::RandomSecret());
    Temp(){std::filesystem::create_directory(path);}
    ~Temp(){std::error_code error;std::filesystem::remove_all(path,error);}
};
int wmain(int argc,wchar_t** argv){
    Check(argc==3,"first-party test peer and real runtime paths required");
    for(const auto mode:{"wrong-id","wrong-token","wrong-version","no-connect","no-hello","zero-frame","oversized-frame","bad-json","bad-utf8","missing-fields","unknown-type","duplicate-hello","disconnect","mid-frame","exit-normal","exit-abnormal","ignore-shutdown","silent-pong","host-ping"}){
        Temp fixture;std::filesystem::copy_file(argv[1],fixture.path/L"NovenPluginHost.exe");{std::ofstream file(fixture.path/"fault.txt");file<<mode;}
        PluginRuntimeManager manager(fixture.path);Check(manager.Start(Record()),"test session start");
        const std::string_view fault(mode);
        if(fault=="ignore-shutdown"||fault=="silent-pong"||fault=="host-ping"){
            Check(manager.WaitFor("com.example.fault",HostState::Ready,10000),"controlled peer Ready");
            if(fault=="silent-pong")Check(manager.Ping("com.example.fault"),"request silent ping");
            else {
                if(fault=="host-ping")Check(manager.Ping("com.example.fault")&&manager.WaitForPong("com.example.fault",1,5000),"bidirectional ping interleaving");
                Check(manager.Stop("com.example.fault"),"request stop");
            }
        }
        Check(manager.WaitForTerminal("com.example.fault",10000),"fault has bounded terminal transition");
        const auto state=*manager.Snapshot("com.example.fault");
        std::cout<<mode<<" state="<<static_cast<int>(state.state)<<" error="<<static_cast<int>(state.error)<<'\n';
        if(fault.starts_with("wrong-"))Check(state.state==HostState::ProtocolError&&state.error==HostError::HandshakeMismatch,"wrong handshake rejected");
        else if(fault=="no-connect")Check(state.error==HostError::ConnectionTimeout,"connection timeout");
        else if(fault=="no-hello")Check(state.error==HostError::HandshakeTimeout,"missing hello timeout");
        else if(fault=="ignore-shutdown")Check(state.error==HostError::ShutdownTimeout,"forced cleanup after missing shutdownAck");
        else if(fault=="silent-pong")Check(state.error==HostError::PingTimeout,"bounded ping timeout");
        else if(fault=="exit-normal"||fault=="host-ping")Check(state.state==HostState::Exited,"normal exit / bidirectional ping");
        else if(fault=="exit-abnormal"||fault=="disconnect"||fault=="mid-frame")Check(state.state==HostState::Crashed,"crash/disconnect isolated");
        else Check(state.state==HostState::ProtocolError&&state.error==HostError::InvalidProtocol,"bad frames/messages rejected");
        ipc::Handle child(OpenProcess(SYNCHRONIZE,FALSE,state.processId));Check(!child||WaitForSingleObject(child.Get(),1000)==WAIT_OBJECT_0,"fault cleanup has no orphan");
        std::cout<<mode<<" PASS\n";
    }
    // 一个故障会话与一个真实 Host 并存，不共享进程、管道或失败状态。
    // A faulty session and a real Host coexist without sharing process, pipe or failure state.
    Temp fixture;std::filesystem::copy_file(argv[1],fixture.path/L"NovenPluginHost.exe");{std::ofstream file(fixture.path/"fault.txt");file<<"wrong-token";}
    PluginRuntimeManager failed(fixture.path),healthy(argv[2]);auto other=Record();other.manifest->id="dev.example.healthy";
    Check(healthy.Start(other)&&failed.Start(Record()),"parallel independent owners");
    Check(failed.WaitForTerminal("com.example.fault",10000)&&healthy.WaitFor("dev.example.healthy",HostState::Ready,10000),"failed session cannot corrupt healthy neighbor");
    Check(healthy.Ping("dev.example.healthy")&&healthy.WaitForPong("dev.example.healthy",1,5000),"healthy owner still communicates");
}
