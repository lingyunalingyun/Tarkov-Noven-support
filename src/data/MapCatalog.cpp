#include "data/MapCatalog.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <unordered_set>

namespace noven::data {
namespace {
std::vector<std::string> Split(const std::string& line){
    std::vector<std::string> fields(1);
    for(std::size_t i=0;i<line.size();++i){
        const char c=line[i];
        if(c=='\t'){fields.emplace_back();continue;}
        if(c!='\\'){fields.back()+=c;continue;}
        if(++i==line.size())throw std::runtime_error("trailing escape");
        switch(line[i]){
        case 't':fields.back()+='\t';break;case 'r':fields.back()+='\r';break;
        case 'n':fields.back()+='\n';break;case '\\':fields.back()+='\\';break;
        default:throw std::runtime_error("invalid escape");}
    }
    return fields;
}
void Identity(const std::string& value){
    if(value.empty()||value.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-")!=std::string::npos)
        throw std::runtime_error("invalid identity");
}
double Number(const std::string& value){
    double result{};const auto [end,ec]=std::from_chars(value.data(),value.data()+value.size(),result);
    if(ec!=std::errc{}||end!=value.data()+value.size()||!std::isfinite(result))throw std::runtime_error("invalid number");
    return result;
}
int Duration(const std::string& value){
    int result{};const auto [end,ec]=std::from_chars(value.data(),value.data()+value.size(),result);
    if(ec!=std::errc{}||end!=value.data()+value.size()||result<0||result>1440)throw std::runtime_error("invalid duration");
    return result;
}
template<class Consumer>void Read(const std::filesystem::path& directory,const char* name,const char* header,Consumer consume){
    std::ifstream file(directory/name,std::ios::binary);std::string line;
    if(!std::getline(file,line))throw std::runtime_error("missing file");
    if(!line.empty()&&line.back()=='\r')line.pop_back();
    if(line!=header)throw std::runtime_error("invalid header");
    const auto width=Split(line).size();
    while(std::getline(file,line)){
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        if(line.empty()||line.size()>131072||line.find('\0')!=std::string::npos
            ||MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,line.data(),static_cast<int>(line.size()),nullptr,0)<=0)
            throw std::runtime_error("invalid UTF-8/row");
        auto fields=Split(line);if(fields.size()!=width)throw std::runtime_error("invalid column count");
        consume(fields);
    }
    if(!file.eof())throw std::runtime_error("read failure");
}
}
const MapRecord* MapCatalog::Map(std::string_view id) const noexcept {
    for(const auto& map:maps_)if(map.id==id)return &map;return nullptr;
}
const MapPointRecord* MapCatalog::Point(std::string_view id) const noexcept {
    for(const auto& point:points_)if(point.id==id)return &point;return nullptr;
}
bool MapCatalog::Load(const std::filesystem::path& directory,std::wstring& error){
    error.clear();
    try{
        // 先完整验证候选目录，失败时保留上一次有效目录，避免半加载数据。
        // Validate a complete candidate first; failed reloads retain the last valid catalog.
        MapCatalog next;
        Read(directory,"map_maps.tsv","id\tnormalizedName\tnameZh\tnameEn\trotation\traidDuration\tplayers",[&](const auto& f){
            Identity(f[0]);Identity(f[1]);
            if(next.Map(f[0])||(f[2].empty()&&f[3].empty()))throw std::runtime_error("invalid map");
            next.maps_.push_back({f[0],f[1],f[2],f[3],f[6],Number(f[4]),Duration(f[5])});
        });
        std::unordered_set<std::string> identities;
        constexpr std::array<std::string_view,7> kinds{"container","extract","transit","spawn","hazard","boss","btr"};
        Read(directory,"map_points.tsv","id\tmapId\tkind\tsubtype\tsourceId\tnameZh\tnameEn\tx\ty\tz",[&](const auto& f){
            Identity(f[0]);Identity(f[1]);
            if(!next.Map(f[1])||!identities.insert(f[0]).second||std::find(kinds.begin(),kinds.end(),f[2])==kinds.end()
                ||f[4].empty()||(f[5].empty()&&f[6].empty()))throw std::runtime_error("invalid point/reference");
            next.points_.push_back({f[0],f[1],f[2],f[3],f[4],f[5],f[6],{Number(f[7]),Number(f[8]),Number(f[9])}});
        });
        if(!next.Ready())throw std::runtime_error("empty catalog");
        for(const auto& map:next.maps_)if(std::none_of(next.points_.begin(),next.points_.end(),[&](const auto& p){return p.mapId==map.id;}))
            throw std::runtime_error("map without points");
        *this=std::move(next);return true;
    }catch(const std::exception&){error=L"地图目录加载失败 / Map catalog load failed";return false;}
}
}
