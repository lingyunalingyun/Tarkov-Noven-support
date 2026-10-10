#include "updates/VersionStore.h"
#include "resources/ResourceFiles.h"
#include "common/AppPaths.h"
#include "common/RuntimeOwnership.h"
#include "raid/RaidJson.h"
#include <iostream>
#include <array>
#include <source_location>
using namespace noven;
namespace {
void Check(bool value,std::source_location location=std::source_location::current()){
    if(!value)throw std::runtime_error("installer assertion at line "+std::to_string(location.line()));
}
template<class F>void Reject(F f){bool failed{};try{f();}catch(...){failed=true;}Check(failed);}
std::string Bundle(const std::filesystem::path& program,std::string_view version,std::string_view content){
    const auto staging=program/"installer-staging",root=staging/std::string(version);
    Check(resources::RemoveResourceTree(program,staging));
    std::string inventory="{\"schemaVersion\":1,\"version\":"+raid::json::Quote(version)+",\"files\":[";
    const std::array names{"NovenTarkovSupport.exe","NovenPluginHost.exe","noven-installed.layout","assets/data/items_catalog.tsv"};
    for(std::size_t i=0;i<names.size();++i){Check(resources::WriteResourceText(root/names[i],content));if(i)inventory+=',';
        inventory+="{\"path\":"+raid::json::Quote(names[i])+",\"size\":"+std::to_string(content.size())+",\"sha256\":"+raid::json::Quote(resources::ResourceHash(root/names[i]))+"}";}
    inventory+="]}";Check(resources::WriteResourceText(staging/"inventory.json",inventory));return inventory;
}
}
int main()try{
    {const auto marker=CreateMutexW(nullptr,FALSE,L"Local\\NovenTarkovSupport.Installer.70C934D2");Check(marker!=nullptr);
        {common::RuntimeOwnership app(false),host(true);Check(!app.handle&&!host.handle&&common::InstallationInProgress());}CloseHandle(marker);}
    Check(!common::InstallationInProgress());
    const auto root=std::filesystem::temp_directory_path()/("noven-installer-version-"+std::to_string(GetCurrentProcessId()));Check(!std::filesystem::exists(root));
    const auto program=root/"program";updates::VersionStore old(program,"0.1.0");
    const auto paths=common::AppPaths::Installed(program/"versions"/"0.1.0",root/"fake-local");
    Check(paths.BootstrapRoot()==program&&paths.userRoot!=program);
    const std::array sentinels{"plugins/example/manifest.json","data/plugin-state.json","data/plugin-storage/value","data/settings.json","data/recent-scans.json","data/raid-history.json","logs/keep.log","resources/maps/keep.png","updates/cache/keep","data/marketplace-cache.json"};
    for(const auto name:sentinels)Check(resources::WriteResourceText(paths.userRoot/name,"preserve"));
    const auto preserved=[&]{for(const auto name:sentinels)Check(resources::ReadResourceText(paths.userRoot/name,64)=="preserve");};
    Bundle(program,"0.1.0","first core");old.InstallBundle();Check(old.BeginLaunch().active=="0.1.0"&&old.Validate("0.1.0"));
    Check(!std::filesystem::exists(program/"NovenTarkovSupport.exe")&&!std::filesystem::exists(paths.Assets()/"maps"));
    Check(std::filesystem::exists(paths.programRoot/"NovenPluginHost.exe"));preserved();
    const auto current=resources::ReadResourceText(program/"current.json",4096);
    Bundle(program,"0.1.0","repaired core");old.InstallBundle();Check(old.Validate("0.1.0")&&resources::ReadResourceText(program/"current.json",4096)==current);preserved();
    Check(std::filesystem::remove(old.Resolve("0.1.0")/"NovenPluginHost.exe"));
    Bundle(program,"0.1.0","repair missing host");old.InstallBundle();Check(old.Validate("0.1.0"));
    Bundle(program,"0.1.0","bad input");Check(resources::WriteResourceText(program/"installer-staging"/"0.1.0"/"NovenTarkovSupport.exe","tamper"));
    Reject([&]{old.InstallBundle();});Check(old.Validate("0.1.0")&&old.Read().active=="0.1.0");
    Bundle(program,"0.1.0","bad extra");Check(resources::WriteResourceText(program/"installer-staging"/"0.1.0"/"extra.dll","unlisted"));
    Reject([&]{old.InstallBundle();});Check(old.Validate("0.1.0"));
    // 安装进程在目录提交后中断：启动恢复旧目录与旧清单，不依赖时间竞态。
    // Deterministically model interruption after directory publish; launch restores both old tree and receipt.
    const auto receipt=resources::ReadResourceText(program/"installer-releases"/"0.1.0.json",updates::MaximumManifestBytes);
    Check(resources::WriteResourceText(program/"installer-backup"/"receipt.json",receipt));
    Check(MoveFileExW(old.Resolve("0.1.0").c_str(),(program/"installer-backup"/"version").c_str(),MOVEFILE_WRITE_THROUGH));
    Check(resources::WriteResourceText(old.Resolve("0.1.0")/"NovenTarkovSupport.exe","interrupted"));
    Check(resources::WriteResourceText(program/"installer-transaction.json","{\"schemaVersion\":1,\"version\":\"0.1.0\",\"hadVersion\":true,\"hadReceipt\":true}"));
    Check(old.BeginLaunch().active=="0.1.0"&&old.Validate("0.1.0"));preserved();
    updates::VersionStore newer(program,"0.1.1");Bundle(program,"0.1.1","new installed core");newer.InstallBundle();
    auto boot=newer.BeginLaunch();Check(boot.active=="0.1.1"&&boot.previous=="0.1.0"&&boot.pending);newer.ConfirmHealthy(boot.active,boot.token);
    const auto newerCurrent=resources::ReadResourceText(program/"current.json",4096);
    Bundle(program,"0.1.0","old installer repair");old.InstallBundle();Check(old.Read().active=="0.1.1"&&old.Validate("0.1.1")&&resources::ReadResourceText(program/"current.json",4096)==newerCurrent);
    preserved();newer.Rollback();Check(newer.BeginLaunch().active=="0.1.0");
    Check(resources::WriteResourceText(program/"current.json","corrupt"));Check(newer.BeginLaunch().active=="0.1.0");
    Check(resources::WriteResourceText(program/"installer-transaction.json","{\"schemaVersion\":1,\"version\":\"../fake-local\",\"hadVersion\":false,\"hadReceipt\":false}"));
    Reject([&]{newer.BeginLaunch();});preserved();Check(std::filesystem::remove(program/"installer-transaction.json"));
    newer.RemoveInstalledVersions();Check(!std::filesystem::exists(program/"versions")&&!std::filesystem::exists(program/"current.json"));preserved();
    Bundle(program,"0.1.0","reinstalled");old.InstallBundle();Check(old.BeginLaunch().active=="0.1.0");preserved();
    const auto legacy=root/"legacy";Check(resources::WriteResourceText(legacy/"NovenTarkovSupport.exe","legacy recovery app"));Check(resources::WriteResourceText(legacy/"NovenPluginHost.exe","legacy recovery host"));
    Bundle(legacy,"0.1.0","versioned copy");updates::VersionStore migrated(legacy,"0.1.0");migrated.InstallBundle();Check(migrated.BeginLaunch().active=="0.1.0");
    Check(resources::ReadResourceText(legacy/"NovenTarkovSupport.exe",64)=="legacy recovery app"&&resources::ReadResourceText(legacy/"NovenPluginHost.exe",64)=="legacy recovery host");
    preserved();Check(resources::RemoveResourceTree(root.parent_path(),root));
    std::cout<<"installer fresh/repair/upgrade/no-downgrade/migration/interruption/uninstall/user-data PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
