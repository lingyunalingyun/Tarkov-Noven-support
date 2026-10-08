// 仅测试构建的第一方故障对端；生产 Host 没有故障模式、旁路入口或配置文件读取。
// First-party fault peer built only for tests; production Host has no fault modes, bypasses or config-file reads.
#include "plugins/PluginPipe.h"
#include <fstream>
#include <shellapi.h>
using namespace noven::plugins::ipc;
void Raw(HANDLE pipe,std::vector<std::uint8_t> bytes){
    Handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr));OVERLAPPED operation{};operation.hEvent=event.Get();DWORD count=0;
    if(!WriteFile(pipe,bytes.data(),static_cast<DWORD>(bytes.size()),&count,&operation)&&GetLastError()==ERROR_IO_PENDING){
        if(WaitForSingleObject(event.Get(),2000)!=WAIT_OBJECT_0)CancelIoEx(pipe,&operation);
        GetOverlappedResult(pipe,&operation,&count,TRUE);
    }
}
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int){
    int count=0;auto values=CommandLineToArgvW(GetCommandLineW(),&count);if(!values)return 2;
    try{
        const auto args=ParseHostArguments(count,values);LocalFree(values);values=nullptr;
        if(args.pluginId=="dev.example.healthy")return RunHost(args);
        wchar_t executable[32768]{};const auto length=GetModuleFileNameW(nullptr,executable,32768);if(!length||length==32768)return 2;
        std::ifstream file(std::filesystem::path(executable).parent_path()/"fault.txt");std::string mode;std::getline(file,mode);if(mode.size()>64)return 2;
        Handle parent(OpenProcess(SYNCHRONIZE,FALSE,args.parent));if(!parent)return 2;
        if(mode=="no-connect"){WaitForSingleObject(parent.Get(),INFINITE);return 0;}
        auto pipe=ConnectClient(args.pipe,args.parent);Channel channel(pipe.Get());
        if(mode=="no-hello"){WaitForSingleObject(parent.Get(),INFINITE);return 0;}
        Message hello{MessageType::Hello,1,args.pluginId,args.secret};
        if(mode=="wrong-id")hello.pluginId="com.other.plugin";
        if(mode=="wrong-token")hello.session=RandomSecret();
        if(mode=="wrong-version")hello.protocolVersion=2;
        if(mode=="zero-frame"){Raw(pipe.Get(),{0,0,0,0});WaitForSingleObject(parent.Get(),INFINITE);return 0;}
        if(mode=="oversized-frame"){Raw(pipe.Get(),{1,0,1,0});WaitForSingleObject(parent.Get(),INFINITE);return 0;}
        if(mode=="bad-json"||mode=="missing-fields"||mode=="bad-utf8"||mode=="unknown-type"){
            const std::string text=mode=="bad-json"?"{":mode=="missing-fields"?"{\"type\":\"hello\"}":mode=="unknown-type"?"{\"type\":\"execute\"}":std::string(1,static_cast<char>(0xff));
            Raw(pipe.Get(),Frame(text));WaitForSingleObject(parent.Get(),INFINITE);return 0;
        }
        if(channel.Write(hello,After(2000),parent.Get())!=IoResult::Complete)return 2;Message incoming;
        if(channel.Read(incoming,After(5000),nullptr,parent.Get())!=IoResult::Complete)return 2;
        if(incoming.type!=MessageType::HelloAck||!MatchesSession(incoming,args.pluginId,args.secret))return 2;
        if(mode=="exit-normal")return 0;
        if(mode=="exit-abnormal")return 71;
        if(mode=="duplicate-hello"){channel.Write(hello,After(2000),parent.Get());WaitForSingleObject(parent.Get(),INFINITE);return 0;}
        if(mode=="mid-frame"||mode=="stalled-frame")Raw(pipe.Get(),{100,0,0,0,'{'});
        if(mode=="stalled-frame"){WaitForSingleObject(parent.Get(),INFINITE);return 0;}
        if(mode=="disconnect"||mode=="mid-frame"){pipe.Reset();WaitForSingleObject(parent.Get(),INFINITE);return 0;}
        if(mode.starts_with("catalog-")){
            if(channel.Read(incoming,After(2000))!=IoResult::Complete)return 71;
            if(incoming.type==MessageType::CatalogAccess&&channel.Read(incoming,After(2000))!=IoResult::Complete)return 71;
            if(incoming.type!=MessageType::LoadPlugin)return 71;
            // 故障对端模拟加载成功，仅测试父端授权；不加载任何 DLL。
            // Fault peer simulates load success to test parent authorization without loading a DLL.
            if(channel.Write({MessageType::LoadPluginResult},After(2000))!=IoResult::Complete)return 71;
            Message request{MessageType::DataRequest};request.dataRequest.requestId=1;request.dataRequest.limit=1;
            if(mode=="catalog-spoof")Raw(pipe.Get(),Frame("{\"type\":\"dataRequest\",\"requestId\":1,\"catalog\":\"items\",\"operation\":\"list\",\"stableId\":\"\",\"offset\":0,\"limit\":1,\"pluginId\":\"dev.example.healthy\"}"));
            else if(mode=="catalog-ack"){
                Message ack{MessageType::DataResultAck};ack.dataResult.requestId=99;channel.Write(ack,After(2000));
            }
            else if(mode=="catalog-overflow"){
                // 读取响应但不 ACK，避免先命中管道缓冲超时而非未完成请求上限。
                // Drain replies without ACKs so the test hits outstanding bounds, not pipe-buffer timeout.
                for(unsigned id=1;id<=17;++id){
                    request.dataRequest.requestId=id;if(channel.Write(request,After(2000))!=IoResult::Complete)break;
                    if(id<=16&&(channel.Read(incoming,After(2000))!=IoResult::Complete||incoming.type!=MessageType::DataResult))return 71;
                }
            }else{
                if(channel.Write(request,After(2000))!=IoResult::Complete||channel.Read(incoming,After(2000))!=IoResult::Complete)return 71;
                if(incoming.type!=MessageType::DataResult)return 71;
                if(mode=="catalog-denied"&&(incoming.dataResult.status!=noven::plugins::DataStatus::PermissionDenied||incoming.dataResult.payload.find("\"total\":0")==std::string::npos))return 71;
                if(mode=="catalog-duplicate")channel.Write(request,After(2000));
                else if(mode=="catalog-denied"){
                    Message ack{MessageType::DataResultAck};ack.dataResult.requestId=1;channel.Write(ack,After(2000));
                }
            }
            if(mode!="catalog-denied"){WaitForSingleObject(parent.Get(),INFINITE);return 0;}
        }
        if(mode=="host-ping"){
            if(channel.Write({MessageType::Ping},After(2000))!=IoResult::Complete||channel.Read(incoming,After(2000))!=IoResult::Complete)return 71;
            while(incoming.type==MessageType::Ping){
                channel.Write({MessageType::Pong},After(2000));
                if(channel.Read(incoming,After(2000))!=IoResult::Complete)return 71;
            }
            if(incoming.type==MessageType::Shutdown){channel.Write({MessageType::ShutdownAck},After(2000));return 0;}
            if(incoming.type!=MessageType::Pong)return 71;
        }
        for(;;){
            if(channel.Read(incoming,Deadline::max(),nullptr,parent.Get())!=IoResult::Complete)return 2;
            if(incoming.type==MessageType::Ping&&mode!="silent-pong")channel.Write({MessageType::Pong},After(2000),parent.Get());
            else if(incoming.type==MessageType::Shutdown){
                if(mode=="ignore-shutdown"){WaitForSingleObject(parent.Get(),INFINITE);return 0;}
                channel.Write({MessageType::ShutdownAck},After(2000),parent.Get());return 0;
            }
        }
    }catch(const std::exception&){if(values)LocalFree(values);return 2;}
}
