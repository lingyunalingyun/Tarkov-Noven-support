#pragma once
#include "ui/PageRegistry.h"
#include "ui/localization/TextKeys.h"

namespace noven::ui {
namespace BuiltinPageId {
inline const PageId Scanner{"builtin.scanner"}, Prices{"builtin.prices"}, Hideout{"builtin.hideout"},
    Tasks{"builtin.tasks"}, Map{"builtin.map"}, RaidHistory{"builtin.raid-history"}, Squad{"builtin.squad"},
    Events{"builtin.events"}, RecentScans{"builtin.recent-scans"}, Settings{"builtin.settings"};
}
// 这是内置页面元数据的唯一来源；不拥有页面实现，也不提供第三方代码执行入口。
// The sole built-in metadata source owns no implementations and provides no third-party execution entry point.
inline PageRegistry MakeBuiltinPageRegistry() {
    PageRegistry registry;
    const auto add=[&](const PageId& id,PageSection section,std::string_view title,std::string_view description,PageIcon icon,int order,
        UiExtensionPolicy policy=UiExtensionPolicy::Extensible) {
        registry.Register({id,section,std::string(title),std::string(description),icon,order,PageSource::BuiltIn,policy});
    };
    add(BuiltinPageId::Scanner,PageSection::Primary,TextKey::NavScan,TextKey::DescScan,PageIcon::Scanner,0);
    add(BuiltinPageId::Prices,PageSection::Primary,TextKey::NavPrices,TextKey::DescPrices,PageIcon::Prices,1);
    add(BuiltinPageId::Hideout,PageSection::Primary,TextKey::NavHideout,TextKey::DescHideout,PageIcon::Hideout,2);
    add(BuiltinPageId::Tasks,PageSection::Primary,TextKey::NavTasks,TextKey::DescTasks,PageIcon::Tasks,3);
    add(BuiltinPageId::Map,PageSection::Primary,TextKey::NavMap,TextKey::DescMap,PageIcon::Map,4);
    add(BuiltinPageId::RaidHistory,PageSection::Secondary,TextKey::NavRaidHistory,TextKey::DescRaidHistory,PageIcon::RaidHistory,0);
    add(BuiltinPageId::Squad,PageSection::Secondary,TextKey::NavSquad,TextKey::DescSquad,PageIcon::Squad,1);
    add(BuiltinPageId::Events,PageSection::Secondary,TextKey::NavEvents,TextKey::DescEvents,PageIcon::Events,2);
    add(BuiltinPageId::RecentScans,PageSection::Secondary,TextKey::NavRecentScans,TextKey::EmptyDescription,PageIcon::RecentScans,3);
    // 先声明保护策略；具体 UI 扩展权限执行留待后续阶段。
    // Declare protection now; enforcement of UI extension permissions belongs to later phases.
    add(BuiltinPageId::Settings,PageSection::Bottom,TextKey::NavSettings,TextKey::LanguageHint,PageIcon::Settings,0,UiExtensionPolicy::Protected);
    return registry;
}
}
