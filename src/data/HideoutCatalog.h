#pragma once
#include "data/GameMode.h"
#include <filesystem>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace noven::data {
struct HideoutStation { std::string mode, id, nameZh, nameEn, imageKey; };
struct HideoutCraftMaterial { std::string itemId; double count{}; bool tool{}, functional{}; };
struct HideoutCraft {
    std::string mode, id, stationId, itemId, restrictions;
    std::int64_t level{}, seconds{}, count{};
    std::vector<HideoutCraftMaterial> materials;
};
struct HideoutItemRequirement { std::string itemId; std::int64_t count{}; };
struct HideoutStationRequirement { std::string stationId; std::int64_t level{}; };
struct HideoutNamedRequirement { std::string id, nameZh, nameEn; std::int64_t level{}; };
struct HideoutLevel {
    std::string mode, id, stationId;
    std::int64_t level{};
    std::optional<std::int64_t> seconds;
    std::vector<HideoutItemRequirement> items;
    std::vector<HideoutStationRequirement> stations;
    std::vector<HideoutNamedRequirement> skills, traders;
};
// 启动加载一次，成功后只读；任何文件失败都不发布半份目录。
// Load once at startup and then read only; a failed file never publishes a partial catalog.
class HideoutCatalog {
public:
    bool Load(const std::filesystem::path& directory, std::wstring& error);
    const std::vector<HideoutLevel>& Levels() const { return levels_; }
    const std::vector<HideoutCraft>& Crafts() const { return crafts_; }
    const HideoutStation* Station(std::string_view mode, std::string_view id) const;
    std::string_view StructureMode(GameMode mode) const;
    bool Ready() const { return !levels_.empty(); }
private:
    std::vector<HideoutStation> stations_;
    std::vector<HideoutLevel> levels_;
    std::vector<HideoutCraft> crafts_;
};
}
