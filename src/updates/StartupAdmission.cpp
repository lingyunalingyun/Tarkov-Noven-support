#include "updates/StartupAdmission.h"
#include "updates/UpdatePlanner.h"
#include "resources/ResourceFiles.h"
#include <array>
#include <algorithm>

namespace noven::updates {
namespace {
std::array<std::string_view,3> Parts(std::string_view version){
    const auto first=version.find('.'),last=version.rfind('.');
    return {version.substr(0,first),version.substr(first+1,last-first-1),version.substr(last+1)};
}
void Cancel(std::stop_token stop){if(stop.stop_requested())throw std::runtime_error("startup verification cancelled");}
}
bool WithinStartupWindow(std::string_view current,std::string_view latest){
    if(!ValidReleaseVersion(current)||!ValidReleaseVersion(latest)||CompareReleaseVersions(current,latest)>0)return false;
    const auto a=Parts(current),b=Parts(latest);if(a[0]!=b[0]||a[1]!=b[1])return false;
    // 十进制加法避免远端超大版本号导致整数溢出。
    // Decimal addition avoids integer overflow on large remote version components.
    std::string limit(a[2]);unsigned carry=5;
    for(std::size_t i=limit.size();i&&carry;){--i;const auto n=static_cast<unsigned>(limit[i]-'0')+carry;limit[i]=static_cast<char>('0'+n%10);carry=n/10;}
    if(carry)limit.insert(limit.begin(),static_cast<char>('0'+carry));
    return b[2].size()<limit.size()||(b[2].size()==limit.size()&&b[2]<=limit);
}
bool VerifyStartupFiles(const std::filesystem::path& root,const AuthenticatedRelease& release,std::stop_token stop)try{
    Cancel(stop);if(!resources::SafeResourcePath(root)||!std::filesystem::is_directory(root))return false;
    const auto& files=release.Manifest().files;std::size_t count{},entries{};bool main{},host{};
    for(const auto& entry:std::filesystem::recursive_directory_iterator(root)){
        Cancel(stop);if(++entries>65536||!resources::SafeResourcePath(entry.path()))return false;
        if(entry.is_regular_file()&&++count>files.size())return false;
    }
    if(count!=files.size())return false;
    for(const auto& file:files){
        Cancel(stop);const auto path=root/file.path;
        if(!resources::SafeResourcePath(path)||std::filesystem::file_size(path)!=file.size||HashFileRange(path,0,file.size)!=file.sha256)return false;
        main|=file.path=="NovenTarkovSupport.exe";host|=file.path=="NovenPluginHost.exe";
    }
    Cancel(stop);return main&&host;
}catch(...){return false;}
void RetainReleaseManifest(const std::filesystem::path& history,const AuthenticatedRelease& release,std::span<const ReleasePublicKey> keys){
    const auto path=history/("release-"+release.Manifest().version+".json");
    if(!resources::SafeResourcePath(path))throw std::runtime_error("unsafe release history path");
    if(std::filesystem::exists(path)){
        const auto previous=VerifyRelease(resources::ReadResourceText(path,2*MaximumManifestBytes+2048),keys);
        if(previous.Identity()!=release.Identity())throw std::runtime_error("published version identity changed");
        return;
    }
    if(!resources::WriteResourceText(path,release.Envelope()))throw std::runtime_error("cannot retain signed release manifest");
}
}
