#include "ui/PluginsPage.h"
#include "ui/BuiltinPages.h"
#include <fstream>
#include <iostream>
using namespace noven::plugins;using namespace noven::ui;
void Check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
int wmain(int argc,wchar_t** argv) try{
    Check(argc==3,"locale/fixture arguments");std::wstring error;Check(UiLocalization().DiscoverLocales(argv[1],error),"localization");
    std::ifstream file(argv[2],std::ios::binary);const std::string text(std::istreambuf_iterator<char>(file),{});
    auto registry=std::make_shared<PluginRegistry>(ParsePluginRegistry(text));auto another=registry->plugins[0];another.metadata.id="com.example.other";another.metadata.name="Other";another.metadata.apiVersion=2;another.categories={"Other category"};another.review=RegistryReview::Blocked;registry->plugins.push_back(another);
    for(const auto locale:{"zh-CN","en-US"}){
        UiLocalization().SetLocale(locale);UiTheme theme;PluginsPage page;PluginSnapshot local;
        auto metadata=registry->plugins[0].metadata;metadata.manifestVersion=1;local.records.push_back({L"plugins/local",PluginState::Valid,metadata,{}});page.SetSnapshot(local);
        auto snapshot=std::make_shared<MarketplaceSnapshot>();snapshot->registry=registry;snapshot->state=MarketplaceState::Live;snapshot->fetchedAt=2000000000;snapshot->reviewFixture=true;
        page.SetMarketplace(snapshot);page.SelectTab(PluginCenterTab::Marketplace);const auto prepare=[&]{page.Prepare(1280,760,theme,nullptr,nullptr,nullptr);};prepare();
        Check(page.Rows().size()==2&&page.Rows()[0].body.find(Tr("plugins.metadata_only"))!=std::wstring::npos,"marketplace uses stable ID local V1 matching");
        Check(page.Rows()[0].body.find(L"https://api.example.com")!=std::wstring::npos&&page.Rows()[0].body.find(L"network.http")!=std::wstring::npos&&page.Rows()[0].body.find(L"SHA-256")!=std::wstring::npos,"origins/permissions/package metadata visible");
        Check(!page.Rows()[0].enable&&!page.Rows()[0].disable&&!page.ControlBounds(metadata.id)&&!page.TakeControlAction(),"read-only marketplace controls");
        Check(page.MarketplaceText().find(Tr("market.fixture"))!=std::wstring::npos&&page.MarketplaceText().find(L"UTC")!=std::wstring::npos,"freshness timestamp and nonproduction label");
        for(const auto query:{L"Registry Test",L"com.example.registry-test",L"TEST AUTHOR",L"Utilities",L"fixture"}){page.SetSearch(query);prepare();Check(!page.Rows().empty(),"local name/ID/author/category/tag search");}
        page.SetSearch({});page.SetMarketplaceFilter("Utilities",false);prepare();Check(page.Rows().size()==1,"real category filter");
        page.SetMarketplaceFilter({},true);prepare();Check(page.Rows().size()==1,"explicit compatible filter");
        page.SetMarketplaceFilter({},false,RegistryReview::Blocked);prepare();Check(page.Rows().size()==1&&page.Rows()[0].body.find(Tr("market.blocked_notice"))!=std::wstring::npos,"blocked detail remains browsable with warning");
        page.SetMarketplaceFilter({},false);prepare();
        const auto category=page.FilterBounds(1);page.Down(category.left+4,category.top+4);page.Up(category.left+4,category.top+4);page.Tick(.3F);
        const auto compatible=DropdownLayout{category}.Option(1);page.Down(compatible.left+4,compatible.top+4);page.Up(compatible.left+4,compatible.top+4);page.Tick(.3F);prepare();Check(page.Rows().size()==1,"native shared dropdown applies compatibility");
        page.SetSearch(L"Registry");prepare();page.SelectTab(PluginCenterTab::MyPlugins);prepare();Check(page.Rows().size()==1&&page.SelectedPlugin()&&page.SelectedPlugin()->directory==L"plugins/local","My Plugins search/selection remains independent");
        page.SetSearch(L"missing local");prepare();page.SelectTab(PluginCenterTab::Marketplace);prepare();Check(page.Rows().size()==1,"tab preserves independent marketplace search");
        const auto refresh=page.RefreshBounds();page.Down(refresh.left+4,refresh.top+4);Check(page.Up(refresh.left+4,refresh.top+4)&&!page.TakeControlAction(),"refresh emits browsing request only");
        Check(page.Snapshot().records.size()==1&&page.Snapshot().records[0].manifest->manifestVersion==1,"search/browse leaves local metadata unchanged");
        auto unrelated=local;unrelated.records[0].manifest->id="com.example.unrelated";
        Check(PresentMarketplace(*registry,unrelated)[0].body.find(Tr("market.not_installed"))!=std::wstring::npos,"identical names never imply installed match");
        auto newer=local;newer.records[0].manifest->manifestVersion=2;newer.records[0].manifest->version="2.0.0";
        Check(PresentMarketplace(*registry,newer)[0].body.find(Tr("market.installed_newer"))!=std::wstring::npos,"strict SemVer installed comparison");
        snapshot=std::make_shared<MarketplaceSnapshot>(*snapshot);snapshot->state=MarketplaceState::Cached;snapshot->error=MarketplaceError::Fetch;page.SetMarketplace(snapshot);prepare();Check(!page.Rows().empty()&&page.MarketplaceText().find(Tr("market.cached"))!=std::wstring::npos,"cached/offline remains searchable");
        const auto pages=MakeBuiltinPageRegistry();Check(pages.Find(BuiltinPageId::Plugins)->extensionPolicy==UiExtensionPolicy::Protected,"Plugin Center remains protected");
    }
    std::cout<<"Marketplace native search/filter/detail/local isolation PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
