#include "plugins/PluginPipe.h"
#include <iostream>
#include <stdexcept>
using namespace noven::plugins::ipc;
void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
struct Temp {std::filesystem::path path=std::filesystem::temp_directory_path()/("noven-native-test-"+RandomSecret());Temp(){std::filesystem::create_directory(path);}~Temp(){std::error_code e;std::filesystem::remove_all(path,e);}};
int wmain(int argc,wchar_t** argv) try {
    Check(argc==17,"host and fifteen ABI fixtures required");
    for(int mode=0;mode<15;++mode) {
        Temp temp;std::filesystem::copy_file(argv[1],temp.path/"NovenPluginHost.exe");
        const auto directory=temp.path/"plugins"/"com.example.fixture";std::filesystem::create_directories(directory);
        std::filesystem::copy_file(argv[mode+2],directory/"plugin.dll");
        auto job=CreateHostJob();const auto name=PipeName(RandomSecret());const auto secret=RandomSecret();auto pipe=CreateServer(name);
        auto host=LaunchHost(temp.path/"NovenPluginHost.exe",job.Get(),name,"com.example.fixture",secret);
        Check(ConnectServer(pipe.Get(),After(5000),nullptr,host.process.Get())==IoResult::Complete,"native host connects");
        Channel channel(pipe.Get());Message hello;
        Check(channel.Read(hello,After(5000),nullptr,host.process.Get())==IoResult::Complete&&MatchesSession(hello,"com.example.fixture",secret),"authenticated native session");
        hello.type=MessageType::HelloAck;Check(channel.Write(hello,After(2000),host.process.Get())==IoResult::Complete,"hello ack");
        Message load{MessageType::LoadPlugin};const auto utf8=directory.u8string();load.directory.assign(utf8.begin(),utf8.end());load.entry="plugin.dll";load.pagePermission=mode!=13;
        Check(channel.Write(load,After(2000),host.process.Get())==IoResult::Complete,"explicit native load command");
        Message incoming;IoResult result;unsigned registrations=0,publications=0,logs=0;const auto deadline=After(mode==8?300:5000);
        do {
            result=channel.Read(incoming,deadline,nullptr,host.process.Get());if(result!=IoResult::Complete)break;
            if(incoming.type==MessageType::UiRegisterPage)++registrations;
            else if(incoming.type==MessageType::UiPublishPage)++publications;
            else if(incoming.type==MessageType::Log)++logs;
        }while(incoming.type!=MessageType::LoadPluginResult);
        Check(GetModuleHandleW(L"plugin.dll")==nullptr,"plugin is never mapped into Noven-side owner");
        if(mode==6){Check(result!=IoResult::Complete&&WaitForSingleObject(host.process.Get(),2000)==WAIT_OBJECT_0,"init crash isolated to Host");continue;}
        if(mode==8){Check(result==IoResult::Timeout,"init hang bounded by owner deadline");job.Reset();Check(WaitForSingleObject(host.process.Get(),2000)==WAIT_OBJECT_0,"hung init Host contained");continue;}
        Check(result==IoResult::Complete&&incoming.type==MessageType::LoadPluginResult,"load reply");
        if(mode==1||mode==3)Check(incoming.result==3,"missing exports");
        else if(mode==2)Check(incoming.result==4,"wrong ABI");
        else if(mode==4||mode==12)Check(incoming.result==5,"invalid size or callback");
        else if(mode==5)Check(incoming.result==6,"initialize failure");
        else {
            Check(incoming.result==0,"valid initialized plugin");
            if(mode==13)Check(registrations==0&&publications==0,"denied page grant has no side effects");
            else Check(registrations==1&&publications==1,"owned page and bounded document published");
            Check(logs<=16,"log burst bounded");
            if(mode==0||mode==9||mode==10) {
                Message action{MessageType::UiAction};action.pageId="dashboard";action.actionId="refresh";Check(channel.Write(action,After(2000),host.process.Get())==IoResult::Complete,"scoped UI action");
                if(mode==9||mode==10){result=channel.Read(incoming,After(300),nullptr,host.process.Get());Check(result!=IoResult::Complete,"hung/crashed action isolated");job.Reset();Check(WaitForSingleObject(host.process.Get(),2000)==WAIT_OBJECT_0,"action fault cleaned up");continue;}
                Check(channel.Read(incoming,After(2000),nullptr,host.process.Get())==IoResult::Complete&&incoming.type==MessageType::UiPublishPage&&incoming.document.find("Clicked")!=std::string::npos,"action updates plugin page");
                Check(channel.Read(incoming,After(2000),nullptr,host.process.Get())==IoResult::Complete&&incoming.type==MessageType::UiActionResult&&incoming.result==0,"action completed");
            }
        }
        Check(channel.Write({MessageType::Shutdown},After(2000),host.process.Get())==IoResult::Complete,"native shutdown request");
        result=channel.Read(incoming,After(mode==7?300:2000),nullptr,host.process.Get());
        if(mode==7){Check(result==IoResult::Timeout,"shutdown callback hang bounded");job.Reset();}
        else Check(result==IoResult::Complete&&incoming.type==MessageType::ShutdownAck,"plugin shutdown acknowledged");
        Check(WaitForSingleObject(host.process.Get(),2000)==WAIT_OBJECT_0,"native host does not orphan");
    }
    std::cout<<"Native ABI/Host isolation PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
