#include "resources/ResourceOnboarding.h"
#include "plugins/PluginPipe.h"
#include "raid/RaidJson.h"
#include <algorithm>
#include <limits>
namespace noven::resources {
namespace {
// 每层目录拒绝重解析点；只创建 AppPaths 的资源清单目录，不清理用户文件。
// Reject reparse points on every directory level; create only AppPaths manifest directories, never clean user files.
bool SafeDirectories(const std::filesystem::path& directory,bool create){
    if(!directory.is_absolute())return false;
    auto current=directory.root_path();
    for(const auto& part:directory.relative_path()){
        if(part==L".."||part==L".")return false;current/=part;
        auto attributes=GetFileAttributesW(current.c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES){
            const auto error=GetLastError();
            if(!create||(error!=ERROR_PATH_NOT_FOUND&&error!=ERROR_FILE_NOT_FOUND))return false;
            if(!CreateDirectoryW(current.c_str(),nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)return false;
            attributes=GetFileAttributesW(current.c_str());
        }
        if(attributes==INVALID_FILE_ATTRIBUTES||!(attributes&FILE_ATTRIBUTE_DIRECTORY)||(attributes&FILE_ATTRIBUTE_REPARSE_POINT))return false;
    }
    return true;
}
bool Completed(const std::filesystem::path& path){
    if(!SafeDirectories(path.parent_path(),false))return false;
    plugins::ipc::Handle file(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    BY_HANDLE_FILE_INFORMATION info{};LARGE_INTEGER size{};
    if(!file||!GetFileInformationByHandle(file.Get(),&info)||info.nNumberOfLinks!=1||
        (info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))||!GetFileSizeEx(file.Get(),&size)||size.QuadPart<=0||size.QuadPart>4096)return false;
    std::string text(static_cast<std::size_t>(size.QuadPart),'\0');DWORD read{};
    if(!ReadFile(file.Get(),text.data(),static_cast<DWORD>(text.size()),&read,nullptr)||read!=text.size())return false;
    try{const auto root=raid::json::Parser(text).Parse();return root.At("schemaVersion").Int()==1&&root.At("completed").Bool();}catch(const std::exception&){return false;}
}
}
ResourceSelection::ResourceSelection(const ResourceManifest& manifest):manifest_(manifest){
    if(manifest_.components.size()>MaximumComponents)throw std::runtime_error("resource selection limit");
    std::set<std::string> ids;
    for(const auto& record:manifest_.components)if(!ValidMapIdentity(record.stableMapId)||record.resourceId!="maps."+record.stableMapId||
        !ids.insert(record.resourceId).second||!record.downloadSize||record.downloadSize>MaximumPackageBytes)throw std::runtime_error("invalid resource selection");
}
bool ResourceSelection::Select(std::string_view id,bool selected){
    if(std::none_of(manifest_.components.begin(),manifest_.components.end(),[&](const auto& record){return record.resourceId==id;}))return false;
    if(selected)selected_.insert(std::string(id));else selected_.erase(std::string(id));return true;
}
void ResourceSelection::SelectAll(){for(const auto& record:manifest_.components)selected_.insert(record.resourceId);}
std::uint64_t ResourceSelection::TotalBytes() const {
    std::uint64_t total{};
    for(const auto& record:manifest_.components)if(selected_.contains(record.resourceId)){
        if(record.downloadSize>std::numeric_limits<std::uint64_t>::max()-total)throw std::runtime_error("resource size overflow");total+=record.downloadSize;
    }
    return total;
}
bool ResourceOnboarding::ShouldShow() const {
    return paths_.mode==common::PathMode::Installed&&!Completed(paths_.ResourceManifests()/L"onboarding.json");
}
bool ResourceOnboarding::Complete() try {
    if(!SafeDirectories(paths_.ResourceManifests(),true))return false;
    const auto path=paths_.ResourceManifests()/L"onboarding.json";
    const auto attributes=GetFileAttributesW(path.c_str());
    if(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)))return false;
    const auto nonce=plugins::ipc::RandomSecret();auto temporary=path;temporary+=L"."+std::wstring(nonce.begin(),nonce.end());
    plugins::ipc::Handle file(CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));if(!file)return false;
    constexpr std::string_view text="{\"schemaVersion\":1,\"completed\":true}";DWORD written{};
    const bool complete=WriteFile(file.Get(),text.data(),static_cast<DWORD>(text.size()),&written,nullptr)&&written==text.size()&&FlushFileBuffers(file.Get());file.Reset();
    if(complete&&MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))return true;
    DeleteFileW(temporary.c_str());return false;
}catch(const std::exception&){return false;}
}
