#include "ui/PluginsPage.h"
#include "plugins/PluginStateStore.h"
namespace noven::ui {
namespace {
std::wstring Wide(std::string_view text){if(text.empty())return {};const auto length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0);if(!length)return {};std::wstring result(length,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),result.data(),length);return result;}
std::string_view ReviewKey(plugins::RegistryReview value){switch(value){case plugins::RegistryReview::Reviewed:return "market.reviewed";case plugins::RegistryReview::Deprecated:return "market.deprecated";case plugins::RegistryReview::Blocked:return "market.blocked";default:return "market.unreviewed";}}
std::string_view CompatibilityKey(plugins::RegistryCompatibility value){switch(value){case plugins::RegistryCompatibility::Compatible:return "market.compatible";case plugins::RegistryCompatibility::Incompatible:return "market.incompatible";default:return "market.unknown";}}
}
std::vector<PluginPresentation> PresentMarketplace(const plugins::PluginRegistry& registry,const plugins::PluginSnapshot& local,
    std::string_view category,bool compatibleOnly,std::optional<plugins::RegistryReview> review){
    std::vector<PluginPresentation> rows;
    for(const auto& entry:registry.plugins){
        const auto compatibility=plugins::Compatibility(entry);
        if((!category.empty()&&std::find(entry.categories.begin(),entry.categories.end(),category)==entry.categories.end())
            ||(compatibleOnly&&compatibility!=plugins::RegistryCompatibility::Compatible)||(review&&*review!=entry.review))continue;
        const auto& m=entry.metadata;PluginPresentation row;row.id=m.id;row.directory=std::filesystem::path(m.id);row.title=Wide(m.name);
        for(auto& c:row.title)if(c<32||c==127)c=L' ';
        row.status=Tr(ReviewKey(entry.review))+L" · "+Tr(CompatibilityKey(compatibility));
        row.error=entry.review==plugins::RegistryReview::Blocked;row.incompatible=compatibility==plugins::RegistryCompatibility::Incompatible;
        const auto line=[&](std::string_view key,std::string_view value){if(!value.empty())row.body+=Tr(key)+L": "+Wide(value)+L"\n";};
        row.body=Wide(m.id)+L"\n";line("plugins.version",m.version);line("plugins.author",m.author);row.body+=Wide(entry.summary)+L"\n";
        row.body+=Tr("market.compatibility")+L": "+Tr(CompatibilityKey(compatibility))+L" · Manifest V"+std::to_wstring(m.manifestVersion)+L" · API "+std::to_wstring(m.apiVersion)+L"\n";
        if(m.manifestVersion==1)row.body+=Tr("plugins.metadata_only")+L"\n";
        const auto found=std::find_if(local.records.begin(),local.records.end(),[&](const auto& record){return record.manifest&&record.manifest->id==m.id;});
        if(found==local.records.end())row.body+=Tr("market.not_installed");
        else if(found->state!=plugins::PluginState::Valid)row.body+=Tr("market.local_incompatible");
        else if(found->manifest->manifestVersion==1)row.body+=Tr("plugins.metadata_only");
        else {const auto comparison=plugins::ComparePluginVersions(found->manifest->version,m.version);row.body+=Tr(comparison==0?"market.installed_same":comparison<0?"market.installed_older":"market.installed_newer");}
        row.body+=L"\n";
        if(!m.description.empty())row.body+=Wide(m.description)+L"\n";
        if(!entry.categories.empty()){row.body+=Tr("plugins.category")+L": ";for(const auto& value:entry.categories)row.body+=Wide(value)+L" · ";row.body+=L"\n";}
        if(!entry.tags.empty()){row.body+=Tr("market.tags")+L": ";for(const auto& value:entry.tags)row.body+=Wide(value)+L" · ";row.body+=L"\n";}
        line("plugins.source",m.source);line("plugins.homepage",m.homepage);line("plugins.license",m.license);line("market.source_ref",entry.sourceRef);line("market.release",entry.releaseUrl);
        row.body+=Tr("plugins.permissions")+L"\n";
        if(m.requestedPermissions.empty())row.body+=Tr("plugins.no_permissions")+L"\n";
        for(const auto& permission:m.requestedPermissions)row.body+=PluginPermissionText(permission,plugins::SupportedPermission(permission))+L"\n";
        const auto network=PluginNetworkText(m);if(!network.empty())row.body+=network+L"\n";
        if(entry.package){const auto& package=*entry.package;line("market.package",package.asset);line("market.package_url",package.url);line("market.package_size",std::to_string(package.size));line("market.package_hash",package.sha256);row.body+=Tr("market.package_notice")+L"\n";}
        if(entry.review==plugins::RegistryReview::Blocked)row.body+=Tr("market.blocked_notice")+L"\n";
        row.body+=Tr("market.trust_notice");
        // 市场身份仅用于选择详情；任何条目都不能产生本地启用/授权请求。
        // Marketplace identity selects details only; no entry can produce local enable/grant requests.
        rows.push_back(std::move(row));
    }
    return rows;
}
}
