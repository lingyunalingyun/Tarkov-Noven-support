#include "plugins/PluginManifest.h"
#include "raid/RaidJson.h"
#include <algorithm>
#include <stdexcept>

namespace noven::plugins {
namespace {
bool Segments(std::string_view text,std::size_t maximum,bool reverseDomain) {
    if(text.empty()||text.size()>maximum||text.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789.-")!=text.npos)return false;
    if(reverseDomain&&text.find('.')==text.npos)return false;
    while(!text.empty()) {
        const auto dot=text.find('.');const auto segment=text.substr(0,dot);
        if(segment.empty()||segment.front()=='-'||segment.back()=='-')return false;
        if(dot==text.npos)return true;
        text.remove_prefix(dot+1);if(text.empty())return false;
    }
    return false;
}
bool Numeric(std::string_view value,bool leadingZero) {
    return !value.empty()&&value.find_first_not_of("0123456789")==value.npos
        &&(leadingZero||value.size()==1||value.front()!='0');
}
bool Identifiers(std::string_view value,bool prerelease) {
    if(value.empty())return false;
    while(!value.empty()) {
        const auto dot=value.find('.');const auto id=value.substr(0,dot);
        if(id.empty()||id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-")!=id.npos)return false;
        if(prerelease&&Numeric(id,true)&&!Numeric(id,false))return false;
        if(dot==value.npos)return true;
        value.remove_prefix(dot+1);if(value.empty())return false;
    }
    return false;
}
bool TextSafe(std::string_view text,bool multiline) {
    return std::none_of(text.begin(),text.end(),[&](unsigned char c){return c==127||(c<32&&!(multiline&&(c=='\n'||c=='\r'||c=='\t')));});
}
bool Url(std::string_view url) {
    if(url.empty())return true;
    const std::size_t prefix=url.starts_with("https://")?8:url.starts_with("http://")?7:0;
    if(!prefix||url.size()<=prefix||url.find_first_of(" \t\r\n\\")!=url.npos)return false;
    const auto host=url.substr(prefix,url.find_first_of("/?#",prefix)-prefix);
    return !host.empty()&&host.find('@')==host.npos;
}
}
bool ValidPluginId(std::string_view id) {
    return Segments(id,128,true)&&!id.starts_with("builtin.")&&!id.starts_with("plugin.");
}
bool ValidSemanticVersion(std::string_view value) {
    if(value.empty()||value.size()>128)return false;
    const auto plus=value.find('+');
    if(plus!=value.npos) {if(!Identifiers(value.substr(plus+1),false))return false;value=value.substr(0,plus);}
    const auto dash=value.find('-');
    if(dash!=value.npos) {if(!Identifiers(value.substr(dash+1),true))return false;value=value.substr(0,dash);}
    for(int index=0;index<3;++index) {
        const auto dot=value.find('.');
        if(!Numeric(value.substr(0,dot),false)||(index<2)==(dot==value.npos))return false;
        if(index<2)value.remove_prefix(dot+1);
    }
    return true;
}
ManifestResult ParseManifest(std::string_view text) {
    ManifestResult result;
    const auto fail=[&](std::string key,std::string field={}){result.diagnostics.push_back({std::move(key),std::move(field)});return result;};
    if(text.size()>MaximumManifestBytes)return fail("plugins.diag.size");
    // 未知字段仅作为 JSON 数据跳过；没有可执行入口字段、权限授予或页面注册副作用。
    // Unknown fields are JSON data only: no executable entry contract, permission grant or page-registration side effect.
    try {
        const auto root=raid::json::Parser(text,true).Parse();
        if(root.type!=raid::json::Value::Type::Object)return fail("plugins.diag.json");
        PluginManifest manifest;
        const auto integer=[&](const char* field,std::int64_t& target) {
            const auto it=root.object.find(field);
            if(it==root.object.end()){result.diagnostics.push_back({"plugins.diag.required",field});return false;}
            if(it->second.type!=raid::json::Value::Type::Integer||it->second.integer<1){result.diagnostics.push_back({"plugins.diag.field",field});return false;}
            target=it->second.integer;return true;
        };
        if(!integer("manifestVersion",manifest.manifestVersion))return result;
        if(manifest.manifestVersion!=1) {result.state=PluginState::IncompatibleManifest;return fail("plugins.diag.manifest_version");}
        const auto string=[&](const char* field,std::string& target,std::size_t maximum,bool required=false,bool multiline=false) {
            const auto it=root.object.find(field);
            if(it==root.object.end()){if(required)result.diagnostics.push_back({"plugins.diag.required",field});return !required;}
            if(it->second.type!=raid::json::Value::Type::String||it->second.text.size()>maximum
                ||(required&&it->second.text.find_first_not_of(" \r\n\t")==std::string::npos)||!TextSafe(it->second.text,multiline)) {
                result.diagnostics.push_back({"plugins.diag.field",field});return false;
            }
            target=it->second.text;return true;
        };
        if(!string("id",manifest.id,128,true)||!string("name",manifest.name,256,true)
            ||!string("version",manifest.version,128,true)||!integer("apiVersion",manifest.apiVersion))return result;
        if(!ValidPluginId(manifest.id))return fail("plugins.diag.id","id");
        if(!ValidSemanticVersion(manifest.version))return fail("plugins.diag.version","version");
        if(!string("description",manifest.description,4096,false,true)||!string("author",manifest.author,256)
            ||!string("homepage",manifest.homepage,2048)||!string("source",manifest.source,2048)||!string("license",manifest.license,128))return result;
        if(!Url(manifest.homepage))return fail("plugins.diag.url","homepage");
        if(!Url(manifest.source))return fail("plugins.diag.url","source");
        if(const auto it=root.object.find("permissions");it!=root.object.end()) {
            if(it->second.type!=raid::json::Value::Type::Array||it->second.array.size()>64)return fail("plugins.diag.permissions","permissions");
            for(const auto& value:it->second.array) {
                if(value.type!=raid::json::Value::Type::String||!Segments(value.text,128,true))return fail("plugins.diag.permissions","permissions");
                if(std::find(manifest.requestedPermissions.begin(),manifest.requestedPermissions.end(),value.text)==manifest.requestedPermissions.end())
                    manifest.requestedPermissions.push_back(value.text);
            }
        }
        result.state=manifest.apiVersion==1?PluginState::Valid:PluginState::IncompatibleApi;
        if(result.state==PluginState::IncompatibleApi)result.diagnostics.push_back({"plugins.diag.api_version","apiVersion"});
        result.manifest=std::move(manifest);return result;
    } catch(const std::exception&) {return fail("plugins.diag.json");}
}
}
