#include "resources/ResourceService.h"
#include "resources/ResourceFiles.h"
#include "plugins/PluginPipe.h"
#include <array>
#include <algorithm>
namespace noven::resources {
namespace {
using S=ResourceState;
std::uint64_t FileSize(const std::filesystem::path& path){if(!SafeResourcePath(path))throw std::runtime_error("unsafe resource path");std::error_code ec;const auto size=std::filesystem::file_size(path,ec);if(ec)throw std::runtime_error("resource size unavailable");return size;}
bool Same(const ResourceRecord& a,const ResourceRecord& b){return a.resourceId==b.resourceId&&a.version==b.version&&a.sha256==b.sha256&&a.downloadSize==b.downloadSize&&a.installedSize==b.installedSize;}
}
bool LegalResourceTransition(S from,S to){
    if(from==to)return true;
    switch(from){
    case S::Unavailable:case S::NotInstalled:return to==S::Queued||to==S::Verifying||to==S::Error;
    case S::Queued:return to==S::Downloading||to==S::Paused||to==S::NotInstalled||to==S::Unavailable||to==S::Installed||to==S::UpdateAvailable||to==S::Error;
    case S::Downloading:return to==S::Paused||to==S::Verifying||to==S::Error||to==S::Installed||to==S::Unavailable||to==S::NotInstalled||to==S::UpdateAvailable;
    case S::Paused:return to==S::Queued||to==S::NotInstalled||to==S::Unavailable||to==S::Installed||to==S::UpdateAvailable||to==S::Error;
    case S::Verifying:return to==S::Installing||to==S::Installed||to==S::UpdateAvailable||to==S::Error||to==S::Paused;
    case S::Installing:return to==S::Installed||to==S::Error||to==S::Paused||to==S::Unavailable||to==S::NotInstalled||to==S::UpdateAvailable;
    case S::Installed:case S::UpdateAvailable:return to==S::Queued||to==S::Verifying||to==S::Deleting||to==S::Error;
    case S::Deleting:return to==S::NotInstalled||to==S::Unavailable||to==S::Error;
    case S::Error:return to==S::Queued||to==S::Verifying||to==S::Deleting||to==S::NotInstalled||to==S::Unavailable||to==S::Installed||to==S::UpdateAvailable;
    }return false;
}
std::vector<ResourceAction> ResourceActions(const ResourceSnapshot& v,RemoteAvailability remote){
    const bool online=remote!=RemoteAvailability::ProductionEndpointUnconfigured;
    switch(v.state){
    case S::NotInstalled:case S::Unavailable:return online?std::vector{ResourceAction::Download}:std::vector<ResourceAction>{};
    case S::Downloading:case S::Queued:return {ResourceAction::Pause,ResourceAction::Cancel};
    case S::Paused:return online?std::vector{ResourceAction::Resume,ResourceAction::Cancel}:std::vector{ResourceAction::Cancel};
    case S::Installed:return {ResourceAction::Verify,ResourceAction::Delete};
    case S::UpdateAvailable:return online?std::vector{ResourceAction::Download,ResourceAction::Verify,ResourceAction::Delete}:std::vector{ResourceAction::Verify,ResourceAction::Delete};
    case S::Error:if(v.present)return online?std::vector{ResourceAction::Repair,ResourceAction::Delete}:std::vector{ResourceAction::Verify,ResourceAction::Delete};return online?std::vector{ResourceAction::Download,ResourceAction::Cancel}:std::vector<ResourceAction>{};
    default:return {};
    }
}
ResourceService::ResourceService(common::AppPaths paths,ResourceManifest manifest,std::shared_ptr<ResourceTransport> transport,
    std::function<std::uint64_t()> disk,std::function<void()> changed,std::vector<ResourceMapLabel> unavailable):paths_(std::move(paths)),transport_(std::move(transport)),diskSpace_(std::move(disk)),changed_(std::move(changed)){
    if(transport_&&paths_.mode==common::PathMode::Installed)throw std::runtime_error("offline transport forbidden in production installed mode");
    std::vector<std::string> ids;for(const auto& r:manifest.components)ids.push_back(r.stableMapId);
    manifest=ParseResourceManifest(EncodeResourceManifest(manifest),ids);
    if(!diskSpace_)diskSpace_=[this]{auto path=paths_.userRoot;while(!std::filesystem::exists(path)&&path!=path.root_path())path=path.parent_path();ULARGE_INTEGER free{};return GetDiskFreeSpaceExW(path.c_str(),&free,nullptr,nullptr)?free.QuadPart:0ULL;};
    for(const auto& record:manifest.components){Entry entry;entry.view.record=record;entry.view.state=transport_?S::NotInstalled:S::Unavailable;
        try{const auto stored=ParseResourceManifest(ReadResourceText(Pointer(record),MaximumManifestBytes),{record.stableMapId});
            if(stored.components.size()==1&&stored.components[0].resourceId==record.resourceId){entry.installed=stored.components[0];entry.view.state=S::Verifying;entry.busy=true;jobs_.push_back({record.resourceId,Work::Verify});}}
        catch(...){ }
        if(!entry.busy){entry.view.received=LoadPartial(record);if(entry.view.received)entry.view.state=S::Paused;}
        entries_.emplace(record.resourceId,std::move(entry));
    }
    if(!transport_){if(unavailable.size()>MaximumComponents)throw std::runtime_error("resource label limit");
        for(const auto& label:unavailable){if(!ValidMapIdentity(label.stableMapId)||label.titleZh.empty()||label.titleEn.empty()||label.titleZh.size()>256||label.titleEn.size()>256)throw std::runtime_error("invalid map label");
            const auto id="maps."+label.stableMapId;if(entries_.contains(id))continue;Entry entry;entry.view.record.resourceId=id;entry.view.record.stableMapId=label.stableMapId;entry.view.record.titleZh=label.titleZh;entry.view.record.titleEn=label.titleEn;
            try{const auto stored=ParseResourceManifest(ReadResourceText(Pointer(entry.view.record),MaximumManifestBytes),{label.stableMapId});
                if(stored.components.size()==1&&stored.components[0].resourceId==id){entry.installed=stored.components[0];entry.view.record=*entry.installed;entry.view.state=S::Verifying;entry.busy=true;jobs_.push_back({id,Work::Verify});}}
            catch(...){ }
            entries_.emplace(id,std::move(entry));}
    }
    for(auto& [id,e]:entries_)e.view.present=e.installed.has_value();
    for(unsigned i=0;i<2;++i)workers_.emplace_back([this](std::stop_token stop){Worker(stop);});
}
ResourceService::~ResourceService(){Shutdown();}
void ResourceService::Shutdown(){
    {std::lock_guard lock(mutex_);if(stopping_)return;stopping_=true;for(auto& [id,e]:entries_)e.stop.request_stop();jobs_.clear();}
    for(auto& worker:workers_)worker.request_stop();condition_.notify_all();workers_.clear();idle_.notify_all();
}
void ResourceService::Notify() const {try{if(changed_)changed_();}catch(...){}}
std::vector<ResourceSnapshot> ResourceService::Snapshot() const {std::lock_guard lock(mutex_);std::vector<ResourceSnapshot> result;for(const auto& [id,e]:entries_)result.push_back(e.view);return result;}
ResourceService::MapLease ResourceService::ResolveMap(std::string_view id) const {std::lock_guard lock(mutex_);const auto found=entries_.find("maps."+std::string(id));return found==entries_.end()||found->second.view.state==S::Deleting?MapLease{}:found->second.location;}
bool ResourceService::WaitIdle(std::chrono::milliseconds time){std::unique_lock lock(mutex_);return idle_.wait_for(lock,time,[&]{return jobs_.empty()&&!active_;});}
void ResourceService::Set(const std::string& id,S state,std::string error){
    {std::lock_guard lock(mutex_);auto& e=entries_.at(id);if(!LegalResourceTransition(e.view.state,state))throw std::runtime_error("illegal resource transition");e.view.state=state;e.view.error=std::move(error);}Notify();
}
std::filesystem::path ResourceService::Partial(const ResourceRecord& r) const {return paths_.DownloadCache()/L"resources"/(r.stableMapId+".part");}
std::filesystem::path ResourceService::Pointer(const ResourceRecord& r) const {return paths_.ResourceManifests()/(r.stableMapId+".json");}
std::uint64_t ResourceService::LoadPartial(const ResourceRecord& r) const try {
    const auto file=Partial(r);auto metadata=file;metadata+=L".json";
    const auto manifest=ParseResourceManifest(ReadResourceText(metadata,MaximumManifestBytes),{r.stableMapId});
    if(manifest.components.size()!=1||!Same(manifest.components[0],r))return 0;const auto size=FileSize(file);return size<=r.downloadSize?size:0;
}catch(...){return 0;}
void ResourceService::DiscardPartial(const ResourceRecord& r) const {const auto file=Partial(r);auto meta=file;meta+=L".json";if(SafeResourcePath(file))DeleteFileW(file.c_str());if(SafeResourcePath(meta))DeleteFileW(meta.c_str());}
bool ResourceService::Act(std::string_view id,ResourceAction action){
    std::unique_lock lock(mutex_);const auto found=entries_.find(std::string(id));if(stopping_||clearing_||found==entries_.end())return false;auto& e=found->second;
    const auto available=ResourceActions(e.view,Remote());if(std::find(available.begin(),available.end(),action)==available.end()&&action!=ResourceAction::Repair)return false;
    if(action==ResourceAction::Pause||action==ResourceAction::Cancel){
        e.cancel=action==ResourceAction::Cancel;e.stop.request_stop();
        if(!e.busy){if(e.cancel){DiscardPartial(e.view.record);e.view.received=0;e.view.state=e.location?(Same(*e.installed,e.view.record)?S::Installed:S::UpdateAvailable):transport_?S::NotInstalled:S::Unavailable;}}
        else if(e.view.state==S::Queued){std::erase_if(jobs_,[&](const auto& j){return j.id==id;});e.busy=false;e.view.state=e.cancel?(e.location?(Same(*e.installed,e.view.record)?S::Installed:S::UpdateAvailable):transport_?S::NotInstalled:S::Unavailable):S::Paused;if(e.cancel){DiscardPartial(e.view.record);e.view.received=0;}}
        lock.unlock();Notify();idle_.notify_all();return true;
    }
    if(e.busy||e.location.use_count()>1)return false;
    Work work=Work::Download;
    if(action==ResourceAction::Verify)work=Work::Verify;
    if(action==ResourceAction::Delete)work=Work::Delete;
    if(work==Work::Download&&!transport_){e.view.error="resources.unconfigured";lock.unlock();Notify();return false;}
    if(work==Work::Verify&&!e.installed)return false;
    if(work==Work::Delete&&!e.installed)return false;
    if(jobs_.size()>=MaximumComponents)return false;
    e.stop=std::stop_source{};e.cancel=false;e.busy=true;e.view.error.clear();e.view.state=work==Work::Verify?S::Verifying:work==Work::Delete?S::Deleting:S::Queued;
    jobs_.push_back({std::string(id),work});lock.unlock();condition_.notify_one();Notify();return true;
}
void ResourceService::DownloadAll(){for(const auto& v:Snapshot())if(v.state==S::NotInstalled||v.state==S::Unavailable||v.state==S::Error)Act(v.record.resourceId,ResourceAction::Download);}
void ResourceService::CheckResources(){for(const auto& v:Snapshot())if(v.installed)Act(v.record.resourceId,ResourceAction::Verify);}
bool ResourceService::ClearDownloadCache(){std::lock_guard lock(mutex_);if(stopping_||clearing_||active_||!jobs_.empty())return false;clearing_=true;jobs_.push_back({{},Work::ClearCache});condition_.notify_one();return true;}
void ResourceService::Worker(std::stop_token stop){
    while(!stop.stop_requested()){
        Job job;std::stop_token task;
        {std::unique_lock lock(mutex_);condition_.wait(lock,[&]{return stopping_||stop.stop_requested()||!jobs_.empty();});if(stopping_||stop.stop_requested())break;
            job=jobs_.front();jobs_.pop_front();++active_;task=job.work==Work::ClearCache?stop:entries_.at(job.id).stop.get_token();}
        if(job.work==Work::ClearCache){const bool valid=RemoveResourceTree(paths_.DownloadCache(),paths_.DownloadCache()/L"resources");
            {std::lock_guard lock(mutex_);for(auto& [id,e]:entries_)if(e.view.state==S::Paused){if(valid){e.view.received=0;e.view.state=e.location?(Same(*e.installed,e.view.record)?S::Installed:S::UpdateAvailable):transport_?S::NotInstalled:S::Unavailable;}else e.view.error="resources.failed";}clearing_=false;--active_;}idle_.notify_all();Notify();continue;}
        try{Execute(job,task);}catch(...){
            bool cancel{},paused{};{std::lock_guard lock(mutex_);const auto& e=entries_.at(job.id);cancel=e.cancel;paused=task.stop_requested();}
            if(cancel){ResourceRecord r;S next;{std::lock_guard lock(mutex_);auto& e=entries_.at(job.id);r=e.view.record;e.view.received=0;next=e.location?(Same(*e.installed,r)?S::Installed:S::UpdateAvailable):transport_?S::NotInstalled:S::Unavailable;}DiscardPartial(r);Set(job.id,next);}
            else Set(job.id,paused?S::Paused:S::Error,paused?"":"resources.failed");
        }
        {std::lock_guard lock(mutex_);auto& e=entries_.at(job.id);reservedBytes_-=e.reserved;e.reserved=0;e.busy=false;--active_;}idle_.notify_all();Notify();
    }
}
void ResourceService::Execute(const Job& job,std::stop_token stop){
    ResourceRecord record;std::optional<ResourceRecord> installed;
    {std::lock_guard lock(mutex_);const auto& e=entries_.at(job.id);record=e.view.record;installed=e.installed;}
    if(job.work==Work::Verify){
        const auto root=paths_.ResourceMaps()/installed->stableMapId/installed->sha256;const bool valid=VerifyPackage(root,*installed,stop);if(stop.stop_requested())throw std::runtime_error("cancelled");
        {std::lock_guard lock(mutex_);auto& e=entries_.at(job.id);e.location=valid?std::make_shared<const std::filesystem::path>(root):MapLease{};e.view.installed=valid;}
        Set(job.id,valid?(Same(*installed,record)?S::Installed:S::UpdateAvailable):S::Error,valid?"":"resources.corrupt");return;
    }
    if(job.work==Work::Delete){
        const auto target=paths_.ResourceMaps()/record.stableMapId;const auto nonce=plugins::ipc::RandomSecret();const auto trash=paths_.ResourceStaging()/(nonce+"-delete");
        const bool exists=std::filesystem::exists(target);
        if(!SafeResourcePath(target)||!ResourceDirectories(trash.parent_path())||(exists&&!MoveFileExW(target.c_str(),trash.c_str(),MOVEFILE_WRITE_THROUGH)))throw std::runtime_error("delete failed");
        if(!WriteResourceText(Pointer(record),EncodeResourceManifest({}))){MoveFileExW(trash.c_str(),target.c_str(),MOVEFILE_WRITE_THROUGH);throw std::runtime_error("inventory failed");}
        {std::lock_guard lock(mutex_);auto& e=entries_.at(job.id);e.location.reset();e.installed.reset();e.view.installed=false;e.view.present=false;}
        if(!RemoveResourceTree(paths_.ResourceStaging(),trash))throw std::runtime_error("delete incomplete");Set(job.id,transport_?S::NotInstalled:S::Unavailable);return;
    }
    Set(job.id,S::Downloading);
    const auto required=record.downloadSize+record.installedSize;
    const auto available=diskSpace_();bool space{};
    {std::lock_guard lock(mutex_);space=required<=available&&reservedBytes_<=available-required;if(space){entries_.at(job.id).reserved=required;reservedBytes_+=required;}}
    if(!space){Set(job.id,S::Error,"resources.disk_space");return;}
    const auto partial=Partial(record);if(!ResourceDirectories(partial.parent_path())||!SafeResourcePath(partial))throw std::runtime_error("unsafe partial");
    auto offset=LoadPartial(record);if(!offset)DiscardPartial(record);
    auto metadata=partial;metadata+=L".json";if(!WriteResourceText(metadata,EncodeResourceManifest({{record}})))throw std::runtime_error("partial metadata failed");
    plugins::ipc::Handle file(CreateFileW(partial.c_str(),GENERIC_WRITE,0,nullptr,offset?OPEN_EXISTING:CREATE_NEW,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    BY_HANDLE_FILE_INFORMATION info{};if(!file||!GetFileInformationByHandle(file.Get(),&info)||info.nNumberOfLinks!=1||(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("unsafe partial");
    LARGE_INTEGER seek{};seek.QuadPart=static_cast<LONGLONG>(offset);if(!SetFilePointerEx(file.Get(),seek,nullptr,FILE_BEGIN))throw std::runtime_error("seek failed");
    auto stream=transport_->Open(record,offset,stop);
    if(!stream||stream->Offset()!=offset||stream->TotalSize()!=record.downloadSize||stream->Identity()!=record.sha256){file.Reset();DiscardPartial(record);throw std::runtime_error("range identity mismatch");}
    std::array<char,65536> bytes{};std::uint64_t received=offset;
    while(!stop.stop_requested()){
        const auto count=stream->Read(bytes,stop);if(!count)break;if(count>bytes.size()||count>record.downloadSize-received)throw std::runtime_error("response too large");
        DWORD written{};if(!WriteFile(file.Get(),bytes.data(),static_cast<DWORD>(count),&written,nullptr)||written!=count||!FlushFileBuffers(file.Get()))throw std::runtime_error("partial write failed");received+=count;
        {std::lock_guard lock(mutex_);entries_.at(job.id).view.received=received;}Notify();
    }
    file.Reset();if(stop.stop_requested())throw std::runtime_error("cancelled");if(received!=record.downloadSize)throw std::runtime_error("wrong response size");
    Set(job.id,S::Verifying);if(ResourceHash(partial,stop)!=record.sha256){DiscardPartial(record);throw std::runtime_error("hash mismatch");}
    {std::lock_guard lock(mutex_);const auto& e=entries_.at(job.id);if(e.location.use_count()>1&&e.installed&&e.installed->sha256==record.sha256)throw std::runtime_error("resource in use");}
    Set(job.id,S::Installing);const auto root=InstallPackage(paths_,record,partial,stop);
    if(stop.stop_requested()||!WriteResourceText(Pointer(record),EncodeResourceManifest({{record}})))throw std::runtime_error("activation failed");
    {std::lock_guard lock(mutex_);auto& e=entries_.at(job.id);e.installed=record;e.location=std::make_shared<const std::filesystem::path>(root);e.view.installed=true;e.view.present=true;}
    DiscardPartial(record);Set(job.id,S::Installed);
}
}
