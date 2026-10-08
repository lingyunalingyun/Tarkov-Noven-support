#include "plugins/PluginRuntimeManager.h"
#include "plugins/PluginPipe.h"
#include "plugins/PluginNativePath.h"
#include <algorithm>
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
    std::optional<Message> load,action;
    std::optional<NativeFile> nativeFile;
    bool awaitingAction{};
    std::function<void()> notify;
    std::shared_ptr<CatalogPluginService> catalog;
    CatalogGrants catalogGrants;
    DataRequestBudget dataBudget{32};
    std::map<std::uint64_t,Deadline> dataAcks;
    // 最后声明，先 join 后释放事件、锁和快照；没有 detached 线程。
    // Declared last so it joins before events/mutex/snapshot are destroyed; no detached threads.
    std::jthread worker;
    void Notify(){changed.notify_all();if(notify)notify();}
    void Publish(HostState state,HostError error=HostError::None){
        {std::lock_guard lock(mutex);snapshot.state=state;snapshot.error=error;if(Terminal(state)){snapshot.pages.clear();snapshot.pendingData=0;}}Notify();
    }
};
bool AcceptNative(Session& session,const Message& message,unsigned& logs,unsigned& updates,Deadline& burst){
    if(!session.load)return false;
    if(std::chrono::steady_clock::now()>=burst){logs=updates=0;burst=After(1000);}
    {
        std::lock_guard lock(session.mutex);
        if(session.stopping)return true;
        // 两端窗口起点不同，接收端允许相邻两个发送窗口的突发，仍有硬上限。
        // Receiver tolerates adjacent sender windows with different origins, while retaining a hard cap.
        if(message.type==MessageType::Log){if(++logs>32)throw Failure{HostState::ProtocolError,HostError::InvalidProtocol};session.snapshot.lastLog=message.text;}
        else if(message.type==MessageType::UiRegisterPage){
            if(!session.load->pagePermission||session.snapshot.pages.size()>=MaximumPluginPages
                ||std::any_of(session.snapshot.pages.begin(),session.snapshot.pages.end(),[&](const auto& page){return page.localId==message.pageId;}))throw Failure{HostState::ProtocolError,HostError::InvalidProtocol};
            session.snapshot.pages.push_back({message.pageId,message.title,{}});
        }else if(message.type==MessageType::UiPublishPage){
            const auto page=std::find_if(session.snapshot.pages.begin(),session.snapshot.pages.end(),[&](const auto& item){return item.localId==message.pageId;});
            if(!session.load->pagePermission||page==session.snapshot.pages.end()||++updates>64)throw Failure{HostState::ProtocolError,HostError::InvalidProtocol};
            page->document=ParseUiDocument(message.document);
        }else return false;
    }
    session.Notify();return true;
}
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
        Deadline pongDeadline=Deadline::max(),actionDeadline=Deadline::max(),loadDeadline=Deadline::max(),burst=After(1000);unsigned logs=0,updates=0;
        if(session.load){
            session.Publish(HostState::Loading);Message access{MessageType::CatalogAccess};
            for(const auto kind:{CatalogKind::Items,CatalogKind::Tasks,CatalogKind::Maps})if(session.catalogGrants.Allows(kind))access.catalogMask|=1u<<(static_cast<unsigned>(kind)-1);
            if(access.catalogMask)Require(channel.Write(access,After(2000),process.process.Get()),HostError::LoadTimeout,session);
            Require(channel.Write(*session.load,After(2000),process.process.Get()),HostError::LoadTimeout,session);loadDeadline=After(5000);
        }
        else session.Publish(HostState::Ready);
        for(;;){
            bool stop=false,ping=false;std::optional<Message> action;
            {std::lock_guard lock(session.mutex);stop=session.stopping;ping=std::exchange(session.ping,false);action=std::move(session.action);session.action.reset();ResetEvent(session.wake.Get());}
            if(stop){
                session.dataAcks.clear();session.dataBudget.Clear();
                session.Publish(HostState::Stopping);const auto deadline=After(2000);
                auto result=channel.Write({MessageType::Shutdown},deadline,process.process.Get());
                if(result==IoResult::Complete)result=channel.Read(incoming,deadline,nullptr,process.process.Get());
                // 在停止之前排队的 pong/ping 不得误判为关闭确认。
                // Queued pong/ping before stopping must not be mistaken for shutdown acknowledgement.
                while(result==IoResult::Complete&&(incoming.type==MessageType::Pong||incoming.type==MessageType::Ping
                    ||(session.load&&(incoming.type==MessageType::Log||incoming.type==MessageType::UiRegisterPage||incoming.type==MessageType::UiPublishPage||incoming.type==MessageType::UiActionResult||incoming.type==MessageType::LoadPluginResult||incoming.type==MessageType::DataRequest||incoming.type==MessageType::DataResultAck)))){
                    if(incoming.type==MessageType::Ping)result=channel.Write({MessageType::Pong},deadline,process.process.Get());
                    if(result==IoResult::Complete)result=channel.Read(incoming,deadline,nullptr,process.process.Get());
                }
                if(result!=IoResult::Complete||incoming.type!=MessageType::ShutdownAck)throw Failure{HostState::Crashed,HostError::ShutdownTimeout};
                {std::lock_guard lock(session.mutex);session.snapshot.shutdownAcknowledged=true;}
                if(WaitForSingleObject(process.process.Get(),2000)!=WAIT_OBJECT_0)throw Failure{HostState::Crashed,HostError::ShutdownTimeout};
                break;
            }
            if(ping){Require(channel.Write({MessageType::Ping},After(2000),process.process.Get()),HostError::PingTimeout,session);pongDeadline=After(3000);}
            if(action){Require(channel.Write(*action,After(2000),process.process.Get()),HostError::ActionTimeout,session);actionDeadline=After(3000);}
            auto dataDeadline=Deadline::max();for(const auto& [id,deadline]:session.dataAcks)dataDeadline=std::min(dataDeadline,deadline);
            const auto deadline=std::min({pongDeadline,loadDeadline,actionDeadline,dataDeadline});
            const auto result=channel.Read(incoming,deadline,session.wake.Get(),process.process.Get());
            if(result==IoResult::Interrupted)continue;
            if(result==IoResult::Disconnected){
                if(WaitForSingleObject(process.process.Get(),100)==WAIT_OBJECT_0){DWORD code{};GetExitCodeProcess(process.process.Get(),&code);finalState=code==0?HostState::Exited:HostState::Crashed;finalError=code==0?HostError::None:HostError::Disconnected;break;}
                throw Failure{HostState::Crashed,HostError::Disconnected};
            }
            Require(result,dataDeadline!=Deadline::max()&&deadline==dataDeadline?HostError::DataTimeout:deadline==loadDeadline&&loadDeadline!=Deadline::max()?HostError::LoadTimeout:deadline==actionDeadline&&actionDeadline!=Deadline::max()?HostError::ActionTimeout:pongDeadline==Deadline::max()?HostError::FrameTimeout:HostError::PingTimeout,session);
            if(incoming.type==MessageType::DataRequest&&session.load&&loadDeadline==Deadline::max()){
                {std::lock_guard lock(session.mutex);if(session.stopping)continue;}
                // 授权来自当前认证会话，不信任 Host 位掩码；原生插件能破坏 Host 内存但不能借用别的会话。
                // Authorize from the authenticated session, not Host mask; native code cannot borrow another session's grants.
                if(session.dataBudget.Begin(incoming.dataRequest.requestId)!=RequestAdmission::Accepted)throw Failure{HostState::ProtocolError,HostError::InvalidProtocol};
                Message reply{MessageType::DataResult};
                reply.dataResult=session.catalog?session.catalog->Query(incoming.dataRequest,session.catalogGrants)
                    :CatalogPluginService::Error(incoming.dataRequest,session.catalogGrants.Allows(incoming.dataRequest.catalog)?DataStatus::Unavailable:DataStatus::PermissionDenied);
                {std::lock_guard lock(session.mutex);if(session.stopping){session.dataBudget.Complete(incoming.dataRequest.requestId);continue;}}
                Require(channel.Write(reply,After(2000),process.process.Get()),HostError::DataTimeout,session);
                session.dataAcks.emplace(incoming.dataRequest.requestId,After(3000));
                {std::lock_guard lock(session.mutex);if(!session.stopping)session.snapshot.pendingData=session.dataAcks.size();}session.Notify();continue;
            }
            if(incoming.type==MessageType::DataResultAck&&session.load){
                if(!session.dataAcks.erase(incoming.dataResult.requestId)||!session.dataBudget.Complete(incoming.dataResult.requestId))throw Failure{HostState::ProtocolError,HostError::InvalidProtocol};
                {std::lock_guard lock(session.mutex);if(!session.stopping){++session.snapshot.dataResults;session.snapshot.pendingData=session.dataAcks.size();}}session.Notify();continue;
            }
            if(AcceptNative(session,incoming,logs,updates,burst))continue;
            if(incoming.type==MessageType::LoadPluginResult&&session.load&&loadDeadline!=Deadline::max()){
                {std::lock_guard lock(session.mutex);session.snapshot.loadResult=incoming.result;}
                if(incoming.result!=0)throw Failure{HostState::Crashed,HostError::LoadFailed};
                loadDeadline=Deadline::max();session.Publish(HostState::Running);continue;
            }
            if(incoming.type==MessageType::UiActionResult&&session.load){
                {std::lock_guard lock(session.mutex);if(!session.awaitingAction||actionDeadline==Deadline::max()||incoming.result!=0)throw Failure{HostState::ProtocolError,HostError::InvalidProtocol};session.awaitingAction=false;}
                actionDeadline=Deadline::max();session.Notify();continue;
            }
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
    process.process.Reset();session.nativeFile.reset();session.dataAcks.clear();session.dataBudget.Clear();session.Publish(finalState,finalError);
}
}
struct PluginRuntimeManager::Impl final {
    explicit Impl(std::filesystem::path directory):host(std::filesystem::absolute(std::move(directory))/L"NovenPluginHost.exe"){}
    std::filesystem::path host;
    mutable std::mutex mutex;
    std::function<void()> notify;
    std::shared_ptr<CatalogPluginService> catalog;
    std::uint64_t nextGeneration{};
    std::map<std::string,std::shared_ptr<Session>,std::less<>> sessions;
    std::shared_ptr<Session> Find(std::string_view id) const {const auto found=sessions.find(id);return found==sessions.end()?nullptr:found->second;}
    template<class Predicate> bool Wait(std::string_view id,DWORD milliseconds,Predicate predicate) const {
        std::shared_ptr<Session> session;{std::lock_guard lock(mutex);session=Find(id);}if(!session)return false;
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
    auto session=std::make_shared<Session>();if(!session->wake)return false;session->notify=impl_->notify;
    session->snapshot.pluginId=id;session->snapshot.state=HostState::Starting;session->snapshot.startedAt=std::chrono::steady_clock::now();
    auto* owned=session.get();impl_->sessions.emplace(id,std::move(session));
    try{owned->worker=std::jthread([owned,host=impl_->host]{Run(*owned,host);});}
    catch(const std::exception&){impl_->sessions.erase(id);return false;}return true;
}
bool PluginRuntimeManager::Ping(std::string_view id){
    std::lock_guard lock(impl_->mutex);auto session=impl_->Find(id);if(!session)return false;std::lock_guard stateLock(session->mutex);
    if((session->snapshot.state!=HostState::Ready&&session->snapshot.state!=HostState::Running)||session->awaitingPong||session->stopping)return false;
    session->ping=true;session->awaitingPong=true;SetEvent(session->wake.Get());return true;
}
bool PluginRuntimeManager::Stop(std::string_view id){
    std::shared_ptr<Session> session;{std::lock_guard lock(impl_->mutex);session=impl_->Find(id);}if(!session)return false;
    {std::lock_guard stateLock(session->mutex);if(Terminal(session->snapshot.state))return false;session->stopping=true;session->snapshot.pages.clear();session->snapshot.pendingData=0;SetEvent(session->wake.Get());}session->Notify();return true;
}
std::optional<HostSnapshot> PluginRuntimeManager::Snapshot(std::string_view id) const {
    std::lock_guard lock(impl_->mutex);auto session=impl_->Find(id);if(!session)return std::nullopt;std::lock_guard stateLock(session->mutex);return session->snapshot;
}
void PluginRuntimeManager::SetChangeHandler(std::function<void()> handler){std::lock_guard lock(impl_->mutex);impl_->notify=std::move(handler);}
void PluginRuntimeManager::SetCatalogService(std::shared_ptr<CatalogPluginService> service){std::lock_guard lock(impl_->mutex);impl_->catalog=std::move(service);}
void PluginRuntimeManager::SetCatalogLocale(std::string_view locale){std::lock_guard lock(impl_->mutex);if(impl_->catalog)impl_->catalog->SetLocale(locale);}
std::vector<HostSnapshot> PluginRuntimeManager::Snapshots() const {
    std::vector<HostSnapshot> result;std::lock_guard lock(impl_->mutex);
    for(const auto& [id,session]:impl_->sessions){std::lock_guard stateLock(session->mutex);result.push_back(session->snapshot);}return result;
}
bool PluginRuntimeManager::StartNative(const PluginRecord& record,const PluginStateStore& grants){
    if(record.state!=PluginState::Valid||!record.manifest||!grants.Authorized(*record.manifest))return false;
    std::optional<NativeFile> file;try{file=NativeFile::Open(impl_->host.parent_path()/L"plugins",record.directory,record.manifest->runtime->entry);}catch(const std::exception&){return false;}
    std::unique_lock lock(impl_->mutex);const auto& id=record.manifest->id;
    if(auto previous=impl_->Find(id)){
        {std::lock_guard stateLock(previous->mutex);if(!Terminal(previous->snapshot.state))return false;}
        lock.unlock();if(previous->worker.joinable())previous->worker.join();lock.lock();
        if(impl_->Find(id)!=previous)return false;impl_->sessions.erase(id);
    }
    if(impl_->sessions.size()>=16)return false;
    auto session=std::make_shared<Session>();if(!session->wake)return false;
    session->notify=impl_->notify;session->nativeFile=std::move(file);session->snapshot.pluginId=id;session->snapshot.state=HostState::Starting;
    session->catalog=impl_->catalog;session->catalogGrants={true,record.manifest->requestedPermissions,grants.Intent(id).grantedPermissions};
    session->snapshot.generation=++impl_->nextGeneration;session->snapshot.startedAt=std::chrono::steady_clock::now();
    Message load{MessageType::LoadPlugin};const auto directory=std::filesystem::absolute(record.directory).u8string();load.directory.assign(directory.begin(),directory.end());load.entry=record.manifest->runtime->entry;
    load.pagePermission=std::find(record.manifest->requestedPermissions.begin(),record.manifest->requestedPermissions.end(),"ui.page.register")!=record.manifest->requestedPermissions.end();session->load=std::move(load);
    auto* owned=session.get();impl_->sessions.emplace(id,std::move(session));
    try{owned->worker=std::jthread([owned,host=impl_->host]{Run(*owned,host);});}catch(const std::exception&){impl_->sessions.erase(id);return false;}return true;
}
bool PluginRuntimeManager::Action(std::string_view id,std::uint64_t generation,std::string_view pageId,std::string_view actionId){
    if(!ValidLocalId(pageId)||!ValidLocalId(actionId))return false;
    std::lock_guard lock(impl_->mutex);auto session=impl_->Find(id);if(!session)return false;std::lock_guard stateLock(session->mutex);
    if(session->snapshot.generation!=generation||session->snapshot.state!=HostState::Running||session->stopping||session->awaitingAction||!session->load||!session->load->pagePermission)return false;
    const auto page=std::find_if(session->snapshot.pages.begin(),session->snapshot.pages.end(),[&](const auto& item){return item.localId==pageId;});
    if(page==session->snapshot.pages.end()||!page->document.HasAction(actionId))return false;
    Message message{MessageType::UiAction};message.pageId=pageId;message.actionId=actionId;session->action=std::move(message);session->awaitingAction=true;SetEvent(session->wake.Get());return true;
}
std::size_t PluginRuntimeManager::SessionCount() const {std::lock_guard lock(impl_->mutex);return impl_->sessions.size();}
const std::filesystem::path& PluginRuntimeManager::HostPath() const {return impl_->host;}
bool PluginRuntimeManager::WaitFor(std::string_view id,HostState state,DWORD timeout) const {return impl_->Wait(id,timeout,[&](const auto& s){return s.state==state;});}
bool PluginRuntimeManager::WaitForTerminal(std::string_view id,DWORD timeout) const {return impl_->Wait(id,timeout,[](const auto& s){return Terminal(s.state);});}
bool PluginRuntimeManager::WaitForPong(std::string_view id,std::uint64_t count,DWORD timeout) const {return impl_->Wait(id,timeout,[&](const auto& s){return s.pongs>=count;});}
}
