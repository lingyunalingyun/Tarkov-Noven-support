#include "plugins/PluginPipe.h"
#include <cstdlib>
#include <iostream>
#include <shellapi.h>
#include <aclapi.h>
using namespace noven::plugins::ipc;
void Check(bool ok,const char* text){if(!ok){std::cerr<<text<<'\n';std::exit(1);}}
int wmain(int argc,wchar_t** argv){
    Check(argc==2,"host path required");const std::filesystem::path executable(argv[1]);
    const auto token=RandomSecret();Check(token!=RandomSecret()&&ValidSecret(token),"Windows CSPRNG token");
    for(const auto value:{L"",L"C:\\folder with spaces\\",L"x\\\"y",L"plain"}){
        const auto quoted=L"program "+QuoteArgument(value);int count=0;auto parsed=CommandLineToArgvW(quoted.c_str(),&count);
        Check(parsed&&count==2&&std::wstring_view(parsed[1])==value,"Windows argument quoting round trip");LocalFree(parsed);
    }
    for(int scenario=0;scenario<6;++scenario){
        auto job=CreateHostJob();const auto pipeName=PipeName(RandomSecret());auto pipe=CreateServer(pipeName);
        PACL acl=nullptr;PSECURITY_DESCRIPTOR descriptor=nullptr;void* rawAce=nullptr;
        Check(GetSecurityInfo(pipe.Get(),SE_KERNEL_OBJECT,DACL_SECURITY_INFORMATION,nullptr,nullptr,&acl,nullptr,&descriptor)==ERROR_SUCCESS,"pipe security descriptor");
        Check(acl&&acl->AceCount==1&&GetAce(acl,0,&rawAce),"one explicit allow ACE, no Everyone defaults");
        const auto ace=static_cast<const ACCESS_ALLOWED_ACE*>(rawAce);auto sid=const_cast<DWORD*>(&ace->SidStart);
        Check(ace->Header.AceType==ACCESS_ALLOWED_ACE_TYPE&&IsValidSid(sid)&&*GetSidSubAuthorityCount(sid)==3&&*GetSidSubAuthority(sid,0)==SECURITY_LOGON_IDS_RID,"pipe access restricted to a logon SID");LocalFree(descriptor);
        auto process=LaunchHost(executable,job.Get(),pipeName,"com.example.test",token);
        Check(ConnectServer(pipe.Get(),After(5000),nullptr,process.process.Get())==IoResult::Complete,"host connects");
        ULONG peer=0;Check(GetNamedPipeClientProcessId(pipe.Get(),&peer)&&peer==process.id,"exact created peer PID");
        BOOL assigned=FALSE;Check(IsProcessInJob(process.process.Get(),job.Get(),&assigned)&&assigned,"job owns host");
        Channel channel(pipe.Get());Message hello;
        Check(channel.Read(hello,After(5000),nullptr,process.process.Get())==IoResult::Complete&&MatchesSession(hello,"com.example.test",token),"real host hello");
        if(scenario==5){job.Reset();Check(WaitForSingleObject(process.process.Get(),3000)==WAIT_OBJECT_0,"closing job terminates host");continue;}
        Message ack=hello;ack.type=MessageType::HelloAck;
        if(scenario==1)ack.pluginId="com.other.test";
        if(scenario==2)ack.session=RandomSecret();
        if(scenario==3)ack.protocolVersion=2;
        if(scenario==4)ack={MessageType::Shutdown};
        Check(channel.Write(ack,After(2000))==IoResult::Complete,"send acknowledgement");Message response;
        if(scenario==0){
            Check(channel.Write({MessageType::Ping},After(2000))==IoResult::Complete,"ping");
            Check(channel.Read(response,After(2000))==IoResult::Complete&&response.type==MessageType::Pong,"pong");
            Check(channel.Write({MessageType::Shutdown},After(2000))==IoResult::Complete,"shutdown");
        }
        Check(channel.Read(response,After(2000))==IoResult::Complete,"host final response");
        Check(response.type==((scenario==0||scenario==4)?MessageType::ShutdownAck:MessageType::ProtocolError),"mismatch reject / shutdown during handshake");
        Check(WaitForSingleObject(process.process.Get(),3000)==WAIT_OBJECT_0,"bounded host exit");DWORD code=99;
        Check(GetExitCodeProcess(process.process.Get(),&code)&&code==((scenario==0||scenario==4)?0u:3u),"host exit classification");
    }
    std::cout<<"First-party host boundary PASS\n";
}
