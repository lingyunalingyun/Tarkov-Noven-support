#include "plugins/PluginPipe.h"
#include "plugins/PluginManifest.h"
#include <bcrypt.h>
#include <sddl.h>
#include <algorithm>
#include <charconv>
#include <stdexcept>

namespace noven::plugins::ipc {
namespace {
[[noreturn]] void Fail(){throw std::runtime_error("first-party host transport failure");}
DWORD Remaining(Deadline deadline){
    if(deadline==Deadline::max())return INFINITE;
    const auto left=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-std::chrono::steady_clock::now()).count();
    return left<=0?0:static_cast<DWORD>(std::min<std::int64_t>(left,MAXDWORD-1));
}
IoResult Await(HANDLE pipe,OVERLAPPED& operation,Deadline deadline,HANDLE interrupt,HANDLE peer,DWORD& transferred){
    // Win32 禁止重复等待句柄；缺少可选信号时缩短数组，而不是复制 I/O 事件。
    // Win32 forbids duplicate wait handles; omit absent optional signals instead of repeating the I/O event.
    std::array<HANDLE,3> events{operation.hEvent};DWORD count=1,interruptIndex=MAXDWORD;
    if(interrupt){interruptIndex=count;events[count++]=interrupt;}
    if(peer&&peer!=interrupt)events[count++]=peer;
    const auto result=WaitForMultipleObjects(count,events.data(),FALSE,Remaining(deadline));
    if(result==WAIT_OBJECT_0)return GetOverlappedResult(pipe,&operation,&transferred,FALSE)?IoResult::Complete:IoResult::Disconnected;
    // 取消后必须收割完成，才能释放 OVERLAPPED 和缓冲区；不再等待对端的协议响应。
    // Drain cancellation before freeing OVERLAPPED/buffers; this no longer waits for a peer protocol response.
    CancelIoEx(pipe,&operation);
    if(GetOverlappedResult(pipe,&operation,&transferred,TRUE))return IoResult::Complete;
    transferred=0;
    if(interruptIndex!=MAXDWORD&&result==WAIT_OBJECT_0+interruptIndex)return IoResult::Interrupted;
    if(result==WAIT_TIMEOUT)return IoResult::Timeout;
    return IoResult::Disconnected;
}
IoResult Transfer(HANDLE pipe,void* bytes,DWORD size,bool write,Deadline deadline,HANDLE interrupt,HANDLE peer,DWORD& transferred){
    if(deadline!=Deadline::max()&&std::chrono::steady_clock::now()>=deadline){transferred=0;return IoResult::Timeout;}
    Handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr));if(!event)Fail();OVERLAPPED operation{};operation.hEvent=event.Get();
    const BOOL done=write?WriteFile(pipe,bytes,size,&transferred,&operation):ReadFile(pipe,bytes,size,&transferred,&operation);
    if(done)return transferred?IoResult::Complete:IoResult::Disconnected;
    if(GetLastError()!=ERROR_IO_PENDING)return IoResult::Disconnected;
    const auto result=Await(pipe,operation,deadline,interrupt,peer,transferred);
    return result==IoResult::Complete&&transferred==0?IoResult::Disconnected:result;
}
std::string Ascii(std::wstring_view text){
    if(text.size()>256)Fail();std::string result;for(const auto c:text){if(c<32||c>126)Fail();result+=static_cast<char>(c);}return result;
}
std::wstring Wide(std::string_view text){return std::wstring(text.begin(),text.end());}
}
Deadline After(DWORD milliseconds){return std::chrono::steady_clock::now()+std::chrono::milliseconds(milliseconds);}
std::string RandomSecret(){
    std::array<UCHAR,32> bytes{};if(BCryptGenRandom(nullptr,bytes.data(),static_cast<ULONG>(bytes.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)Fail();
    constexpr char hex[]="0123456789abcdef";std::string result;result.reserve(64);
    for(const auto byte:bytes){result+=hex[byte>>4];result+=hex[byte&15];}return result;
}
std::wstring PipeName(std::string_view secret){if(!ValidSecret(secret))Fail();return L"\\\\.\\pipe\\Noven.Host."+Wide(secret);}
std::wstring QuoteArgument(std::wstring_view value){
    std::wstring result=L"\"";std::size_t slashes=0;
    for(const auto c:value){if(c==L'\\'){++slashes;continue;}result.append(c==L'"'?2*slashes+1:slashes,L'\\');slashes=0;result+=c;}
    result.append(2*slashes,L'\\');return result+L'"';
}
Handle CreateServer(std::wstring_view pipeName){
    Handle token;HANDLE raw{};if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&raw))Fail();token=Handle(raw);
    DWORD size=0;GetTokenInformation(token.Get(),TokenGroups,nullptr,0,&size);if(size==0||size>64*1024)Fail();
    std::vector<std::uint8_t> data(size);if(!GetTokenInformation(token.Get(),TokenGroups,data.data(),size,&size))Fail();
    const auto groups=reinterpret_cast<const TOKEN_GROUPS*>(data.data());PSID logon=nullptr;
    for(DWORD i=0;i<groups->GroupCount;++i)if((groups->Groups[i].Attributes&SE_GROUP_LOGON_ID)==SE_GROUP_LOGON_ID){logon=groups->Groups[i].Sid;break;}
    if(!logon)Fail();LPWSTR sid=nullptr;if(!ConvertSidToStringSidW(logon,&sid))Fail();
    const auto sddl=L"D:P(A;;GA;;;"+std::wstring(sid)+L")";LocalFree(sid);
    PSECURITY_DESCRIPTOR descriptor=nullptr;if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),SDDL_REVISION_1,&descriptor,nullptr))Fail();
    SECURITY_ATTRIBUTES attributes{sizeof(attributes),descriptor,FALSE};
    Handle server(CreateNamedPipeW(std::wstring(pipeName).c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,4096,4096,0,&attributes));LocalFree(descriptor);
    if(!server)Fail();return server;
}
IoResult ConnectServer(HANDLE pipe,Deadline deadline,HANDLE interrupt,HANDLE peer){
    Handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr));if(!event)Fail();OVERLAPPED operation{};operation.hEvent=event.Get();
    if(ConnectNamedPipe(pipe,&operation))return IoResult::Complete;
    const auto error=GetLastError();if(error==ERROR_PIPE_CONNECTED)return IoResult::Complete;if(error!=ERROR_IO_PENDING)return IoResult::Disconnected;
    DWORD bytes{};return Await(pipe,operation,deadline,interrupt,peer,bytes);
}
Handle ConnectClient(std::wstring_view name,DWORD expectedParent){
    Handle pipe(CreateFileW(std::wstring(name).c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED|SECURITY_SQOS_PRESENT|SECURITY_IDENTIFICATION,nullptr));
    ULONG server{};if(!pipe||!GetNamedPipeServerProcessId(pipe.Get(),&server)||server!=expectedParent)Fail();return pipe;
}
Handle CreateHostJob(){
    Handle job(CreateJobObjectW(nullptr,nullptr));JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(!job||!SetInformationJobObject(job.Get(),JobObjectExtendedLimitInformation,&limits,sizeof(limits)))Fail();return job;
}
HostProcess LaunchHost(const std::filesystem::path& executable,HANDLE job,std::wstring_view pipe,std::string_view pluginId,std::string_view secret){
    if(!executable.is_absolute()||executable.filename()!=L"NovenPluginHost.exe"||!ValidPluginId(pluginId)||!ValidSecret(secret))Fail();
    std::wstring command=QuoteArgument(executable.wstring())+L" --pipe "+QuoteArgument(pipe)+L" --plugin-id "+QuoteArgument(Wide(pluginId))+
        L" --session "+QuoteArgument(Wide(secret))+L" --protocol 1 --parent "+std::to_wstring(GetCurrentProcessId());
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED|CREATE_NO_WINDOW,nullptr,
        executable.parent_path().c_str(),&startup,&process))Fail();
    Handle owner(process.hProcess),thread(process.hThread);
    // 先挂起创建、加入父进程 Job，再恢复；清单不能指定可执行文件或参数。
    // Create suspended, assign the parent's Job, then resume; manifests cannot select executable paths/arguments.
    if(!AssignProcessToJobObject(job,owner.Get())||ResumeThread(thread.Get())==static_cast<DWORD>(-1)){
        TerminateProcess(owner.Get(),1);WaitForSingleObject(owner.Get(),2000);Fail();
    }
    return {std::move(owner),process.dwProcessId};
}
HostArguments ParseHostArguments(int count,wchar_t** arguments){
    if(count!=11)Fail();HostArguments result;bool pipe=false,id=false,session=false,protocol=false,parent=false;
    for(int i=1;i<count;i+=2){
        const std::wstring_view key(arguments[i]),value(arguments[i+1]);
        if(key==L"--pipe"&&!pipe){result.pipe=value;pipe=true;}
        else if(key==L"--plugin-id"&&!id){result.pluginId=Ascii(value);id=true;}
        else if(key==L"--session"&&!session){result.secret=Ascii(value);session=true;}
        else if(key==L"--protocol"&&!protocol){if(value!=L"1")Fail();protocol=true;}
        else if(key==L"--parent"&&!parent){const auto text=Ascii(value);const auto parsed=std::from_chars(text.data(),text.data()+text.size(),result.parent);if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size()||result.parent==0)Fail();parent=true;}
        else Fail();
    }
    constexpr std::wstring_view prefix=L"\\\\.\\pipe\\Noven.Host.";
    if(!pipe||!id||!session||!protocol||!parent||!ValidPluginId(result.pluginId)||!ValidSecret(result.secret)||
        !result.pipe.starts_with(prefix)||!ValidSecret(Ascii(std::wstring_view(result.pipe).substr(prefix.size()))))Fail();
    return result;
}
IoResult Channel::Read(Message& message,Deadline deadline,HANDLE interrupt,HANDLE peer){
    while(headerOffset_<header_.size()){
        DWORD bytes=0;const auto result=Transfer(pipe_,header_.data()+headerOffset_,static_cast<DWORD>(header_.size()-headerOffset_),false,std::min(deadline,frameDeadline_),interrupt,peer,bytes);
        if(bytes&&frameDeadline_==Deadline::max())frameDeadline_=After(5000);
        headerOffset_+=bytes;if(result!=IoResult::Complete)return result;
    }
    if(payload_.empty()){
        std::uint32_t length=0;for(unsigned i=0;i<4;++i)length|=static_cast<std::uint32_t>(header_[i])<<(8*i);
        if(length==0||length>MaximumFrameBytes)Fail();payload_.resize(length);
    }
    while(payloadOffset_<payload_.size()){
        DWORD bytes=0;const auto result=Transfer(pipe_,payload_.data()+payloadOffset_,static_cast<DWORD>(payload_.size()-payloadOffset_),false,std::min(deadline,frameDeadline_),interrupt,peer,bytes);
        payloadOffset_+=bytes;if(result!=IoResult::Complete)return result;
    }
    message=ParseMessage(payload_);headerOffset_=payloadOffset_=0;payload_.clear();frameDeadline_=Deadline::max();return IoResult::Complete;
}
IoResult Channel::Write(const Message& message,Deadline deadline,HANDLE peer){
    auto frame=Frame(Serialize(message));std::size_t offset=0;
    while(offset<frame.size()){
        DWORD bytes=0;const auto result=Transfer(pipe_,frame.data()+offset,static_cast<DWORD>(frame.size()-offset),true,deadline,nullptr,peer,bytes);
        offset+=bytes;if(result!=IoResult::Complete)return result;
    }
    return IoResult::Complete;
}
int RunHost(const HostArguments& arguments){
    try{
        Handle parent(OpenProcess(SYNCHRONIZE,FALSE,arguments.parent));if(!parent)return 2;
        auto pipe=ConnectClient(arguments.pipe,arguments.parent);Channel channel(pipe.Get());
        Message hello{MessageType::Hello,TransportProtocolVersion,arguments.pluginId,arguments.secret};
        if(channel.Write(hello,After(5000),parent.Get())!=IoResult::Complete)return 2;
        Message incoming;
        if(channel.Read(incoming,After(5000),nullptr,parent.Get())!=IoResult::Complete)return 2;
        if(incoming.type==MessageType::Shutdown)return channel.Write({MessageType::ShutdownAck},After(2000),parent.Get())==IoResult::Complete?0:2;
        if(incoming.type!=MessageType::HelloAck||!MatchesSession(incoming,arguments.pluginId,arguments.secret)){
            channel.Write({MessageType::ProtocolError},After(1000),parent.Get());return 3;
        }
        for(;;){
            if(channel.Read(incoming,Deadline::max(),nullptr,parent.Get())!=IoResult::Complete)return 2;
            if(incoming.type==MessageType::Ping){if(channel.Write({MessageType::Pong},After(2000),parent.Get())!=IoResult::Complete)return 2;}
            else if(incoming.type==MessageType::Shutdown)return channel.Write({MessageType::ShutdownAck},After(2000),parent.Get())==IoResult::Complete?0:2;
            else {channel.Write({MessageType::ProtocolError},After(1000),parent.Get());return 3;}
        }
    }catch(const std::exception&){return 3;}
}
}
