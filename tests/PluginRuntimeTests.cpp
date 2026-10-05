#include "plugins/PluginRuntimeManager.h"
#include "plugins/PluginPipe.h"
#include <cstdlib>
#include <iostream>
#include <fstream>
using namespace noven::plugins;
void Check(bool ok,const char* text){if(!ok){std::cerr<<text<<'\n';std::exit(1);}}
PluginRecord Record(std::string id){PluginRecord record;record.state=PluginState::Valid;record.manifest=PluginManifest{};record.manifest->id=std::move(id);record.manifest->manifestVersion=record.manifest->apiVersion=1;return record;}
int wmain(int argc,wchar_t** argv){
    if(argc==4&&std::wstring_view(argv[1])==L"--owner-fixture"){
        PluginRuntimeManager owner(argv[2]);Check(owner.Start(Record("com.example.owner-exit"))&&owner.WaitFor("com.example.owner-exit",HostState::Ready,10000),"abrupt-owner fixture ready");
        {std::ofstream output(argv[3]);output<<owner.Snapshot("com.example.owner-exit")->processId;}
        ExitProcess(0);
    }
    Check(argc==2,"runtime directory required");const std::filesystem::path directory(argv[1]);DWORD before=0,after=0;
    // Win32/CNG 首次调用缓存系统句柄；先完整初始化，再精确比较后续会话的计数。
    // Win32/CNG cache system handles on first use; fully initialize before exact subsequent-session counts.
    {PluginRuntimeManager warmup(directory);Check(warmup.Start(Record("com.example.warmup"))&&warmup.WaitFor("com.example.warmup",HostState::Ready,10000),"initialize first-party runtime");}
    Check(GetProcessHandleCount(GetCurrentProcess(),&before),"handle count before");
    {
        PluginRuntimeManager manager(directory);Check(manager.SessionCount()==0,"constructing manager creates no session");
        Check(manager.HostPath()==directory/L"NovenPluginHost.exe","host only from runtime directory");
        Check(!manager.Start(PluginRecord{})&&!manager.Stop("missing")&&!manager.Ping("missing")&&!manager.Snapshot("missing"),"invalid state rejection");
        for(const auto& id:{"com.example.one","dev.example.two"})Check(manager.Start(Record(id)),"independent sessions start");
        Check(!manager.Start(Record("com.example.one")),"no duplicate session");
        for(const auto& id:{"com.example.one","dev.example.two"}){
            Check(manager.WaitFor(id,HostState::Ready,10000),"valid handshake reaches Ready");
            const auto state=manager.Snapshot(id);Check(state&&state->jobOwned&&state->processId!=GetCurrentProcessId(),"out-of-process, Job owned");
            Check(manager.Ping(id)&&manager.WaitForPong(id,1,5000),"manager ping/pong");
        }
        Check(manager.Snapshot("com.example.one")->processId!=manager.Snapshot("dev.example.two")->processId,"one process per session");
        Check(manager.Stop("com.example.one")&&manager.WaitForTerminal("com.example.one",6000),"graceful shutdown");
        Check(manager.Snapshot("com.example.one")->shutdownAcknowledged&&manager.Snapshot("com.example.one")->state==HostState::Exited,"acknowledged exit");
        Check(manager.Ping("dev.example.two")&&manager.WaitForPong("dev.example.two",2,5000),"other session remains independent");
        // 只终止本测试明确创建的第一方进程，验证崩溃隔离。
        // Terminate only the first-party process explicitly created by this test to verify crash isolation.
        ipc::Handle process(OpenProcess(PROCESS_TERMINATE|SYNCHRONIZE,FALSE,manager.Snapshot("dev.example.two")->processId));
        Check(process&&TerminateProcess(process.Get(),71),"simulate abnormal host exit");
        Check(manager.WaitForTerminal("dev.example.two",6000)&&manager.Snapshot("dev.example.two")->state==HostState::Crashed,"crash does not crash owner");
    }
    Check(GetProcessHandleCount(GetCurrentProcess(),&after),"handle count after");
    Check(before==after,"all process/pipe/job/thread/event handles released");
    {
        PluginRuntimeManager manager(directory/L"not-present");Check(manager.Start(Record("com.example.missing")),"async missing host start");
        Check(manager.WaitForTerminal("com.example.missing",6000)&&manager.Snapshot("com.example.missing")->error==HostError::Startup,"CreateProcess failure is bounded diagnostic");
    }
    {
        PluginRuntimeManager bounded(directory/L"not-present");
        for(int i=0;i<16;++i)Check(bounded.Start(Record("com.example.bound"+std::to_string(i))),"bounded retained sessions");
        Check(!bounded.Start(Record("com.example.overflow"))&&bounded.SessionCount()==16,"session count cannot grow unbounded");
    }
    DWORD child=0;
    {PluginRuntimeManager manager(directory);manager.Start(Record("com.example.cleanup"));Check(manager.WaitFor("com.example.cleanup",HostState::Ready,10000),"cleanup session ready");child=manager.Snapshot("com.example.cleanup")->processId;}
    ipc::Handle process(OpenProcess(SYNCHRONIZE,FALSE,child));Check(!process||WaitForSingleObject(process.Get(),2000)==WAIT_OBJECT_0,"destruction leaves no orphan");
    const auto output=std::filesystem::temp_directory_path()/("noven-owner-test-"+ipc::RandomSecret()+".txt");
    wchar_t self[32768]{};Check(GetModuleFileNameW(nullptr,self,32768)!=0,"test owner executable");
    auto command=ipc::QuoteArgument(self)+L" --owner-fixture "+ipc::QuoteArgument(directory.wstring())+L" "+ipc::QuoteArgument(output.wstring());
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION ownerProcess{};
    Check(CreateProcessW(self,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&ownerProcess),"first-party abrupt-owner test process");
    ipc::Handle owner(ownerProcess.hProcess),ownerThread(ownerProcess.hThread);
    Check(WaitForSingleObject(owner.Get(),15000)==WAIT_OBJECT_0,"test owner exits without destructors");DWORD ownerCode=99;
    Check(GetExitCodeProcess(owner.Get(),&ownerCode)&&ownerCode==0,"test owner prepared host before abrupt exit");
    {std::ifstream input(output);child=0;input>>child;Check(!input.fail()&&child!=0,"child identity recorded without secrets");}
    std::filesystem::remove(output);ipc::Handle orphan(OpenProcess(SYNCHRONIZE,FALSE,child));
    Check(!orphan||WaitForSingleObject(orphan.Get(),3000)==WAIT_OBJECT_0,"OS closes owner Job and kills host after abrupt owner exit");
    std::cout<<"Plugin runtime ownership PASS\n";
}
