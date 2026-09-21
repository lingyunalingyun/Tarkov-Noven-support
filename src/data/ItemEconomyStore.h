#pragma once

#include "data/GameMode.h"
#include "data/ItemEconomyTypes.h"

#include <cstddef>
#include <filesystem>
#include <string_view>
#include <unordered_map>

namespace noven::data {

class ItemEconomyStore final {
public:
    ItemEconomyStore() = default;

    ItemEconomyStore(const ItemEconomyStore&) = delete;
    ItemEconomyStore& operator=(const ItemEconomyStore&) = delete;

    bool LoadCacheFile(GameMode mode, const std::filesystem::path& path, std::wstring& error);
    bool ReplaceFromUpstreamJson(GameMode mode, std::string_view payload, std::wstring& error);
    bool SaveCacheFile(GameMode mode, const std::filesystem::path& path, std::wstring& error) const;

    [[nodiscard]] const ItemEconomyInfo* Lookup(
        GameMode mode,
        std::string_view item_id
    ) const noexcept;
    [[nodiscard]] std::chrono::system_clock::time_point GetLastUpdated(GameMode mode) const noexcept;
    [[nodiscard]] std::size_t ItemCount(GameMode mode) const noexcept;

private:
    struct ModeCache final {
        std::unordered_map<std::string, ItemEconomyInfo> items;
        std::chrono::system_clock::time_point fetched_at{};
        std::string source;
        std::string upstream_mode;
        int schema_version{};
    };

    [[nodiscard]] ModeCache& Cache(GameMode mode) noexcept;
    [[nodiscard]] const ModeCache& Cache(GameMode mode) const noexcept;

    ModeCache pvp_;
    ModeCache pve_;
    ModeCache seasonal_;
};

} // namespace noven::data
