#include "plugins/PluginRuntimeManager.h"
#include "plugins/PluginPipe.h"
#include <condition_variable>
#include <map>
#include <mutex>
#include <thread>

namespace noven::plugins {
using namespace ipc;
bool Terminal(HostState state){return state==HostState::Stopped||state==HostState::Exited||state==HostState::Crashed||state==HostState::ProtocolError;}
namespace {
std::filesystem::path ExecutableDirectory(){
    std::wstring path(32768,L'\0');const auto length=GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));
    if(!length||length==path.size())throw std::runtime_error("cannot resolve first-party runtime directory");
    path.resize(length);return std::filesystem::path(path).parent_path();
}
struct Failure final {HostState state;HostError error;};
struct Session final {
    mutable std::mutex mutex;
    mutable std::condition_variable changed;
    HostSnapshot snapshot;
    Handle wake{CreateEventW(nullptr,TRUE,FALSE,nullptr)};
    bool stopping{},ping{},awaitingPong{};
    // 最后声明，先 join 后释放事件、锁和快照；没有 detached 线程。
    // Declared last so it joins before events/mutex/snapshot are destroyed; no detached threads.
    std::jthread worker;
    void Publish(HostState state,HostError error=HostError::None){std::lock_guard lock(mutex);snapshot.state=state;snapshot.error=error;changed.notify_all();}
};
void Require(IoResult result,HostError timeout,Session& session){
    if(result==IoResult::Complete)return;
    if(result==IoResult::Interrupted){std::lock_guard lock(session.mutex);if(session.stopping)throw Failure{HostState::Stopped,HostError::None};}
    throw Failure{HostState::Crashed,result==IoResult::Timeout?timeout:HostError::Disconnected};
}
void Run(Session& session,const std::filesystem::path& host){
    Handle job,pipe;HostProcess process;HostState finalState=HostState::Exited;HostError finalError=HostError::None;
    try{
        {std::lock_guard lock(session.mutex);if(session.stopping)throw Failure{HostState::Stopped,HostError::None};}
        const auto secret=RandomSecret(),name=RandomSecret();const auto pipeName=PipeName(name);
        pipe=CreateServer(pipeName);job=CreateHostJob();
        process=LaunchHost(host,job.Get(),pipeName,session.snapshot.pluginId,secret);
        {std::lock_guard lock(session.mutex);session.snapshot.processId=process.id;session.snapshot.jobOwned=true;}
        session.Publish(HostState::Connecting);
        Require(ConnectServer(pipe.Get(),After(5000),session.wake.Get(),process.process.Get()),HostError::ConnectionTimeout,session);
        ULONG peer{};if(!GetNamedPipeClientProcessId(pipe.Get(),&peer)||peer!=process.id)throw Failure{HostState::ProtocolError,HostError::PeerMismatch};
        session.Publish(HostState::Handshaking);Channel channel(pipe.Get());Message incoming;
        Require(channel.Read(incoming,After(5000),session.wake.Get(),process.process.Get()),HostError::HandshakeTimeout,session);
        if(incoming.type!=MessageType::Hello||!MatchesSession(incoming,session.snapshot.pluginId,secret))throw Failure{HostState::ProtocolError,HostError::HandshakeMismatch};
        incoming.type=MessageType::HelloAck;Require(channel.Write(incoming,After(2000),process.process.Get()),HostError::HandshakeTimeout,session);
        session.Publish(HostState::Ready);Deadline pongDeadline=Deadline::max();
        for(;;){
            bool stop=false,ping=false;
            {std::lock_guard lock(session.mutex);stop=session.stopping;ping=std::exchange(session.ping,false);ResetEvent(session.wake.Get());}
            if(stop){
                session.Publish(HostState::Stopping);const auto deadline=After(2000);
                auto result=channel.Write({MessageType::Shutdown},deadline,process.process.Get());
                if(result==IoResult::Complete)result=channel.Read(incoming,deadline,nullptr,process.process.Get());
                // 在停止之前排队的 pong/ping 不得误判为关闭确认。
                // Queued pong/ping before stopping must not be mistaken for shutdown acknowledgement.
                while(result==IoResult::Complete&&(incoming.type==MessageType::Pong||incoming.type==MessageType::Ping)){
                    if(incoming.type==MessageType::Ping)result=channel.Write({MessageType::Pong},deadline,process.process.Get());
                    if(result==IoResult::Complete)result=channel.Read(incoming,deadline,nullptr,process.process.Get());
                }
                if(result!=IoResult::Complete||incoming.type!=MessageType::ShutdownAck)throw Failure{HostState::Crashed,HostError::ShutdownTimeout};
                {std::lock_guard lock(session.mutex);session.snapshot.shutdownAcknowledged=true;}
                if(WaitForSingleObject(process.process.Get(),2000)!=WAIT_OBJECT_0)throw Failure{HostState::Crashed,HostError::ShutdownTimeout};
                break;
            }
            if(ping){Require(channel.Write({MessageType::Ping},After(2000),process.process.Get()),HostError::PingTimeout,session);pongDeadline=After(3000);}
            const auto result=channel.Read(incoming,pongDeadline,session.wake.Get(),process.process.Get());
            if(result==IoResult::Interrupted)continue;
            if(result==IoResult::Disconnected){
                if(WaitForSingleObject(process.process.Get(),100)==WAIT_OBJECT_0){DWORD code{};GetExitCodeProcess(process.process.Get(),&code);finalState=code==0?HostState::Exited:HostState::Crashed;finalError=code==0?HostError::None:HostError::Disconnected;break;}
                throw Failure{HostState::Crashed,HostError::Disconnected};
            }
            Require(result,pongDeadline==Deadline::max()?HostError::FrameTimeout:HostError::PingTimeout,session);
            if(incoming.type==MessageType::Ping)Require(channel.Write({MessageType::Pong},After(2000),process.process.Get()),HostError::Disconnected,session);
            else if(incoming.type==MessageType::Pong){
                std::lock_guard lock(session.mutex);if(!session.awaitingPong)throw Failure{HostState::ProtocolError,HostError::InvalidProtocol};
                session.awaitingPong=false;++session.snapshot.pongs;pongDeadline=Deadline::max();session.changed.notify_all();
            }else throw Failure{HostState::ProtocolError,HostError::InvalidProtocol};
        }
    }catch(const Failure& failure){finalState=failure.state;finalError=failure.error;}
    catch(const std::exception&){finalState=process.process?HostState::ProtocolError:HostState::Crashed;finalError=process.process?HostError::InvalidProtocol:HostError::Startup;}
    // 终态只在句柄与子进程收束后发布；失败不影响其他会话，不自动重启。
    // Publish terminal state only after child/handle cleanup; failures do not affect other sessions or auto-restart.
    pipe.Reset();job.Reset();
    if(process.process&&WaitForSingleObject(process.process.Get(),2000)!=WAIT_OBJECT_0){TerminateProcess(process.process.Get(),1);WaitForSingleObject(process.process.Get(),2000);finalState=HostState::Crashed;finalError=HostError::ShutdownTimeout;}
    process.process.Reset();session.Publish(finalState,finalError);
}
}
struct PluginRuntimeManager::Impl final {
    explicit Impl(std::filesystem::path directory):host(std::filesystem::absolute(std::move(directory))/L"NovenPluginHost.exe"){}
    std::filesystem::path host;
    mutable std::mutex mutex;
    std::map<std::string,std::unique_ptr<Session>,std::less<>> sessions;
    Session* Find(std::string_view id) const {const auto found=sessions.find(id);return found==sessions.end()?nullptr:found->second.get();}
    template<class Predicate> bool Wait(std::string_view id,DWORD milliseconds,Predicate predicate) const {
        Session* session;{std::lock_guard lock(mutex);session=Find(id);}if(!session)return false;
        std::unique_lock lock(session->mutex);
        session->changed.wait_for(lock,std::chrono::milliseconds(milliseconds),[&]{return predicate(session->snapshot)||Terminal(session->snapshot.state);});
        return predicate(session->snapshot);
    }
};
PluginRuntimeManager::PluginRuntimeManager():PluginRuntimeManager(ExecutableDirectory()){}
PluginRuntimeManager::PluginRuntimeManager(std::filesystem::path directory):impl_(std::make_unique<Impl>(std::move(directory))){}
PluginRuntimeManager::~PluginRuntimeManager(){
    for(auto& [id,session]:impl_->sessions){std::lock_guard lock(session->mutex);session->stopping=true;SetEvent(session->wake.Get());}
    for(auto& [id,session]:impl_->sessions)if(session->worker.joinable())session->worker.join();
}
bool PluginRuntimeManager::Start(const PluginRecord& record){
    if(record.state!=PluginState::Valid||!record.manifest||!ValidPluginId(record.manifest->id)||record.manifest->manifestVersion!=1||record.manifest->apiVersion!=1)return false;
    std::lock_guard lock(impl_->mutex);const auto& id=record.manifest->id;
    if(impl_->sessions.contains(id)||impl_->sessions.size()>=16)return false;
    auto session=std::make_unique<Session>();if(!session->wake)return false;
    session->snapshot.pluginId=id;session->snapshot.state=HostState::Starting;session->snapshot.startedAt=std::chrono::steady_clock::now();
    auto* owned=session.get();impl_->sessions.emplace(id,std::move(session));
    try{owned->worker=std::jthread([owned,host=impl_->host]{Run(*owned,host);});}
    catch(const std::exception&){impl_->sessions.erase(id);return false;}return true;
}
bool PluginRuntimeManager::Ping(std::string_view id){
    std::lock_guard lock(impl_->mutex);auto* session=impl_->Find(id);if(!session)return false;std::lock_guard stateLock(session->mutex);
    if(session->snapshot.state!=HostState::Ready||session->awaitingPong||session->stopping)return false;
    session->ping=true;session->awaitingPong=true;SetEvent(session->wake.Get());return true;
}
bool PluginRuntimeManager::Stop(std::string_view id){
    std::lock_guard lock(impl_->mutex);auto* session=impl_->Find(id);if(!session)return false;std::lock_guard stateLock(session->mutex);
    if(Terminal(session->snapshot.state))return false;session->stopping=true;SetEvent(session->wake.Get());return true;
}
std::optional<HostSnapshot> PluginRuntimeManager::Snapshot(std::string_view id) const {
    std::lock_guard lock(impl_->mutex);auto* session=impl_->Find(id);if(!session)return std::nullopt;std::lock_guard stateLock(session->mutex);return session->snapshot;
}
std::size_t PluginRuntimeManager::SessionCount() const {std::lock_guard lock(impl_->mutex);return impl_->sessions.size();}
const std::filesystem::path& PluginRuntimeManager::HostPath() const {return impl_->host;}
bool PluginRuntimeManager::WaitFor(std::string_view id,HostState state,DWORD timeout) const {return impl_->Wait(id,timeout,[&](const auto& s){return s.state==state;});}
bool PluginRuntimeManager::WaitForTerminal(std::string_view id,DWORD timeout) const {return impl_->Wait(id,timeout,[](const auto& s){return Terminal(s.state);});}
bool PluginRuntimeManager::WaitForPong(std::string_view id,std::uint64_t count,DWORD timeout) const {return impl_->Wait(id,timeout,[&](const auto& s){return s.pongs>=count;});}
}
