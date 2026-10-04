#include "raid/LocalRaidService.h"
#include <algorithm>
#include <deque>
#include <cwctype>
#include <fstream>
#include <map>
#include <stdexcept>

namespace noven::raid {
namespace {
bool Supported(const std::filesystem::path& path) {
    const auto name=path.filename().wstring();
    for(const auto* token:{L" application_",L" backend_"}) {
        const auto pos=name.rfind(token);if(pos==name.npos)continue;
        const auto suffix=name.substr(pos+std::wstring_view(token).size());
        if(suffix.size()==7&&suffix.substr(3)==L".log"
            &&suffix.find_first_not_of(L"0123456789",0)>2)return true;
    }
    return false;
}
bool IsDirectory(const std::filesystem::directory_entry& entry) {
    return entry.is_directory() && !(GetFileAttributesW(entry.path().c_str()) & FILE_ATTRIBUTE_REPARSE_POINT);
}
bool Within(const std::filesystem::path& path,const std::filesystem::path& root) {
    auto a=std::filesystem::weakly_canonical(path).wstring();auto b=std::filesystem::weakly_canonical(root).wstring();
    std::transform(a.begin(),a.end(),a.begin(),[](wchar_t c){return static_cast<wchar_t>(std::towlower(c));});
    std::transform(b.begin(),b.end(),b.begin(),[](wchar_t c){return static_cast<wchar_t>(std::towlower(c));});
    return a==b||(a.starts_with(b)&&a.size()>b.size()&&(a[b.size()]=='\\'||a[b.size()]=='/'));
}
}
std::optional<std::filesystem::path> ReadEftLogRoot(const std::filesystem::path& config) {
    std::ifstream file(config,std::ios::binary);if(!file)return {};
    std::string text;char c{};
    while(file.get(c)) {if(text.size()==32768)return {};text+=c;}
    if(text.starts_with("\xEF\xBB\xBF"))text.erase(0,3);
    while(!text.empty()&&(text.back()=='\r'||text.back()=='\n'))text.pop_back();
    if(text.empty()||text.find_first_of("\0\r\n",0,3)!=text.npos
        ||!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0))return {};
    auto path=std::filesystem::path(std::u8string(text.begin(),text.end()));
    if(!path.is_absolute())return {};return path;
}
struct LocalRaidService::Pipeline {
    struct Source {std::unique_ptr<EftLogReader> reader;std::deque<RaidEvent> heads;};
    RaidSessionStore store;RaidSessionDetector detector;RaidCheckpoint checkpoint;
    std::map<std::filesystem::path,Source> sources;
    RaidServiceStatus stats;
    bool Fill(Source& source,std::stop_token stop) {
        std::string line;std::uint64_t offset{};
        while(source.heads.empty()&&!stop.stop_requested()) {
            const auto before=source.reader->BytesRead();
            const auto result=source.reader->NextLine(line,offset);stats.bytesRead+=source.reader->BytesRead()-before;
            if(result==ReadResult::End)return false;
            if(result==ReadResult::Error)throw std::runtime_error("EFT source read failed");
            if(result==ReadResult::Skipped) {++stats.ignoredLines;continue;}
            auto events=ParseRaidEvents(line,source.reader->SourceIdentity(),offset);
            if(events.empty())++stats.ignoredLines;
            for(auto& event:events) {
                // 跨文件合并必须有真实日志时间；没有时间的行不猜测事件顺序。
                // Cross-file merge requires a real log timestamp; never guess ordering for untimed lines.
                if(event.time)source.heads.push_back(std::move(event));else ++stats.ignoredLines;
            }
        }
        return !source.heads.empty();
    }
    void Save() {
        checkpoint.detector=detector.Snapshot();
        for(const auto& [path,source]:sources) {
            auto cursor=source.reader->Cursor();
            // 预读但尚未应用的事件不推进持久游标；崩溃后必须能重放该完整行。
            // Prefetched, unapplied events cannot advance durable cursors; crashes must replay that line.
            if(!source.heads.empty())cursor.offset=source.heads.front().offset;
            auto it=std::find_if(checkpoint.cursors.begin(),checkpoint.cursors.end(),[&](const auto& c){return c.path==path;});
            if(it==checkpoint.cursors.end())checkpoint.cursors.push_back(std::move(cursor));else *it=std::move(cursor);
        }
        std::string error;if(!store.Save(checkpoint,error))throw std::runtime_error(error);
    }
    void Drain(const std::filesystem::path& root,std::stop_token stop) {
        ++stats.directoryPasses;
        std::vector<std::filesystem::path> groups;
        for(const auto& entry:std::filesystem::directory_iterator(root))
            if(IsDirectory(entry)&&entry.path().filename().wstring().starts_with(L"log_")) {
                if(groups.size()==4096)throw std::runtime_error("EFT directory capacity exceeded");
                groups.push_back(entry.path());
            }
        // 显式目录也允许直接指向一组 application/backend 文件。
        // Explicit roots may also directly name a group of application/backend files.
        groups.push_back(root);std::sort(groups.begin(),groups.end());
        for(const auto& group:groups) {
            if(stop.stop_requested())break;
            if(!checkpoint.sourceGroup.empty()&&group<checkpoint.sourceGroup)continue;
            if(!checkpoint.sourceGroup.empty()&&group!=checkpoint.sourceGroup)sources.clear();
            std::vector<std::filesystem::path> paths;
            for(const auto& entry:std::filesystem::directory_iterator(group))
                if(entry.is_regular_file()&&!(GetFileAttributesW(entry.path().c_str())&FILE_ATTRIBUTE_REPARSE_POINT)
                    &&Supported(entry.path()))paths.push_back(entry.path());
            std::sort(paths.begin(),paths.end());
            if(paths.empty())continue;
            if(paths.size()>256)throw std::runtime_error("EFT source group capacity exceeded");
            bool changed=false;
            for(const auto& path:paths) {
                auto& source=sources[path];
                if(!source.reader) {
                    source.reader=std::make_unique<EftLogReader>();
                    const auto saved=std::find_if(checkpoint.cursors.begin(),checkpoint.cursors.end(),[&](const auto& c){return c.path==path;});
                    if(!source.reader->Open(path,saved==checkpoint.cursors.end()?std::nullopt:std::optional(*saved)))
                        throw std::runtime_error("EFT source open failed");
                    changed=true;
                } else if(!source.reader->Refresh())throw std::runtime_error("EFT source refresh failed");
                if(source.reader->TakeSourceReset()) {detector.SourceBoundary();source.heads.clear();changed=true;}
                const auto offset=source.reader->Cursor().offset;Fill(source,stop);
                changed=changed||offset!=source.reader->Cursor().offset;
            }
            std::size_t sinceSave{};
            for(;;) {
                if(stop.stop_requested())break;
                Source* best=nullptr;
                for(auto& [path,source]:sources) {
                    static_cast<void>(path);
                    if(!source.heads.empty()&&(!best||*source.heads.front().time<*best->heads.front().time))best=&source;
                }
                if(!best)break;
                if(checkpoint.sourceGroup!=group) {detector.SourceBoundary();checkpoint.sourceGroup=group;changed=true;}
                detector.Consume(best->heads.front());best->heads.pop_front();++stats.events;++sinceSave;changed=true;
                Fill(*best,stop);
                if(sinceSave==256) {Save();sinceSave=0;changed=false;}
            }
            if(changed)Save();
        }
    }
};
LocalRaidService::LocalRaidService() : stopEvent_(CreateEventW(nullptr,TRUE,FALSE,nullptr)),scanEvent_(CreateEventW(nullptr,FALSE,FALSE,nullptr)) {}
LocalRaidService::~LocalRaidService() {Stop();if(stopEvent_)CloseHandle(stopEvent_);if(scanEvent_)CloseHandle(scanEvent_);}
bool LocalRaidService::RequestScan() {
    std::lock_guard lock(mutex_);
    if(status_.manualScanPending)return true;
    if(!status_.running||!scanEvent_||!SetEvent(scanEvent_))return false;
    status_.manualScanPending=true;return true;
}
bool LocalRaidService::Start(const std::filesystem::path& root,const std::filesystem::path& history) {
    Stop();
    if(!stopEvent_||!scanEvent_||(!root.empty()&&!root.is_absolute())||!history.is_absolute())return false;
    ResetEvent(stopEvent_);
    ResetEvent(scanEvent_);
    {std::lock_guard lock(mutex_);published_={};status_={};status_.running=true;}
    worker_=std::jthread([this,root,history](std::stop_token stop){Run(stop,root,history);});return true;
}
void LocalRaidService::Stop() {
    worker_.request_stop();if(stopEvent_)SetEvent(stopEvent_);if(worker_.joinable())worker_.join();
    pipeline_.reset();std::lock_guard lock(mutex_);status_.running=false;
}
void LocalRaidService::Run(std::stop_token stop,std::filesystem::path root,std::filesystem::path history) {
    HANDLE change=INVALID_HANDLE_VALUE;
    try {
        if(!root.empty()&&Within(history,root))throw std::runtime_error("raid storage must be outside watched EFT logs");
        pipeline_=std::make_unique<Pipeline>();std::string error;
        if(!pipeline_->store.Load(history,pipeline_->checkpoint,error))throw std::runtime_error(error);
        pipeline_->detector.Restore(pipeline_->checkpoint.detector);
        {std::lock_guard lock(mutex_);published_=pipeline_->detector.Snapshot();published_.active.reset();}
        if(changed_)changed_();
        // 无日志配置仍可浏览已保存记录，但不得恢复成正在游戏的会话。
        // Saved history remains browsable without log configuration, never as an active game session.
        if(root.empty()) {
            {std::lock_guard lock(mutex_);published_.active.reset();}
            if(changed_)changed_();
            std::lock_guard lock(mutex_);status_.running=false;return;
        }
        if(!std::filesystem::is_directory(root)||GetFileAttributesW(root.c_str())&FILE_ATTRIBUTE_REPARSE_POINT)
            throw std::runtime_error("configured EFT log root unavailable");
        change=FindFirstChangeNotificationW(root.c_str(),TRUE,FILE_NOTIFY_CHANGE_FILE_NAME|FILE_NOTIFY_CHANGE_DIR_NAME
            |FILE_NOTIFY_CHANGE_SIZE|FILE_NOTIFY_CHANGE_LAST_WRITE);
        if(change==INVALID_HANDLE_VALUE)throw std::runtime_error("EFT directory watch unavailable");
        if(!pipeline_->checkpoint.sourceGroup.empty()&&!Within(pipeline_->checkpoint.sourceGroup,root)) {
            pipeline_->detector.SourceBoundary();pipeline_->checkpoint.sourceGroup.clear();pipeline_->checkpoint.cursors.clear();
        }
        bool manualScan{};
        while(!stop.stop_requested()) {
            pipeline_->Drain(root,stop);
            // 首次打开/读完前已排队的变更也要排空，再发布稳定状态。
            // Drain changes already queued during initial opening/reading before publishing a settled state.
            if(WaitForSingleObject(change,0)==WAIT_OBJECT_0) {
                if(!FindNextChangeNotification(change))throw std::runtime_error("EFT watch rearm failed");
                continue;
            }
            bool notify{};
            {std::lock_guard lock(mutex_);const auto snapshot=pipeline_->detector.Snapshot();
                notify=published_.active!=snapshot.active||published_.completed!=snapshot.completed;
                const auto scans=status_.manualScans+(manualScan?1:0);
                const bool pending=status_.manualScanPending&&!manualScan;
                published_=snapshot;status_=pipeline_->stats;status_.running=true;
                status_.manualScans=scans;status_.manualScanPending=pending;
                // 手动扫描即使无新对局也通知完成；普通无关追加仍不驱动 UI。
                // Notify manual completion even without new raids; unrelated automatic appends still stay quiet.
                notify=notify||manualScan;manualScan=false;}
            if(notify&&changed_)changed_();
            // 手动扫描也交给同一工作线程和增量游标；没有 UI 线程重读或额外轮询。
            // Manual scans share the worker and incremental cursors, with no UI-thread reread or extra polling.
            HANDLE waits[]{stopEvent_,change,scanEvent_};const DWORD result=WaitForMultipleObjects(3,waits,FALSE,INFINITE);
            if(result==WAIT_OBJECT_0)break;
            if(result==WAIT_OBJECT_0+2){manualScan=true;continue;}
            if(result!=WAIT_OBJECT_0+1)throw std::runtime_error("EFT watch wait failed");
            if(!FindNextChangeNotification(change))throw std::runtime_error("EFT watch rearm failed");
            // 合并短时间内的文件追加通知；空闲时无限等待，没有周期性轮询。
            // Coalesce short append bursts; idle waits indefinitely, with no periodic polling.
            if(WaitForSingleObject(stopEvent_,500)==WAIT_OBJECT_0)break;
        }
    } catch(const std::exception& e) {
        {std::lock_guard lock(mutex_);status_.error=e.what();status_.manualScanPending=false;published_.active.reset();}
        if(changed_)changed_();
    }
    if(change!=INVALID_HANDLE_VALUE)FindCloseChangeNotification(change);
    std::lock_guard lock(mutex_);status_.running=false;
}
std::optional<RaidSession> LocalRaidService::ActiveSession() const {
    std::lock_guard lock(mutex_);return published_.active&&!published_.active->sourceInterrupted?published_.active:std::nullopt;
}
std::optional<RaidSession> LocalRaidService::FindSession(std::string_view id) const {
    std::lock_guard lock(mutex_);if(published_.active&&published_.active->localSessionId==id)return published_.active;
    const auto it=std::find_if(published_.completed.begin(),published_.completed.end(),[&](const auto& s){return s.localSessionId==id;});
    return it==published_.completed.end()?std::nullopt:std::optional(*it);
}
std::vector<RaidSession> LocalRaidService::CompletedSessions() const {std::lock_guard lock(mutex_);return published_.completed;}
RaidServiceStatus LocalRaidService::Status() const {std::lock_guard lock(mutex_);return status_;}
}
