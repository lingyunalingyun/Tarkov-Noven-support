#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <string>
#include <string_view>
#include <vector>

namespace noven::data {
struct MapWorldPosition final {double x{},y{},z{};};
// DEV 的世界 x/z 先旋转，再映射到底图矩形；不读取游戏状态。
// Rotate DEV world x/z before mapping to the image rectangle, without game-state access.
struct MapProjection final {
    double width{},height{},rotation{};
    double minX{},maxX{},minZ{},maxZ{};
    std::array<double,2> Rotate(double x,double z) const noexcept {
        const double angle=rotation*std::numbers::pi/180;
        return {x*std::cos(angle)-z*std::sin(angle),x*std::sin(angle)+z*std::cos(angle)};
    }
    std::array<double,2> Project(MapWorldPosition world) const noexcept {
        auto low=Rotate(minX,minZ),high=low;
        for(const auto& corner:std::array{Rotate(minX,maxZ),Rotate(maxX,minZ),Rotate(maxX,maxZ)})
            for(std::size_t i=0;i<2;++i){low[i]=std::min(low[i],corner[i]);high[i]=std::max(high[i],corner[i]);}
        const auto point=Rotate(world.x,world.z);
        return {(point[0]-low[0])/(high[0]-low[0])*width,(high[1]-point[1])/(high[1]-low[1])*height};
    }
};
struct MapFloorExtent final {
    std::string floorId;
    double bottom{},top{},minX{},maxX{},minZ{},maxZ{};
    bool Contains(MapWorldPosition point) const noexcept {
        return point.y>=bottom&&point.y<top&&point.x>=minX&&point.x<=maxX&&point.z>=minZ&&point.z<=maxZ;
    }
};
struct MapFloorRecord final {
    std::string id,nameZh,nameEn,abstractPath,satellitePath;
    int order{};
};
// 按上游叠层优先顺序判定，有空间边界时不能只用高度；缺失范围回落主层。
// Use source overlay priority and spatial bounds, not height alone; missing ranges fall back to the base floor.
inline std::string_view MapFloorFor(MapWorldPosition point,const std::vector<MapFloorExtent>& extents,std::string_view base) noexcept {
    for(const auto& extent:extents)if(extent.Contains(point))return extent.floorId;
    return base;
}
}
