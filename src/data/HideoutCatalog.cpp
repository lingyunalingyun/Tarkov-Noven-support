#include "data/HideoutCatalog.h"
#include <windows.h>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <set>
#include <stdexcept>
#include <tuple>

namespace noven::data {
namespace {
std::vector<std::string> Split(const std::string& line) {
    std::vector<std::string> result(1);
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (c == '\t') { result.emplace_back(); continue; }
        if (c != '\\') { result.back() += c; continue; }
        if (++i == line.size()) throw std::runtime_error("escape");
        switch (line[i]) {
        case 't': result.back() += '\t'; break;
        case 'n': result.back() += '\n'; break;
        case 'r': result.back() += '\r'; break;
        case '\\': result.back() += '\\'; break;
        default: throw std::runtime_error("escape");
        }
    }
    return result;
}
std::int64_t Number(const std::string& value, std::int64_t minimum = 1) {
    std::int64_t result{};
    const auto [end, ec] = std::from_chars(value.data(), value.data()+value.size(), result);
    if (ec != std::errc{} || end != value.data()+value.size() || result < minimum)
        throw std::runtime_error("number");
    return result;
}
void Identity(const std::string& id) {
    if (id.empty() || id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != std::string::npos)
        throw std::runtime_error("identity");
}
template<class Consumer> void Read(const std::filesystem::path& directory, const char* name,
    const char* header, Consumer consume) {
    std::ifstream file(directory / (std::string("hideout_")+name+".tsv"), std::ios::binary);
    std::string line;
    if (!std::getline(file, line)) throw std::runtime_error("missing file");
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line != header) throw std::runtime_error("header");
    const auto width = Split(line).size();
    std::set<std::string> rows;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line.find('\0') != std::string::npos || line.size() > 65536
            || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, line.data(), static_cast<int>(line.size()), nullptr, 0) <= 0)
            throw std::runtime_error("UTF-8/row");
        if (!rows.insert(line).second) throw std::runtime_error("duplicate row");
        auto fields = Split(line);
        if (fields.size() != width || (fields[0] != "regular" && fields[0] != "pve"))
            throw std::runtime_error("columns/mode");
        consume(fields);
    }
    if (!file.eof()) throw std::runtime_error("read");
}
}
const HideoutStation* HideoutCatalog::Station(std::string_view mode, std::string_view id) const {
    for (const auto& station : stations_) if (station.mode == mode && station.id == id) return &station;
    return nullptr;
}
std::string_view HideoutCatalog::StructureMode(GameMode mode) const {
    if (mode == GameMode::Pve && std::any_of(stations_.begin(), stations_.end(),
        [](const auto& station) { return station.mode == "pve"; })) return "pve";
    return "regular";
}
bool HideoutCatalog::Load(const std::filesystem::path& directory, std::wstring& error) {
    stations_.clear(); levels_.clear(); crafts_.clear(); error.clear();
    try {
        HideoutCatalog next;
        Read(directory, "stations", "mode\tid\tnameZh\tnameEn", [&](const auto& f) {
            Identity(f[1]);
            if (next.Station(f[0], f[1]) || (f[2].empty() && f[3].empty())) throw std::runtime_error("station");
            next.stations_.push_back({f[0],f[1],f[2],f[3]});
        });
        Read(directory, "levels", "mode\tid\tstation\tlevel\tseconds", [&](const auto& f) {
            Identity(f[1]); Identity(f[2]);
            const auto n = Number(f[3]);
            if (!next.Station(f[0],f[2])) throw std::runtime_error("station reference");
            for (const auto& level : next.levels_)
                if (level.mode == f[0] && (level.id == f[1] || (level.stationId == f[2] && level.level == n)))
                    throw std::runtime_error("duplicate level");
            HideoutLevel level; level.mode=f[0]; level.id=f[1]; level.stationId=f[2]; level.level=n;
            if (!f[4].empty()) level.seconds=Number(f[4],0);
            next.levels_.push_back(std::move(level));
        });
        Read(directory, "station_images", "mode\tstationId\timageKey", [&](const auto& f) {
            if (!f[2].starts_with("station-") || f[2].size()<=8 || f[2].size()>96
                || f[2].find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-")!=std::string::npos)
                throw std::runtime_error("image key");
            auto found=std::find_if(next.stations_.begin(),next.stations_.end(),[&](const auto& s){return s.mode==f[0] && s.id==f[1];});
            if (found==next.stations_.end() || !found->imageKey.empty()) throw std::runtime_error("image station reference");
            found->imageKey=f[2];
        });
        Read(directory, "crafts", "mode\tid\tstationId\tlevel\tseconds\titemId\tcount\trestrictions", [&](const auto& f) {
            Identity(f[1]); Identity(f[5]); const auto level=Number(f[3]);
            if (!std::any_of(next.levels_.begin(),next.levels_.end(),[&](const auto& l){
                return l.mode==f[0] && l.stationId==f[2] && l.level==level;
            }) || std::any_of(next.crafts_.begin(),next.crafts_.end(),[&](const auto& c){return c.mode==f[0] && c.id==f[1];}))
                throw std::runtime_error("craft identity/reference");
            next.crafts_.push_back({f[0],f[1],f[2],f[5],f[7],level,Number(f[4],0),Number(f[6]),{}});
        });
        Read(directory, "craft_materials", "mode\tcraftId\titemId\tcount\ttool\tfunctional", [&](const auto& f) {
            Identity(f[2]);
            auto craft=std::find_if(next.crafts_.begin(),next.crafts_.end(),[&](const auto& c){return c.mode==f[0] && c.id==f[1];});
            if (craft==next.crafts_.end() || (f[4]!="0" && f[4]!="1") || (f[5]!="0" && f[5]!="1"))
                throw std::runtime_error("craft material reference/flags");
            for (const auto& m:craft->materials) if(m.itemId==f[2]) throw std::runtime_error("duplicate craft material");
            double count{}; const auto [end,ec]=std::from_chars(f[3].data(),f[3].data()+f[3].size(),count);
            if(ec!=std::errc{} || end!=f[3].data()+f[3].size() || !std::isfinite(count) || count<=0 || count>9007199254740992.0)
                throw std::runtime_error("craft quantity");
            craft->materials.push_back({f[2],count,f[4]=="1",f[5]=="1"});
        });
        const auto find = [&](const auto& f) -> HideoutLevel& {
            for (auto& level : next.levels_) if (level.mode == f[0] && level.id == f[1]) return level;
            throw std::runtime_error("level reference");
        };
        Read(directory, "item_requirements", "mode\tlevelId\titemId\tcount", [&](const auto& f) {
            Identity(f[2]); auto& level = find(f);
            for (const auto& item : level.items) if (item.itemId == f[2]) throw std::runtime_error("duplicate item");
            level.items.push_back({f[2], Number(f[3])});
        });
        Read(directory, "station_requirements", "mode\tlevelId\tstationId\tlevel", [&](const auto& f) {
            const auto n=Number(f[3]);
            if (!std::any_of(next.levels_.begin(),next.levels_.end(),[&](const auto& level) {
                return level.mode==f[0] && level.stationId==f[2] && level.level==n;
            })) throw std::runtime_error("prerequisite reference");
            find(f).stations.push_back({f[2],n});
        });
        for (bool trader : {false,true}) Read(directory, trader ? "trader_requirements" : "skill_requirements",
            trader ? "mode\tlevelId\ttraderId\tnameZh\tnameEn\tlevel" : "mode\tlevelId\tskillId\tnameZh\tnameEn\tlevel",
            [&](const auto& f) {
                Identity(f[2]); if (f[3].empty() && f[4].empty()) throw std::runtime_error("name");
                auto& level=find(f);
                (trader ? level.traders : level.skills).push_back({f[2],f[3],f[4],Number(f[5])});
            });
        if (next.levels_.empty() || !std::any_of(next.levels_.begin(),next.levels_.end(),
            [](const auto& l){ return l.mode=="regular"; })) throw std::runtime_error("empty regular structure");
        std::sort(next.levels_.begin(),next.levels_.end(),[](const auto& a,const auto& b){
            return std::tie(a.mode,a.stationId,a.level)<std::tie(b.mode,b.stationId,b.level); });
        *this=std::move(next); return true;
    } catch (const std::exception&) { error=L"[hideout] invalid or missing generated TSV assets"; return false; }
}
}
