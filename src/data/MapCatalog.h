#pragma once
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace noven::data {
struct MapWorldPosition final { double x{},y{},z{}; };
struct MapRecord final {
    std::string id,normalizedName,nameZh,nameEn,players;
    double cardinalRotation{};
    int raidDuration{};
};
struct MapExtractCondition final {std::string field,value;};
struct MapPointRecord final {
    std::string id,mapId,kind,subtype,sourceId,nameZh,nameEn;
    MapWorldPosition position;
    std::vector<std::string> icons;
    std::vector<MapWorldPosition> outline;
    // 来源条件只作静态说明，不代表玩家当前能否撤离。
    // Source conditions are static information, never live player extraction eligibility.
    std::vector<MapExtractCondition> conditions;
};

// 目录拥有静态数据与身份；世界坐标不等于屏幕坐标或实时游戏状态。
// Catalog owns static data/identity; world coordinates are neither screen positions nor live game state.
class MapCatalog final {
public:
    bool Load(const std::filesystem::path& directory,std::wstring& error);
    const std::vector<MapRecord>& Maps() const noexcept {return maps_;}
    const std::vector<MapPointRecord>& Points() const noexcept {return points_;}
    const MapRecord* Map(std::string_view id) const noexcept;
    const MapPointRecord* Point(std::string_view id) const noexcept;
    bool Ready() const noexcept {return !maps_.empty()&&!points_.empty();}
private:
    std::vector<MapRecord> maps_;
    std::vector<MapPointRecord> points_;
};
}
