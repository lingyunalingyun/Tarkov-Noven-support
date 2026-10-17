#include "UpdateTestSigning.h"
#include "updates/StartupAdmission.h"
#include "updates/UpdateLocalSource.h"
#include "resources/ResourceFiles.h"
#include <iostream>
#include <source_location>
using namespace noven;
namespace {
void Check(bool value,std::source_location at=std::source_location::current()){
    if(!value)throw std::runtime_error("startup admission assertion line "+std::to_string(at.line()));
}
template<class F>void Reject(F f){bool rejected{};try{f();}catch(...){rejected=true;}Check(rejected);}
class Source final:public updates::UpdateSource {
public:
    std::string latest,archive;unsigned checks{},archives{};bool offline{};
    std::string FetchManifest(std::stop_token stop)override{++checks;if(offline||stop.stop_requested())throw std::runtime_error("offline");return latest;}
    std::string FetchVersionManifest(std::string_view version,std::stop_token stop)override{
        Check(version=="0.1.3");++archives;if(offline||stop.stop_requested())throw std::runtime_error("offline");return archive;}
    std::unique_ptr<resources::ResourceStream> Open(std::string_view,std::uint64_t,std::uint64_t,std::string_view,std::stop_token)override{throw std::runtime_error("no download expected");}
};
}
int main()try{
    using updates::StartupAdmission;
    Check(updates::WithinStartupWindow("0.1.3","0.1.3"));
    Check(updates::WithinStartupWindow("0.1.3","0.1.8"));
    Check(!updates::WithinStartupWindow("0.1.2","0.1.8"));
    Check(!updates::WithinStartupWindow("0.1.99","0.2.0"));
    Check(!updates::WithinStartupWindow("0.1.9","1.0.0"));
    Check(!updates::WithinStartupWindow("0.1.9","0.1.8"));
    Check(updates::WithinStartupWindow("0.1.98","0.1.103"));
    Check(!updates::WithinStartupWindow("0.1.98","0.1.104"));
    Check(updates::WithinStartupWindow("0.1.999999999999999999999","0.1.1000000000000000000004"));
    for(const auto bad:{"0.1.03","0.1.3-beta","../0.1.3","0.1","0.1.3.0"})Check(!updates::WithinStartupWindow(bad,"0.1.8"));
    const auto root=std::filesystem::temp_directory_path()/("noven-startup-admission-"+std::to_string(GetCurrentProcessId()));
    Check(!std::filesystem::exists(root));const auto program=root/"program",runtime=program/"versions"/"0.1.3";
    const auto paths=common::AppPaths::Test(runtime,root/"isolated");
    for(const auto name:{"NovenTarkovSupport.exe","NovenPluginHost.exe","assets/core.dat"})Check(resources::WriteResourceText(runtime/name,name));
    tests::UpdateSigner signer;updates::ReleaseManifest manifest{"0.1.3","2026-10-17T00:00:00Z","",{}};
    for(const auto name:{"NovenTarkovSupport.exe","NovenPluginHost.exe","assets/core.dat"}){
        const auto size=std::filesystem::file_size(runtime/name);const auto hash=updates::HashFileRange(runtime/name,0,size);
        manifest.files.push_back({name,hash,size,{{0,size,0,hash,"test.pack"}}});
    }
    auto source=std::make_shared<Source>();source->latest=source->archive=signer.Sign(updates::EncodeReleaseManifest(manifest));
    const auto auth=updates::VerifyRelease(source->archive,std::span(&signer.publicKey,1));
    Check(updates::VerifyStartupFiles(runtime,auth,{}));
    Check(resources::WriteResourceText(runtime/"extra.dll","unexpected"));Check(!updates::VerifyStartupFiles(runtime,auth,{}));
    Check(std::filesystem::remove(runtime/"extra.dll"));
    std::stop_source stop;stop.request_stop();Check(!updates::VerifyStartupFiles(runtime,auth,stop.get_token()));
    // 安装器的自生成清单不能授权篡改字节；只接受签名清单。
    // Installer-generated inventories cannot authorize tampered bytes: signed manifests remain authoritative.
    Check(resources::WriteResourceText(runtime/"NovenPluginHost.exe","corrupt host"));Check(!updates::VerifyStartupFiles(runtime,auth,{}));
    Check(resources::WriteResourceText(runtime/"NovenPluginHost.exe","NovenPluginHost.exe"));
    Check(resources::WriteResourceText(paths.Data()/"settings.json","preserve"));
    Check(resources::WriteResourceText(paths.Plugins()/"keep.txt","preserve"));
    {
        updates::UpdateService service(paths,program,"0.1.3",{signer.publicKey},source,{},true);
        Check(service.Snapshot().admission==StartupAdmission::Blocked);
        Check(service.StartupCheck(100000)&&service.WaitIdle(std::chrono::seconds(10)));
        Check(service.Snapshot().admission==StartupAdmission::Allowed&&source->checks==1);
    }
    const auto check=[&](StartupAdmission expected){
        updates::UpdateService service(paths,program,"0.1.3",{signer.publicKey},source,{},true);
        Check(service.StartupCheck(100001)&&service.WaitIdle(std::chrono::seconds(10)));
        Check(service.Snapshot().admission==expected);
    };
    check(StartupAdmission::Allowed);Check(source->checks==2); // No daily check throttle.
    manifest.version="0.1.8";source->latest=signer.Sign(updates::EncodeReleaseManifest(manifest));check(StartupAdmission::Allowed);Check(source->archives==1);
    manifest.version="0.1.9";source->latest=signer.Sign(updates::EncodeReleaseManifest(manifest));check(StartupAdmission::UpdateRequired);
    manifest.version="0.2.0";source->latest=signer.Sign(updates::EncodeReleaseManifest(manifest));check(StartupAdmission::UpdateRequired);
    source->offline=true;check(StartupAdmission::Blocked);source->offline=false;
    const auto valid=source->latest;source->latest="{}";check(StartupAdmission::Blocked);source->latest=valid;
    tests::UpdateSigner wrong;source->latest=wrong.Sign(updates::EncodeReleaseManifest(manifest));check(StartupAdmission::Blocked);source->latest=valid;
    const auto archive=source->archive;source->archive=valid;check(StartupAdmission::Blocked);source->archive=archive;
    source->archive="{}";check(StartupAdmission::Blocked);source->archive=archive;
    Check(resources::WriteResourceText(runtime/"assets/core.dat","modified"));check(StartupAdmission::Blocked);
    Check(resources::WriteResourceText(runtime/"assets/core.dat","assets/core.dat"));
    manifest.version="0.1.2";source->latest=signer.Sign(updates::EncodeReleaseManifest(manifest));check(StartupAdmission::Blocked);
    manifest.version="0.1.3";manifest.notes="different bytes under a published version";
    const auto changed=updates::VerifyRelease(signer.Sign(updates::EncodeReleaseManifest(manifest)),std::span(&signer.publicKey,1));
    Reject([&]{updates::RetainReleaseManifest(paths.Updates()/"manifests",changed,std::span(&signer.publicKey,1));});
    // 真正走既有增量暂存/激活路径，再次启动校验通过；回滚不能绕过窗口。
    // Exercise delta staging/activation and re-admission; rollback cannot bypass the version window.
    const auto fixture=root/"fixture";std::string pack;manifest.version="0.1.10";manifest.notes.clear();manifest.files.clear();
    for(const auto name:{"NovenTarkovSupport.exe","NovenPluginHost.exe","assets/core.dat"}){
        const std::string content=std::string_view(name)=="NovenTarkovSupport.exe"?"new main":name;
        const auto offset=pack.size();pack+=content;
        Check(resources::WriteResourceText(fixture/"core.pack",pack));
        const auto hash=updates::HashFileRange(fixture/"core.pack",offset,content.size());
        manifest.files.push_back({name,hash,content.size(),{{0,content.size(),offset,hash,"core.pack"}}});
    }
    Check(resources::WriteResourceText(fixture/"release.json",signer.Sign(updates::EncodeReleaseManifest(manifest))));
    Check(resources::WriteResourceText(fixture/"release-0.1.3.json",archive));
    auto local=updates::LocalUpdateSource(fixture);updates::VersionStore store(program,"0.1.3",{signer.publicKey});store.SeedInitial();
    {
        updates::UpdateService required(paths,program,"0.1.3",{signer.publicKey},local,{},true);
        Check(required.StartupCheck(100002)&&required.WaitIdle(std::chrono::seconds(10)));
        Check(required.Snapshot().admission==StartupAdmission::UpdateRequired&&required.Snapshot().downloadBytes==8);
        Check(required.Act(updates::UpdateAction::Download)&&required.WaitIdle(std::chrono::seconds(10)));
        Check(required.Snapshot().state==updates::UpdateState::Staged&&required.Snapshot().admission!=StartupAdmission::Allowed);
    }
    const auto target=updates::VerifyRelease(resources::ReadResourceText(fixture/"release.json",65536),std::span(&signer.publicKey,1));
    store.Activate(target);const auto boot=store.BeginLaunch();
    {
        updates::UpdateService next(common::AppPaths::Test(program/"versions"/"0.1.10",root/"isolated"),program,"0.1.10",{signer.publicKey},local,{},true);
        Check(next.StartupCheck(100003)&&next.WaitIdle(std::chrono::seconds(10)));
        Check(next.Snapshot().admission==StartupAdmission::Allowed);
    }
    store.ConfirmHealthy(boot.active,boot.token);store.Rollback();Check(store.Read().active=="0.1.3");
    {
        updates::UpdateService rolledBack(paths,program,"0.1.3",{signer.publicKey},local,{},true);
        Check(rolledBack.StartupCheck(100004)&&rolledBack.WaitIdle(std::chrono::seconds(10)));
        Check(rolledBack.Snapshot().admission==StartupAdmission::UpdateRequired);
    }
    Check(resources::ReadResourceText(paths.Data()/"settings.json",128)=="preserve");
    Check(resources::ReadResourceText(paths.Plugins()/"keep.txt",128)=="preserve");
    Check(std::filesystem::exists(paths.Updates()/"manifests"/"release-0.1.3.json"));
    Check(std::filesystem::exists(paths.Updates()/"manifests"/"release-0.1.8.json"));
    Check(resources::RemoveResourceTree(root.parent_path(),root));
    std::cout<<"startup online admission/version window/signed inventory/history/user-data safety PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
