#pragma once

#include <cstddef>

namespace noven::ui {

// 点位分类是筛选、地图标记与详情图标共享的稳定视觉身份。
// Point categories are the stable visual identity shared by filters, markers and details.
enum class MapPointCategory {
    Container, LooseLoot, Lock, Switch, StationaryWeapon, Mine, Artillery,
    Boss, Task, PmcExtract, ScavExtract, CoopExtract, Transit, HiddenExtract,
    Sniper, Spawn, ScavSpawn, Btr, EasterEgg, Count
};

inline constexpr std::size_t MapCategoryCount=static_cast<std::size_t>(MapPointCategory::Count);

} // namespace noven::ui
