#include "plugins/PluginRegistry.h"
#include "plugins/PluginHttpOrigin.h"
#include "plugins/PluginStateStore.h"
#include "raid/RaidJson.h"
#include <set>
namespace noven::plugins {
namespace {
using raid::json::Value;
[[noreturn]] void Invalid(){throw std::runtime_error("invalid plugin registry");}
std::string Text(const Value& root,const char* key,std::size_t limit,bool required=false){
    const auto found=root.object.find(key);if(found==root.object.end()){if(required)Invalid();return {};}
    auto value=found->second.String();if(value.size()>limit||(required&&value.empty()))Invalid();
    for(unsigned char c:value)if(c==127||(c<32&&c!='\n'&&c!='\t'))Invalid();return value;
}
std::string Url(const Value& root,const char* key,bool required=false){auto value=Text(root,key,2048,required);if(!value.empty()&&!ParseHttpUrl(value))Invalid();return value;}
std::vector<std::string> Labels(const Value& root,const char* key,std::size_t count,std::size_t length){
    std::vector<std::string> values;const auto found=root.object.find(key);if(found==root.object.end())return values;
    const auto& array=found->second.Array();if(array.size()>count)Invalid();
    for(const auto& v:array){auto label=v.String();if(label.empty()||label.size()>length)Invalid();for(unsigned char c:label)if(c<32||c==127)Invalid();values.push_back(std::move(label));}
    std::sort(values.begin(),values.end());if(std::adjacent_find(values.begin(),values.end())!=values.end())Invalid();return values;
}
int Number(std::string_view a,std::string_view b){if(a.size()!=b.size())return a.size()<b.size()?-1:1;return a==b?0:a<b?-1:1;}
std::string_view Segment(std::string_view& v){const auto dot=v.find('.');auto first=v.substr(0,dot);v=dot==v.npos?std::string_view{}:v.substr(dot+1);return first;}
}
PluginRegistry ParsePluginRegistry(std::string_view text){
    if(text.empty()||text.size()>MaximumRegistryBytes)Invalid();const auto root=raid::json::Parser(text,true).Parse();
    if(root.At("registryVersion").Int()!=1)Invalid();const auto& entries=root.At("plugins").Array();if(entries.size()>MaximumRegistryPlugins)Invalid();
    PluginRegistry result;std::set<std::string> ids;
    for(const auto& entry:entries){
        RegistryPlugin record;auto& m=record.metadata;
        m.id=Text(entry,"id",128,true);if(!ValidPluginId(m.id)||!ids.insert(m.id).second)Invalid();
        m.name=Text(entry,"name",128,true);m.author=Text(entry,"author",256,true);record.summary=Text(entry,"summary",512,true);
        m.description=Text(entry,"description",8192);m.homepage=Url(entry,"homepage");m.source=Url(entry,"source",true);m.license=Text(entry,"license",128);
        record.categories=Labels(entry,"categories",16,64);record.tags=Labels(entry,"tags",32,64);
        const auto review=Text(entry,"status",32);if(review.empty()||review=="unreviewed")record.review=RegistryReview::Unreviewed;
        else if(review=="reviewed")record.review=RegistryReview::Reviewed;else if(review=="deprecated")record.review=RegistryReview::Deprecated;else if(review=="blocked")record.review=RegistryReview::Blocked;else Invalid();
        const auto& current=entry.At("current");m.version=Text(current,"version",128,true);if(!ValidSemanticVersion(m.version))Invalid();
        m.manifestVersion=current.At("manifestVersion").Int();m.apiVersion=current.At("apiVersion").Int();if(m.manifestVersion<1||m.apiVersion<1)Invalid();
        m.requestedPermissions=Labels(current,"permissions",64,128);
        for(const auto& permission:m.requestedPermissions){if(permission.find('.')==permission.npos||permission.front()=='.'||permission.back()=='.'||permission.find("..")!=permission.npos||permission.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-.")!=permission.npos)Invalid();}
        if(const auto network=current.object.find("network");network!=current.object.end()){
            const auto& origins=network->second.At("origins").Array();if(origins.empty()||origins.size()>32)Invalid();
            for(const auto& origin:origins){auto canonical=CanonicalHttpOrigin(origin.String());if(!canonical)Invalid();m.networkOrigins.push_back(std::move(*canonical));}
            std::sort(m.networkOrigins.begin(),m.networkOrigins.end());m.networkOrigins.erase(std::unique(m.networkOrigins.begin(),m.networkOrigins.end()),m.networkOrigins.end());
        }
        if(std::find(m.requestedPermissions.begin(),m.requestedPermissions.end(),"network.http")!=m.requestedPermissions.end()&&m.networkOrigins.empty())Invalid();
        record.sourceRef=Text(current,"sourceRef",256);record.releaseUrl=Url(current,"releaseUrl");
        if(const auto p=current.object.find("package");p!=current.object.end()){
            RegistryPackage package;package.url=Url(p->second,"url",true);package.asset=Text(p->second,"asset",256,true);package.sha256=Text(p->second,"sha256",64,true);
            if(package.sha256.size()!=64||package.sha256.find_first_not_of("0123456789abcdef")!=package.sha256.npos)Invalid();const auto size=p->second.At("size").Int();if(size<=0||size>1024LL*1024*1024)Invalid();package.size=static_cast<std::uint64_t>(size);record.package=std::move(package);
        }
        result.plugins.push_back(std::move(record));
    }
    std::sort(result.plugins.begin(),result.plugins.end(),[](const auto& a,const auto& b){return a.metadata.name==b.metadata.name?a.metadata.id<b.metadata.id:a.metadata.name<b.metadata.name;});return result;
}
RegistryCompatibility Compatibility(const RegistryPlugin& p){
    const auto& m=p.metadata;if(m.manifestVersion!=1&&m.manifestVersion!=2)return RegistryCompatibility::Unknown;
    if(m.apiVersion!=1||!SupportedPermissions(m))return RegistryCompatibility::Incompatible;return RegistryCompatibility::Compatible;
}
int ComparePluginVersions(std::string_view a,std::string_view b){
    if(!ValidSemanticVersion(a)||!ValidSemanticVersion(b))throw std::runtime_error("invalid semantic version");
    a=a.substr(0,a.find('+'));b=b.substr(0,b.find('+'));const auto dashA=a.find('-'),dashB=b.find('-');auto coreA=a.substr(0,dashA),coreB=b.substr(0,dashB);
    for(int i=0;i<3;++i){const auto comparison=Number(Segment(coreA),Segment(coreB));if(comparison)return comparison;}
    if(dashA==a.npos||dashB==b.npos)return dashA==dashB?0:dashA==a.npos?1:-1;
    auto preA=a.substr(dashA+1),preB=b.substr(dashB+1);while(!preA.empty()&&!preB.empty()){const auto x=Segment(preA),y=Segment(preB);const bool nx=x.find_first_not_of("0123456789")==x.npos,ny=y.find_first_not_of("0123456789")==y.npos;
        const auto comparison=nx&&ny?Number(x,y):nx!=ny?(nx?-1:1):x==y?0:x<y?-1:1;if(comparison)return comparison;}
    return preA.empty()==preB.empty()?0:preA.empty()?-1:1;
}
}
