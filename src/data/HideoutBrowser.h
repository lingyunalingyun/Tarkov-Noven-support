#pragma once
#include "data/HideoutCatalog.h"
#include "data/ItemCatalog.h"
#include "data/ItemEconomyStore.h"
#include "data/LocalizedName.h"

namespace noven::data {
struct HideoutMaterial {
    std::string id, name;
    std::int64_t count{};
    FleaStatus status{FleaStatus::Unknown};
    std::optional<std::int64_t> unitPrice, subtotal;
};
struct HideoutPrerequisite { std::string name; std::int64_t level{}; };
struct HideoutCraftItemView { std::string id, name; double count{}; bool tool{}, functional{}; };
struct HideoutCraftView {
    const HideoutCraft* source{};
    std::string name;
    std::vector<HideoutCraftItemView> materials;
};
struct HideoutRow {
    const HideoutLevel* source{};
    std::string name;
    std::vector<HideoutMaterial> materials;
    std::vector<HideoutPrerequisite> stations, skills, traders;
    std::int64_t knownSubtotal{};
    std::size_t unknownRequirementCount{};
    bool completeEstimate{true};
    bool filteredDetails{};
};
bool AddFleaEstimate(HideoutMaterial& material, std::int64_t& total);
class HideoutBrowser {
public:
    HideoutBrowser(const HideoutCatalog& hideout, const ItemCatalog& items, const ItemEconomyStore& economy)
        : hideout_(hideout), items_(items), economy_(economy) {}
    std::vector<HideoutRow> Query(std::string_view query, GameMode mode, std::string_view locale) const;
    std::vector<HideoutCraftView> Crafts(const HideoutLevel& level, std::string_view locale, std::string_view query={}) const;
    auto LastUpdated(GameMode mode) const { return economy_.GetLastUpdated(mode); }
    bool Ready() const { return hideout_.Ready(); }
private:
    const HideoutCatalog& hideout_;
    const ItemCatalog& items_;
    const ItemEconomyStore& economy_;
};
}
