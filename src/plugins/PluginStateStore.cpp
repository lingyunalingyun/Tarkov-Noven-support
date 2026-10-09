#include "plugins/PluginStateStore.h"
#include "plugins/PluginHttpOrigin.h"
#include "plugins/PluginPipe.h"
#include "raid/RaidJson.h"
#include <algorithm>

namespace noven::plugins {
namespace {constexpr std::size_t MaximumStateBytes=256*1024;}
bool SupportedPermission(std::string_view permission){
    return permission=="ui.page.register"||permission=="catalog.items.read"||permission=="catalog.tasks.read"||permission=="catalog.maps.read"
        ||permission=="raid.history.read"||permission=="catalog.events.read"||permission=="scan.history.read"||permission=="scan.events.subscribe"||permission=="storage.plugin"||permission=="network.http";
}
bool SupportedPermissions(const PluginManifest& manifest) {
    return std::all_of(manifest.requestedPermissions.begin(),manifest.requestedPermissions.end(),SupportedPermission)
        &&(std::find(manifest.requestedPermissions.begin(),manifest.requestedPermissions.end(),"network.http")==manifest.requestedPermissions.end()||ValidHttpOrigins(manifest.networkOrigins));
}
PluginIntent PluginStateStore::Intent(std::string_view id) const {
    const auto found=intents_.find(id);return found==intents_.end()?PluginIntent{}:found->second;
}
bool PluginStateStore::Authorized(const PluginManifest& manifest) const {
    if(manifest.manifestVersion!=2||manifest.apiVersion!=1||!manifest.runtime||manifest.runtime->kind!="native-dll"||!ValidRuntimeEntry(manifest.runtime->entry)||!ValidPluginId(manifest.id)||!SupportedPermissions(manifest))return false;
    const auto intent=Intent(manifest.id);if(!intent.enabled)return false;
    if(std::find(manifest.requestedPermissions.begin(),manifest.requestedPermissions.end(),"network.http")!=manifest.requestedPermissions.end()
        &&!std::all_of(manifest.networkOrigins.begin(),manifest.networkOrigins.end(),[&](const auto& origin){return std::find(intent.grantedOrigins.begin(),intent.grantedOrigins.end(),origin)!=intent.grantedOrigins.end();}))return false;
    return std::all_of(manifest.requestedPermissions.begin(),manifest.requestedPermissions.end(),[&](const auto& permission){return std::find(intent.grantedPermissions.begin(),intent.grantedPermissions.end(),permission)!=intent.grantedPermissions.end();});
}
bool PluginStateStore::Consent(const PluginManifest& manifest) {
    if(manifest.manifestVersion!=2||manifest.apiVersion!=1||!manifest.runtime||manifest.runtime->kind!="native-dll"||!ValidRuntimeEntry(manifest.runtime->entry)||!ValidPluginId(manifest.id)||!SupportedPermissions(manifest)||manifest.requestedPermissions.size()>64
        ||(!intents_.contains(manifest.id)&&intents_.size()>=128))return false;
    auto granted=manifest.requestedPermissions;std::sort(granted.begin(),granted.end());granted.erase(std::unique(granted.begin(),granted.end()),granted.end());
    intents_[manifest.id]={true,std::move(granted),manifest.networkOrigins};return true;
}
void PluginStateStore::Disable(std::string_view id){if(auto found=intents_.find(id);found!=intents_.end())found->second.enabled=false;}
std::string PluginStateStore::Encode() const {
    std::string json="{\"schemaVersion\":1,\"plugins\":[";bool comma=false;
    for(const auto& [id,intent]:intents_) {
        if(comma)json+=',';comma=true;
        json+="{\"id\":"+raid::json::Quote(id)+",\"enabled\":"+(intent.enabled?"true":"false")+",\"grantedPermissions\":[";
        bool separator=false;for(const auto& permission:intent.grantedPermissions){if(separator)json+=',';separator=true;json+=raid::json::Quote(permission);}json+="]";
        if(!intent.grantedOrigins.empty()){json+=",\"grantedOrigins\":[";separator=false;for(const auto& origin:intent.grantedOrigins){if(separator)json+=',';separator=true;json+=raid::json::Quote(origin);}json+=']';}json+='}';
    }
    return json+"]}";
}
PluginStateStore PluginStateStore::Decode(std::string_view text) {
    PluginStateStore result;
    try {
        if(text.empty()||text.size()>MaximumStateBytes)throw std::runtime_error("plugin state bounds");
        const auto root=raid::json::Parser(text).Parse();if(root.At("schemaVersion").Int()!=1||root.object.size()!=2)throw std::runtime_error("plugin state schema");
        const auto& entries=root.At("plugins").Array();if(entries.size()>128)throw std::runtime_error("plugin state capacity");
        for(const auto& entry:entries) {
            const auto id=entry.At("id").String();PluginIntent intent;intent.enabled=entry.At("enabled").Bool();
            if(!ValidPluginId(id)||(entry.object.size()!=3&&entry.object.size()!=4)||entry.At("grantedPermissions").Array().size()>10)throw std::runtime_error("plugin state entry");
            for(const auto& value:entry.At("grantedPermissions").Array()){
                const auto& permission=value.String();
                if(!SupportedPermission(permission)||std::find(intent.grantedPermissions.begin(),intent.grantedPermissions.end(),permission)!=intent.grantedPermissions.end())throw std::runtime_error("unsupported/duplicate saved grant");
                intent.grantedPermissions.push_back(permission);
            }
            if(const auto origins=entry.object.find("grantedOrigins");origins!=entry.object.end()){if(origins->second.Array().size()>32)throw std::runtime_error("origin bounds");for(const auto& origin:origins->second.Array())intent.grantedOrigins.push_back(origin.String());if(!ValidHttpOrigins(intent.grantedOrigins))throw std::runtime_error("origin grant");}
            else if(entry.object.size()!=3)throw std::runtime_error("unknown state field");
            if(std::find(intent.grantedPermissions.begin(),intent.grantedPermissions.end(),"network.http")!=intent.grantedPermissions.end()&&!ValidHttpOrigins(intent.grantedOrigins))throw std::runtime_error("missing saved origins");
            if(!result.intents_.emplace(id,std::move(intent)).second)throw std::runtime_error("duplicate plugin intent");
        }
    }catch(const std::exception&){result.intents_.clear();result.corrupt_=true;}
    return result;
}
PluginStateStore PluginStateStore::Load(const std::filesystem::path& path) {
    ipc::Handle file(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    if(!file){PluginStateStore result;const auto error=GetLastError();result.corrupt_=error!=ERROR_FILE_NOT_FOUND&&error!=ERROR_PATH_NOT_FOUND;return result;}
    LARGE_INTEGER size{};BY_HANDLE_FILE_INFORMATION info{};
    if(!GetFileInformationByHandle(file.Get(),&info)||(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))||info.nNumberOfLinks!=1
        ||!GetFileSizeEx(file.Get(),&size)||size.QuadPart<0||size.QuadPart>static_cast<LONGLONG>(MaximumStateBytes))return Decode({});
    std::string text(static_cast<std::size_t>(size.QuadPart),'\0');DWORD read{};
    if(!ReadFile(file.Get(),text.data(),static_cast<DWORD>(text.size()),&read,nullptr)||read!=text.size())return Decode({});
    return Decode(text);
}
bool PluginStateStore::Save(const std::filesystem::path& path) const try {
    std::error_code error;std::filesystem::create_directories(path.parent_path(),error);if(error)return false;
    const auto json=Encode();if(json.size()>MaximumStateBytes)return false;
    const auto nonce=ipc::RandomSecret();auto temporary=path;temporary+=L"."+std::wstring(nonce.begin(),nonce.end());
    // 唯一临时文件 + Flush + 原子替换，不覆盖旧授权直到全部写入成功。
    // Unique temporary file + flush + atomic replacement preserve old consent until fully written.
    ipc::Handle file(CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));if(!file)return false;
    DWORD written{};const bool complete=WriteFile(file.Get(),json.data(),static_cast<DWORD>(json.size()),&written,nullptr)&&written==json.size()&&FlushFileBuffers(file.Get());file.Reset();
    if(complete&&MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))return true;
    DeleteFileW(temporary.c_str());return false;
}catch(const std::exception&){return false;}
}
