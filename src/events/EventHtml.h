#pragma once
#include <charconv>
#include <stdexcept>
#include <string>
#include <string_view>

namespace noven::events::html {
inline std::string_view Attribute(std::string_view tag,std::string_view key) {
    const auto token=std::string(key)+"=\"";const auto p=tag.find(token);
    if(p==tag.npos)return {};const auto begin=p+token.size(),end=tag.find('"',begin);
    if(end==tag.npos)throw std::runtime_error("broken HTML attribute");return tag.substr(begin,end-begin);
}
inline std::string Text(std::string_view html) {
    std::string out;out.reserve(html.size());
    for(std::size_t i=0;i<html.size();) {
        if(html[i]=='<') {
            const auto end=html.find('>',i);if(end==html.npos)throw std::runtime_error("broken HTML tag");
            const auto tag=html.substr(i,end-i+1);if(tag.starts_with("<br") || tag=="</p>")out+='\n';i=end+1;
        }else if(html[i]=='&') {
            const auto end=html.find(';',i);if(end==html.npos || end-i>16)throw std::runtime_error("broken HTML entity");
            const auto entity=html.substr(i+1,end-i-1);unsigned code{};
            if(entity=="amp")code='&';else if(entity=="lt")code='<';else if(entity=="gt")code='>';
            else if(entity=="quot")code='"';else if(entity=="apos")code='\'';else if(entity=="nbsp")code=' ';
            else if(entity.starts_with('#')) {
                const bool hex=entity.size()>1 && (entity[1]=='x' || entity[1]=='X');const auto number=entity.substr(hex?2:1);
                const auto r=std::from_chars(number.data(),number.data()+number.size(),code,hex?16:10);
                if(number.empty() || r.ec!=std::errc{} || r.ptr!=number.data()+number.size())throw std::runtime_error("invalid HTML entity");
            }else throw std::runtime_error("unsupported HTML entity");
            if(code==0 || code>0x10FFFF || (code>=0xD800 && code<=0xDFFF))throw std::runtime_error("invalid Unicode entity");
            if(code<0x80)out+=static_cast<char>(code);
            else if(code<0x800){out+=static_cast<char>(0xC0|(code>>6));out+=static_cast<char>(0x80|(code&63));}
            else if(code<0x10000){out+=static_cast<char>(0xE0|(code>>12));out+=static_cast<char>(0x80|((code>>6)&63));out+=static_cast<char>(0x80|(code&63));}
            else{out+=static_cast<char>(0xF0|(code>>18));out+=static_cast<char>(0x80|((code>>12)&63));out+=static_cast<char>(0x80|((code>>6)&63));out+=static_cast<char>(0x80|(code&63));}
            i=end+1;
        }else out+=html[i++];
    }
    const auto first=out.find_first_not_of(" \r\n\t");if(first==out.npos)return {};
    return out.substr(first,out.find_last_not_of(" \r\n\t")-first+1);
}
}
