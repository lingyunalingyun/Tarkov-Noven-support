#pragma once
#include "ui/localization/TextKeys.h"
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace noven::ui {
// 只使用上游类型，不根据物品名称推断；具体类型优先，市场状态不属于分类。
// Use upstream types only, never name inference; specific types precede broad ones, excluding market flags.
struct ItemTypeMapping { std::string_view type, key; };
inline constexpr ItemTypeMapping ItemTypeMappings[]{
        {"container", TextKey::TypeContainer}, {"ammoBox", TextKey::TypeAmmoBox},
        {"grenade", TextKey::TypeGrenade}, {"ammo", TextKey::TypeAmmo},
        {"armorPlate", TextKey::TypeArmorPlate}, {"helmet", TextKey::TypeHelmet},
        {"armor", TextKey::TypeArmor}, {"rig", TextKey::TypeRig},
        {"backpack", TextKey::TypeBackpack}, {"headphones", TextKey::TypeHeadphones},
        {"glasses", TextKey::TypeGlasses}, {"gun", TextKey::TypeGun},
        {"preset", TextKey::TypeGun}, {"suppressor", TextKey::TypeSuppressor},
        {"pistolGrip", TextKey::TypeGrip}, {"mods", TextKey::TypeMods},
        {"keys", TextKey::TypeKeys}, {"injectors", TextKey::TypeInjectors},
        {"meds", TextKey::TypeMeds}, {"provisions", TextKey::TypeProvisions},
        {"barter", TextKey::TypeBarter}, {"specialSlot", TextKey::TypeSpecial},
        {"poster", TextKey::TypePoster}, {"wearable", TextKey::TypeWearable}};
inline std::string_view ItemTypeKey(const std::vector<std::string>& types) {
    for (const auto& mapping : ItemTypeMappings)
        if (std::find(types.begin(), types.end(), mapping.type) != types.end()) return mapping.key;
    return {};
}
}
