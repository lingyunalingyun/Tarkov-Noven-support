#include "updates/UpdateService.h"
#include "updates/UpdateTrust.h"
#include "resources/ResourceFiles.h"
#include "raid/RaidJson.h"
namespace noven::updates {
UpdateService::UpdateService(common::AppPaths paths,std::filesystem::path bootstrap,std::string current,std::vector<ReleasePublicKey> keys,
    std::shared_ptr<UpdateSource> source,std::function<void()> changed):paths_(std::move(paths)),bootstrap_(std::move(bootstrap)),keys_(std::move(keys)),source_(std::move(source)),changed_(std::move(changed)){
    if(!ValidReleaseVersion(current))throw std::runtime_error("invalid current application version");view_.current=std::move(current);
    view_.state=source_&&!keys_.empty()?UpdateState::Idle:UpdateState::Unconfigured;
    if(!bootstrap_.empty())try{view_.previous=VersionStore(bootstrap_,InstallerVersion(),keys_).Read().previous;}catch(...){}
    worker_=std::jthread([this](std::stop_token stop){Worker(stop);});
}
UpdateService::~UpdateService(){Shutdown();}
UpdateSnapshot UpdateService::Snapshot() const {std::lock_guard lock(mutex_);return view_;}
void UpdateService::Notify() const {if(changed_)try{changed_();}catch(...){}}
bool UpdateService::Act(UpdateAction action){bool accepted{};{
    std::lock_guard lock(mutex_);if(stopping_||!source_||keys_.empty()||bootstrap_.empty())return false;
    if(action==UpdateAction::Cancel&&view_.state==UpdateState::Paused){view_.state=UpdateState::Available;return true;}
    if(action==UpdateAction::Pause||action==UpdateAction::Cancel){if(view_.state!=UpdateState::Downloading)return false;paused_=action==UpdateAction::Pause;
        if(pending_){pending_.reset();view_.state=paused_?UpdateState::Paused:UpdateState::Available;idle_.notify_all();}else operation_.request_stop();return true;}
    if(busy_||pending_)return false;
    if(action==UpdateAction::Check){view_.state=UpdateState::Checking;pending_=action;accepted=true;}
    else if((action==UpdateAction::Download||action==UpdateAction::Resume)&&release_&&
        (view_.state==UpdateState::Available||view_.state==UpdateState::Paused||view_.state==UpdateState::Error)){
        view_.state=UpdateState::Downloading;view_.error.clear();view_.received=0;pending_=UpdateAction::Download;paused_=false;accepted=true;}}
    if(accepted){condition_.notify_one();Notify();}return accepted;
}
bool UpdateService::StartupCheck(std::int64_t now){if(now<=0||Snapshot().state==UpdateState::Unconfigured)return false;
    try{const auto j=raid::json::Parser(resources::ReadResourceText(paths_.Updates()/L"check.json",1024)).Parse();const auto last=j.At("lastCheck").Int();
        if(last>0&&last<=now&&now-last<86400)return false;}catch(...){}
    if(!Act(UpdateAction::Check))return false;return resources::WriteResourceText(paths_.Updates()/L"check.json","{\"lastCheck\":"+std::to_string(now)+"}");
}
void UpdateService::Worker(std::stop_token stop){while(!stop.stop_requested()){
    UpdateAction action;std::stop_token token;{
        std::unique_lock lock(mutex_);condition_.wait(lock,[&]{return stopping_||pending_.has_value();});if(stopping_)return;
        action=*pending_;pending_.reset();busy_=true;operation_=std::stop_source{};token=operation_.get_token();}
    try{
        if(action==UpdateAction::Check){const auto text=source_->FetchManifest(token);const auto release=VerifyRelease(text,keys_);
            const auto order=CompareReleaseVersions(release.Manifest().version,view_.current);if(order<0)throw std::runtime_error("remote downgrade denied");
            if(order==0){std::lock_guard lock(mutex_);view_.state=UpdateState::Current;view_.target.clear();view_.notes.clear();view_.error.clear();view_.fullBytes=view_.reusedBytes=view_.downloadBytes=view_.received=0;release_.reset();plan_.reset();}
            else{const auto plan=PlanUpdate(view_.current,paths_.programRoot,paths_.UpdateCache(),release);
                const bool staged=UpdateEngine(bootstrap_,paths_.UpdateCache()).ValidateVersion(release);std::lock_guard lock(mutex_);
                release_=release;plan_=plan;view_.target=plan.version;view_.notes=release.Manifest().notes;view_.fullBytes=plan.fullBytes;view_.reusedBytes=plan.reusedBytes;view_.downloadBytes=plan.downloadBytes;view_.state=staged?UpdateState::Staged:UpdateState::Available;view_.error.clear();}}
        else{const auto release=*release_;const auto plan=PlanUpdate(view_.current,paths_.programRoot,paths_.UpdateCache(),release);
            {std::lock_guard lock(mutex_);plan_=plan;view_.downloadBytes=plan.downloadBytes;view_.reusedBytes=plan.reusedBytes;}
            UpdateEngine(bootstrap_,paths_.UpdateCache()).Stage(release,plan,*source_,token,[&](auto progress){
                {std::lock_guard lock(mutex_);view_.received=progress.downloaded;view_.constructing=progress.constructing;}Notify();});
            std::lock_guard lock(mutex_);view_.state=UpdateState::Staged;view_.constructing=false;view_.received=view_.downloadBytes;}
    }catch(const std::exception& e){std::lock_guard lock(mutex_);if(token.stop_requested())view_.state=paused_?UpdateState::Paused:UpdateState::Available;
        else{view_.state=UpdateState::Error;view_.error=e.what();if(view_.error.size()>256)view_.error.resize(256);}view_.constructing=false;}
    {std::lock_guard lock(mutex_);busy_=false;}idle_.notify_all();Notify();}
}
bool UpdateService::WaitIdle(std::chrono::milliseconds timeout){std::unique_lock lock(mutex_);return idle_.wait_for(lock,timeout,[&]{return !busy_&&!pending_;});}
void UpdateService::Shutdown(){{std::lock_guard lock(mutex_);if(stopping_)return;stopping_=true;pending_.reset();operation_.request_stop();}worker_.request_stop();condition_.notify_all();if(worker_.joinable())worker_.join();}
bool UpdateService::LaunchApply(bool rollback){const auto view=Snapshot();if(bootstrap_.empty()||(!rollback&&view.state!=UpdateState::Staged)||(rollback&&view.previous.empty()))return false;
    const auto version=rollback?view.previous:view.target;if(!ValidReleaseVersion(version))return false;const auto exe=bootstrap_/L"NovenUpdater.exe";
    if(!resources::SafeResourcePath(exe)||!std::filesystem::is_regular_file(exe))return false;
    std::wstring command=L"\""+exe.wstring()+L"\" "+(rollback?L"--rollback ":L"--apply ")+std::wstring(version.begin(),version.end())+L" --wait-pid "+std::to_wstring(GetCurrentProcessId());
    STARTUPINFOW start{sizeof(start)};PROCESS_INFORMATION process{};if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,bootstrap_.c_str(),&start,&process))return false;
    CloseHandle(process.hThread);CloseHandle(process.hProcess);return true;
}
}
