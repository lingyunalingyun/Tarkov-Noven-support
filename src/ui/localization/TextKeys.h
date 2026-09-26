#pragma once
#include <array>
#include <string_view>

// 新语言只添加 JSON；只有新增文字概念才需要修改这里。
// New languages add JSON only; edit this file only for new text concepts.
namespace noven::ui::TextKey {
inline constexpr std::string_view NavScan = "nav.scan";
inline constexpr std::string_view NavPrices = "nav.prices";
inline constexpr std::string_view NavHideout = "nav.hideout";
inline constexpr std::string_view NavTasks = "nav.tasks";
inline constexpr std::string_view NavMap = "nav.map";
inline constexpr std::string_view NavRaidHistory = "nav.raid_history";
inline constexpr std::string_view NavSquad = "nav.squad";
inline constexpr std::string_view NavEvents = "nav.events";
inline constexpr std::string_view NavRecentScans = "nav.recent_scans";
inline constexpr std::string_view NavSettings = "nav.settings";
inline constexpr std::string_view UserName = "profile.name";
inline constexpr std::string_view LocalUse = "profile.local";
inline constexpr std::string_view Primary = "nav.primary";
inline constexpr std::string_view More = "nav.more";
inline constexpr std::string_view DescScan = "page.scan.description";
inline constexpr std::string_view DescPrices = "page.prices.description";
inline constexpr std::string_view DescHideout = "page.hideout.description";
inline constexpr std::string_view DescTasks = "page.tasks.description";
inline constexpr std::string_view DescMap = "page.map.description";
inline constexpr std::string_view DescRaidHistory = "page.raid_history.description";
inline constexpr std::string_view DescSquad = "page.squad.description";
inline constexpr std::string_view DescEvents = "page.events.description";
inline constexpr std::string_view EmptyTitle = "recent.empty.title";
inline constexpr std::string_view EmptyDescription = "recent.empty.description";
inline constexpr std::string_view ImageUnavailable = "image.unavailable";
inline constexpr std::string_view ScanReady = "scan.ready";
inline constexpr std::string_view ScanHint = "scan.hint";
inline constexpr std::string_view ScannerStatus = "scan.status";
inline constexpr std::string_view GameMode = "scan.game_mode";
inline constexpr std::string_view EconomyHint = "scan.economy_hint";
inline constexpr std::string_view Ready = "status.ready";
inline constexpr std::string_view Unavailable = "status.unavailable";
inline constexpr std::string_view Catalog = "catalog.label";
inline constexpr std::string_view CatalogReady = "catalog.ready";
inline constexpr std::string_view Strict = "match.strict";
inline constexpr std::string_view Possible = "match.possible";
inline constexpr std::string_view Ambiguous = "match.ambiguous";
inline constexpr std::string_view OcrOnly = "match.ocr_only";
inline constexpr std::string_view Unknown = "value.unknown";
inline constexpr std::string_view FleaSale = "price.flea";
inline constexpr std::string_view TraderSale = "price.trader";
inline constexpr std::string_view ValuePerSlot = "price.slot";
inline constexpr std::string_view FleaState = "flea.label";
inline constexpr std::string_view FleaAvailable = "flea.available";
inline constexpr std::string_view FleaBlocked = "flea.blocked";
inline constexpr std::string_view Mode = "overlay.mode";
inline constexpr std::string_view Size = "overlay.size";
inline constexpr std::string_view Language = "settings.language";
inline constexpr std::string_view LanguageHint = "settings.language_hint";
inline constexpr std::array All{NavScan, NavPrices, NavHideout, NavTasks, NavMap, NavRaidHistory, NavSquad, NavEvents, NavRecentScans, NavSettings, UserName, LocalUse, Primary, More, DescScan, DescPrices, DescHideout, DescTasks, DescMap, DescRaidHistory, DescSquad, DescEvents, EmptyTitle, EmptyDescription, ImageUnavailable, ScanReady, ScanHint, ScannerStatus, GameMode, EconomyHint, Ready, Unavailable, Catalog, CatalogReady, Strict, Possible, Ambiguous, OcrOnly, Unknown, FleaSale, TraderSale, ValuePerSlot, FleaState, FleaAvailable, FleaBlocked, Mode, Size, Language, LanguageHint};
}
