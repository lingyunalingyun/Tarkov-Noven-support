#include "resources/ResourceManifest.h"
#include "plugins/PluginHttpOrigin.h"
#include "plugins/PluginManifest.h"
#include "raid/RaidJson.h"
#include <algorithm>
#include <set>
namespace noven::resources {
namespace {
[[noreturn]] void Invalid(){throw std::runtime_error("invalid resource manifest");}
std::string Text(const raid::json::Value& root,const char* key,std::size_t limit){
    auto value=root.At(key).String();if(value.empty()||value.size()>limit)Invalid();
    for(unsigned char c:value)if(c<32||c==127)Invalid();return value;
}
std::uint64_t Size(const raid::json::Value& root,const char* key,std::uint64_t maximum){
    const auto value=root.At(key).Int();if(value<=0||static_cast<std::uint64_t>(value)>maximum)Invalid();
    return static_cast<std::uint64_t>(value);
}
}
bool ValidMapIdentity(std::string_view value){
    return !value.empty()&&value.size()<=64&&value.front()!='-'&&value.back()!='-'&&
        value.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-")==value.npos;
}
bool ValidArtifactName(std::string_view value){
    return value.size()>4&&value.size()<=128&&value.ends_with(".nvr")&&value.front()!='.'&&
        value.find("..") ==value.npos&&value.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-.")==value.npos;
}
bool ResourceSourcePolicy::Enabled() const {
    const auto url=plugins::ParseHttpUrl(baseUrl);
    return url&&url->port==443&&baseUrl.ends_with('/')&&url->target.find_first_of("?%") ==url->target.npos&&
        url->target.find("..") ==url->target.npos&&url->target.find("//") ==url->target.npos;
}
std::string ResourceSourcePolicy::DownloadUrl(const ResourceRecord& resource) const {
    if(!Enabled()||!ValidArtifactName(resource.artifact))throw std::runtime_error("resource source unavailable");
    return baseUrl+resource.artifact;
}
ResourceManifest ParseResourceManifest(std::string_view utf8,const std::vector<std::string>& knownMapIds){
    if(utf8.empty()||utf8.size()>MaximumManifestBytes)Invalid();
    const auto root=raid::json::Parser(utf8).Parse();if(root.At("schemaVersion").Int()!=1)Invalid();
    const auto& records=root.At("components").Array();if(records.size()>MaximumComponents)Invalid();
    ResourceManifest manifest;std::set<std::string> ids,artifacts;
    for(const auto& value:records){
        ResourceRecord record;
        if(Text(value,"type",32)!="map"||value.At("required").Bool())Invalid();
        record.stableMapId=Text(value,"stableMapId",64);
        if(!ValidMapIdentity(record.stableMapId)||std::find(knownMapIds.begin(),knownMapIds.end(),record.stableMapId)==knownMapIds.end())Invalid();
        record.resourceId=Text(value,"resourceId",80);
        if(record.resourceId!="maps."+record.stableMapId||!ids.insert(record.resourceId).second)Invalid();
        record.titleZh=Text(value,"titleZh",256);record.titleEn=Text(value,"titleEn",256);
        record.version=Text(value,"version",128);if(!plugins::ValidSemanticVersion(record.version))Invalid();
        record.sha256=Text(value,"sha256",64);
        if(record.sha256.size()!=64||record.sha256.find_first_not_of("0123456789abcdef")!=record.sha256.npos)Invalid();
        record.artifact=Text(value,"artifact",128);
        if(!ValidArtifactName(record.artifact)||!artifacts.insert(record.artifact).second)Invalid();
        record.downloadSize=Size(value,"downloadSize",MaximumPackageBytes);
        record.installedSize=Size(value,"installedSize",4*MaximumPackageBytes);
        manifest.components.push_back(std::move(record));
    }
    std::sort(manifest.components.begin(),manifest.components.end(),[](const auto& a,const auto& b){return a.resourceId<b.resourceId;});
    return manifest;
}
std::string EncodeResourceManifest(const ResourceManifest& manifest){
    std::string text="{\"schemaVersion\":1,\"components\":[";bool comma{};
    for(const auto& r:manifest.components){if(comma)text+=',';comma=true;
        text+="{\"type\":\"map\",\"required\":false,\"resourceId\":"+raid::json::Quote(r.resourceId)+",\"stableMapId\":"+raid::json::Quote(r.stableMapId)
            +",\"titleZh\":"+raid::json::Quote(r.titleZh)+",\"titleEn\":"+raid::json::Quote(r.titleEn)+",\"version\":"+raid::json::Quote(r.version)
            +",\"sha256\":"+raid::json::Quote(r.sha256)+",\"artifact\":"+raid::json::Quote(r.artifact)+",\"downloadSize\":"+std::to_string(r.downloadSize)+",\"installedSize\":"+std::to_string(r.installedSize)+"}";}
    return text+"]}";
}
}
