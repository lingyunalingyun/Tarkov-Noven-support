#include "UpdateTestSigning.h"
#include "updates/UpdateLocalSource.h"
#include "resources/ResourceFiles.h"
#include <iostream>
using namespace noven;
namespace {
void Check(bool v){if(!v)throw std::runtime_error("update service assertion");}
class Gate final:public updates::UpdateSource {
    std::shared_ptr<updates::UpdateSource> local_;
public:
    std::mutex mutex;std::condition_variable_any condition;bool reading{},released{};unsigned checks{};
    explicit Gate(std::filesystem::path fixture):local_(updates::LocalUpdateSource(fixture)){}
    std::string FetchManifest(std::stop_token s)override{++checks;return local_->FetchManifest(s);}
    std::unique_ptr<resources::ResourceStream> Open(std::string_view pack,std::uint64_t offset,std::uint64_t count,std::string_view identity,std::stop_token s)override{
        {std::unique_lock lock(mutex);reading=true;condition.notify_all();if(!condition.wait(lock,s,[&]{return released;}))throw std::runtime_error("paused synthetic transport");}
        return local_->Open(pack,offset,count,identity,s);}
    void Wait(){std::unique_lock lock(mutex);Check(condition.wait_for(lock,std::chrono::seconds(10),[&]{return reading;}));}
    void Release(){std::lock_guard lock(mutex);released=true;condition.notify_all();}
};
}
int main()try {
    const auto root=std::filesystem::temp_directory_path()/("noven-update-service-"+std::to_string(GetCurrentProcessId()));const auto program=root/"program",old=program/"versions"/"0.1.0",fixture=root/"fixture";
    const auto paths=common::AppPaths::Test(old,root/"test");Check(paths.BootstrapRoot()==program);
    Check(resources::WriteResourceText(old/"NovenTarkovSupport.exe","old"));Check(resources::WriteResourceText(old/"NovenPluginHost.exe","host"));
    tests::UpdateSigner signer;updates::VersionStore store(program,"0.1.0",{signer.publicKey});store.SeedInitial();
    updates::ReleaseManifest manifest{"0.1.1","2026-10-10T00:00:00Z","offline review",{}};std::string pack="new apphost";
    Check(resources::WriteResourceText(fixture/"core.pack",pack));
    manifest.files.push_back({"NovenTarkovSupport.exe",updates::HashFileRange(fixture/"core.pack",0,7),7,{{0,7,0,updates::HashFileRange(fixture/"core.pack",0,7),"core.pack"}}});
    manifest.files.push_back({"NovenPluginHost.exe",updates::HashFileRange(fixture/"core.pack",7,4),4,{{0,4,7,updates::HashFileRange(fixture/"core.pack",7,4),"core.pack"}}});
    Check(resources::WriteResourceText(fixture/"release.json",signer.Sign(updates::EncodeReleaseManifest(manifest))));
    auto source=std::make_shared<Gate>(fixture);{
        updates::UpdateService unconfigured(paths,program,"0.1.0",{});Check(unconfigured.Snapshot().state==updates::UpdateState::Unconfigured&&!unconfigured.StartupCheck(100000)&&!unconfigured.Act(updates::UpdateAction::Check));
        Check(!std::filesystem::exists(paths.Updates()));}
    {
        updates::UpdateService service(paths,program,"0.1.0",{signer.publicKey},source);
        Check(service.StartupCheck(100000)&&service.WaitIdle(std::chrono::seconds(10)));Check(source->checks==1&&!service.StartupCheck(100001));
        auto view=service.Snapshot();Check(view.state==updates::UpdateState::Available&&view.fullBytes==11&&view.downloadBytes==7&&view.reusedBytes==4);
        Check(service.Act(updates::UpdateAction::Download));source->Wait();Check(service.Act(updates::UpdateAction::Pause)&&service.WaitIdle(std::chrono::seconds(10))&&service.Snapshot().state==updates::UpdateState::Paused);
        Check(service.Act(updates::UpdateAction::Cancel)&&service.Snapshot().state==updates::UpdateState::Available);
        source->Release();Check(service.Act(updates::UpdateAction::Download)&&service.WaitIdle(std::chrono::seconds(10))&&service.Snapshot().state==updates::UpdateState::Staged);
        Check(store.Read().active=="0.1.0");}
    {
        updates::UpdateService restarted(paths,program,"0.1.0",{signer.publicKey},source);Check(restarted.Act(updates::UpdateAction::Check)&&restarted.WaitIdle(std::chrono::seconds(10))&&restarted.Snapshot().state==updates::UpdateState::Staged);}
    manifest.version="0.1.0";Check(resources::WriteResourceText(fixture/"release.json",signer.Sign(updates::EncodeReleaseManifest(manifest))));
    {
        updates::UpdateService current(paths,program,"0.1.0",{signer.publicKey},source);Check(current.Act(updates::UpdateAction::Check)&&current.WaitIdle(std::chrono::seconds(10)));
        Check(current.Snapshot().state==updates::UpdateState::Current&&current.Snapshot().target.empty()&&!current.Act(updates::UpdateAction::Download));}
    Check(common::AppPaths::Test(program/"versions"/"0.1.1",root/"test").ResourceMaps()==paths.ResourceMaps());
    Check(resources::RemoveResourceTree(root.parent_path(),root));std::cout<<"async capability/check throttling/pause/cancel/staged restart/stable resource root PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
