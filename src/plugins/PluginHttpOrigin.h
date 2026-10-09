#pragma once
#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace noven::plugins {
struct HttpUrl final {std::string origin,host,target;unsigned port{443};};
// 托管 URL 使用严格 ASCII 子集，拒绝解析器歧义；域名解析后还必须检查所有地址。
// Managed URLs use a strict ASCII subset without parser ambiguities; every resolved address is checked separately.
inline std::optional<HttpUrl> ParseHttpUrl(std::string_view input){
    if(input.size()>2048||input.size()<9)return {};
    std::string value(input);for(auto& c:value)if(static_cast<unsigned char>(c)<33||static_cast<unsigned char>(c)>126||c=='\\')return {};
    auto scheme=value.substr(0,8);for(auto& c:scheme)if(c>='A'&&c<='Z')c+=32;if(scheme!="https://")return {};
    const auto end=value.find_first_of("/?#",8);auto authority=value.substr(8,end==value.npos?value.size()-8:end-8);
    if(authority.find_first_of("@%[]")!=authority.npos)return {};
    HttpUrl result;const auto colon=authority.find(':');result.host=authority.substr(0,colon);
    if(colon!=authority.npos){const auto port=authority.substr(colon+1);if(port.empty()||port.size()>5||port.find_first_not_of("0123456789")!=port.npos)return {};result.port=0;for(auto c:port)result.port=result.port*10+(c-'0');if(!result.port||result.port>65535)return {};}
    for(auto& c:result.host)if(c>='A'&&c<='Z')c+=32;
    if(result.host.size()>253||result.host.find('.')==result.host.npos||result.host.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-.")!=result.host.npos)return {};
    std::string_view remaining=result.host;while(!remaining.empty()){const auto dot=remaining.find('.');const auto label=remaining.substr(0,dot);if(label.empty()||label.size()>63||label.front()=='-'||label.back()=='-')return {};if(dot==remaining.npos)break;remaining.remove_prefix(dot+1);if(remaining.empty())return {};}
    const auto suffix=std::string_view(result.host).substr(result.host.rfind('.')+1);
    if(suffix.find_first_not_of("0123456789")==suffix.npos)return {};
    for(const auto local:{"localhost","local","internal","lan","home","arpa"})if(suffix==local)return {};
    if(result.host=="localhost.localdomain"||result.host.ends_with(".localhost.localdomain"))return {};
    result.origin="https://"+result.host;if(result.port!=443)result.origin+=':'+std::to_string(result.port);
    result.target=end==value.npos?"/":value.substr(end);if(result.target.find('#')!=result.target.npos)return {};if(result.target.front()=='?')result.target='/'+result.target;
    return result;
}
inline std::optional<std::string> CanonicalHttpOrigin(std::string_view input){
    if(input.size()>512||input.find_first_of("/?#",8)!=input.npos)return {};
    const auto url=ParseHttpUrl(input);return url?std::optional(url->origin):std::nullopt;
}
inline bool ValidHttpOrigins(const std::vector<std::string>& origins){
    if(origins.empty()||origins.size()>32)return false;
    for(std::size_t i=0;i<origins.size();++i){const auto canonical=CanonicalHttpOrigin(origins[i]);if(!canonical||*canonical!=origins[i]||(i&&origins[i-1]>=origins[i]))return false;}return true;
}
}
