#include "updates/VersionStore.h"
#include "resources/ResourceFiles.h"
#include "plugins/PluginPipe.h"
#include "raid/RaidJson.h"
#include <set>
namespace noven::updates {
namespace {
[[noreturn]]void Fail(){throw std::runtime_error("invalid application version inventory");}
bool Version(std::string_view s){return ValidReleaseVersion(s);}
bool Running(){for(const auto name:{L"Local\\NovenTarkovSupport.App.70C934D2",L"Local\\NovenTarkovSupport.Host.70C934D2"}){
    HANDLE h=OpenMutexW(SYNCHRONIZE,FALSE,name);if(h){CloseHandle(h);return true;}if(GetLastError()!=ERROR_FILE_NOT_FOUND)return true;}return false;}
bool Inventory(const std::filesystem::path& path,std::string_view text,std::string_view version)try{
    const auto json=raid::json::Parser(text).Parse();if(json.At("schemaVersion").Int()!=1||json.At("version").String()!=version)return false;
    const auto& files=json.At("files").Array();if(files.empty()||files.size()>MaximumReleaseFiles)return false;std::set<std::string> names;std::uint64_t total{};
    for(const auto& f:files){const auto name=f.At("path").String(),hash=f.At("sha256").String();const auto n=f.At("size").Int();
        auto normalized=name;std::transform(normalized.begin(),normalized.end(),normalized.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
        if(!ValidReleasePath(name)||!names.insert(normalized).second||hash.size()!=64||hash.find_first_not_of("0123456789abcdef")!=hash.npos||n<0||static_cast<std::uint64_t>(n)>MaximumReleaseBytes-total||
            std::filesystem::file_size(path/name)!=static_cast<std::uint64_t>(n)||HashFileRange(path/name,0,static_cast<std::uint64_t>(n))!=hash)return false;total+=static_cast<std::uint64_t>(n);}
    std::size_t count{},entries{};for(const auto& entry:std::filesystem::recursive_directory_iterator(path)){
        if(++entries>65536||!resources::SafeResourcePath(entry.path()))return false;if(entry.is_regular_file())++count;}
    return count==files.size()&&names.contains("noventarkovsupport.exe")&&names.contains("novenpluginhost.exe");
}catch(...){return false;}
}
VersionStore::VersionStore(std::filesystem::path root,std::string version,std::vector<ReleasePublicKey> keys):root_(std::move(root)),initial_(std::move(version)),keys_(std::move(keys)){
    if(!resources::SafeResourcePath(root_)||!Version(initial_))Fail();}
std::filesystem::path VersionStore::Resolve(std::string_view version) const {if(!Version(version))Fail();const auto path=root_/L"versions"/std::string(version);if(!resources::SafeResourcePath(path))Fail();return path;}
void VersionStore::Write(const Activation& a) const {using raid::json::Quote;
    if(!Version(a.active)||(!a.previous.empty()&&!Version(a.previous))||a.attempts>2||a.token.size()>64)Fail();
    if(!resources::WriteResourceText(root_/L"current.json","{\"schemaVersion\":1,\"active\":"+Quote(a.active)+",\"previous\":"+Quote(a.previous)+",\"pending\":"+(a.pending?"true":"false")+",\"attempts\":"+std::to_string(a.attempts)+",\"token\":"+Quote(a.token)+"}"))Fail();}
Activation VersionStore::Read() const {
    try{const auto json=raid::json::Parser(resources::ReadResourceText(root_/L"current.json",4096)).Parse();if(json.At("schemaVersion").Int()!=1)Fail();
        Activation a{json.At("active").String(),json.At("previous").String(),json.At("token").String(),json.At("pending").Bool(),0};
        const auto attempts=json.At("attempts").Int();if(!Version(a.active)||(!a.previous.empty()&&!Version(a.previous))||attempts<0||attempts>2||a.token.size()>64)Fail();a.attempts=static_cast<unsigned>(attempts);return a;
    }catch(...){
        // 本地恢复清单只由成功启动确认更新；不能信任损坏 current 的任意路径。
        // A healthy local recovery record, not arbitrary paths from corrupt current, supplies fallback.
        const auto json=raid::json::Parser(resources::ReadResourceText(root_/L"last-good.json",1024)).Parse();const auto version=json.At("version").String();
        if(!Version(version)||!Validate(version))Fail();return {version,{}, {},false,0};}
}
bool VersionStore::Validate(std::string_view version) const try {
    const auto path=Resolve(version);if(!std::filesystem::is_regular_file(path/L"NovenTarkovSupport.exe")||!std::filesystem::is_regular_file(path/L"NovenPluginHost.exe"))return false;
    const auto receipt=root_/L"installer-releases"/(std::string(version)+".json");
    if(std::filesystem::exists(receipt))return Inventory(path,resources::ReadResourceText(receipt,MaximumManifestBytes),version);
    if(std::filesystem::exists(root_/L"initial.json")){
        const auto text=resources::ReadResourceText(root_/L"initial.json",MaximumManifestBytes);
        if(raid::json::Parser(text).Parse().At("version").String()==version)return Inventory(path,text,version);
    }
    const auto envelope=resources::ReadResourceText(root_/L"releases"/(std::string(version)+".json"),2*MaximumManifestBytes+2048);
    const auto release=VerifyRelease(envelope,keys_);return release.Manifest().version==version&&UpdateEngine(root_,root_/L"unused-cache").ValidateVersion(release);
}catch(...){return false;}
void VersionStore::SeedInitial(){
    if(std::filesystem::exists(root_/L"initial.json")||std::filesystem::exists(root_/L"current.json"))Fail();const auto path=Resolve(initial_);std::vector<std::filesystem::path> files;
    for(const auto& f:std::filesystem::recursive_directory_iterator(path)){if(!resources::SafeResourcePath(f.path()))Fail();if(f.is_regular_file())files.push_back(f.path());if(files.size()>MaximumReleaseFiles)Fail();}
    std::sort(files.begin(),files.end());std::string text="{\"schemaVersion\":1,\"version\":"+raid::json::Quote(initial_)+",\"files\":[";std::uint64_t total{};
    for(std::size_t i=0;i<files.size();++i){const auto name=files[i].lexically_relative(path).generic_string();const auto size=std::filesystem::file_size(files[i]);if(!ValidReleasePath(name)||size>MaximumReleaseBytes-total)Fail();total+=size;
        if(i)text+=',';text+="{\"path\":"+raid::json::Quote(name)+",\"size\":"+std::to_string(size)+",\"sha256\":"+raid::json::Quote(HashFileRange(files[i],0,size))+"}";}text+="]}";
    if(!resources::WriteResourceText(root_/L"initial.json",text)||!Validate(initial_))Fail();Write({initial_,{}, {},false,0});
    if(!resources::WriteResourceText(root_/L"last-good.json","{\"version\":"+raid::json::Quote(initial_)+"}"))Fail();
}
void VersionStore::Activate(const AuthenticatedRelease& release){
    if(Running())throw std::runtime_error("close Noven and PluginHost normally before activation");const auto old=Read();const auto& version=release.Manifest().version;
    if(CompareReleaseVersions(version,old.active)<=0||!Validate(old.active)||!Validate(version))Fail();
    Write({version,old.active,plugins::ipc::RandomSecret(),true,0});
}
void VersionStore::RecoverInstaller(){
    const auto journal=root_/L"installer-transaction.json",backup=root_/L"installer-backup";
    if(!std::filesystem::exists(journal))return;
    const auto json=raid::json::Parser(resources::ReadResourceText(journal,4096)).Parse();
    if(json.At("schemaVersion").Int()!=1)Fail();const auto version=json.At("version").String();const auto final=Resolve(version),saved=backup/L"version";
    const auto receipt=root_/L"installer-releases"/(version+".json");
    if(!resources::SafeResourcePath(backup)||!resources::SafeResourcePath(receipt))Fail();
    if(json.At("hadVersion").Bool()){
        if(std::filesystem::exists(saved)){
            if(!resources::RemoveResourceTree(root_,final)||!MoveFileExW(saved.c_str(),final.c_str(),MOVEFILE_WRITE_THROUGH))Fail();
        }
    }else if(!resources::RemoveResourceTree(root_,final))Fail();
    if(json.At("hadReceipt").Bool()){
        if(!resources::WriteResourceText(receipt,resources::ReadResourceText(backup/L"receipt.json",MaximumManifestBytes)))Fail();
    }else if(std::filesystem::exists(receipt)&&!std::filesystem::remove(receipt))Fail();
    if(!std::filesystem::remove(journal)||!resources::RemoveResourceTree(root_,backup))Fail();
}
void VersionStore::InstallBundle(){
    if(Running())throw std::runtime_error("close Noven normally before installer commit");RecoverInstaller();
    const auto staging=root_/L"installer-staging",bundle=staging/std::filesystem::path(initial_),final=Resolve(initial_);
    const auto text=resources::ReadResourceText(staging/L"inventory.json",MaximumManifestBytes);
    if(!resources::SafeResourcePath(bundle)||!Inventory(bundle,text,initial_))Fail();
    const bool hadActivation=std::filesystem::exists(root_/L"current.json")||std::filesystem::exists(root_/L"last-good.json");
    std::optional<Activation> active;if(hadActivation){active=Read();
        // 同版本修复允许损坏的目标；切换到其他版本仍必须保留可信当前版本。
        // Same-version repair may restore damaged target files; switching versions requires a trusted current version.
        if(active->active!=initial_&&!Validate(active->active))Fail();}
    const auto receipt=root_/L"installer-releases"/(initial_+".json"),backup=root_/L"installer-backup",journal=root_/L"installer-transaction.json";
    const bool hadVersion=std::filesystem::exists(final),hadReceipt=std::filesystem::exists(receipt);
    if(!resources::SafeResourcePath(final)||!resources::SafeResourcePath(receipt)||!resources::RemoveResourceTree(root_,backup)||!resources::ResourceDirectories(backup)||!resources::ResourceDirectories(final.parent_path()))Fail();
    if(hadReceipt&&!resources::WriteResourceText(backup/L"receipt.json",resources::ReadResourceText(receipt,MaximumManifestBytes)))Fail();
    if(!resources::WriteResourceText(journal,"{\"schemaVersion\":1,\"version\":"+raid::json::Quote(initial_)+",\"hadVersion\":"+(hadVersion?"true":"false")+",\"hadReceipt\":"+(hadReceipt?"true":"false")+"}"))Fail();
    try{
        if(hadVersion&&!MoveFileExW(final.c_str(),(backup/L"version").c_str(),MOVEFILE_WRITE_THROUGH))Fail();
        if(!MoveFileExW(bundle.c_str(),final.c_str(),MOVEFILE_WRITE_THROUGH)||!resources::WriteResourceText(receipt,text)||!Validate(initial_))Fail();
        // 日志清除是提交点：此前启动恢复旧内容，此后只有完整版本可供激活。
        // Journal removal is the commit point: before it recover old content, after it activate only complete content.
        if(!std::filesystem::remove(journal))Fail();
    }catch(...){RecoverInstaller();throw;}
    if(!std::filesystem::exists(root_/L"initial.json")&&!resources::WriteResourceText(root_/L"initial.json",text))Fail();
    if(!active){Write({initial_,{}, {},false,0});if(!resources::WriteResourceText(root_/L"last-good.json","{\"version\":"+raid::json::Quote(initial_)+"}"))Fail();}
    else if(CompareReleaseVersions(initial_,active->active)>0)Write({initial_,active->active,plugins::ipc::RandomSecret(),true,0});
    // 旧安装器和同版本修复不写 current，绝不隐式降级或重置启动确认。
    // Older installers and same-version repair never rewrite current or reset boot confirmation.
    if(!resources::RemoveResourceTree(root_,backup)||!resources::RemoveResourceTree(root_,staging))Fail();
}
void VersionStore::RemoveInstalledVersions(){
    if(Running())throw std::runtime_error("close Noven before uninstall");
    RecoverInstaller();
    const auto versions=root_/L"versions",releases=root_/L"releases";
    if(!resources::SafeResourcePath(versions)||!resources::SafeResourcePath(releases))Fail();
    for(const auto& entry:std::filesystem::directory_iterator(versions)){
        const auto version=entry.path().filename().string();
        if(!entry.is_directory()||!Version(version)||!Validate(version))Fail();}
    // 卸载只清理固定 Program 子树；没有从清单接受删除路径，也不触碰 User Data。
    // Uninstall removes fixed Program subtrees only, never manifest-selected paths or User Data.
    if(!resources::RemoveResourceTree(root_,versions))Fail();
    if(std::filesystem::exists(releases)&&!resources::RemoveResourceTree(root_,releases))Fail();
    for(const auto name:{L"installer-releases",L"installer-staging",L"installer-backup"})if(!resources::RemoveResourceTree(root_,root_/name))Fail();
    for(const auto name:{L"initial.json",L"current.json",L"last-good.json"}){
        const auto file=root_/name;if(!resources::SafeResourcePath(file))Fail();
        if(std::filesystem::exists(file)&&!std::filesystem::remove(file))Fail();}
}
void VersionStore::Rollback(){if(Running())throw std::runtime_error("close Noven normally before rollback");const auto old=Read();if(old.previous.empty()||!Validate(old.previous))Fail();
    Write({old.previous,{}, {},false,0});if(!resources::WriteResourceText(root_/L"last-good.json","{\"version\":"+raid::json::Quote(old.previous)+"}"))Fail();}
Activation VersionStore::BeginLaunch(){RecoverInstaller();auto a=Read();if(!Validate(a.active)){if(a.previous.empty()||!Validate(a.previous))Fail();Rollback();a=Read();}
    if(a.pending){if(a.attempts){Rollback();return Read();}a.attempts=1;Write(a);}return a;
}
void VersionStore::ConfirmHealthy(std::string_view version,std::string_view token){auto a=Read();if(a.active!=version||!a.pending||a.token!=token||token.empty())Fail();
    a.pending=false;a.attempts=0;a.token.clear();Write(a);if(!resources::WriteResourceText(root_/L"last-good.json","{\"version\":"+raid::json::Quote(a.active)+"}"))Fail();}
void VersionStore::DeferUnconfirmedBoot(std::string_view version,std::string_view token){auto a=Read();if(a.active==version&&a.pending&&a.token==token){a.attempts=0;Write(a);}}
}
