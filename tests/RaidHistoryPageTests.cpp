#include "ui/RaidHistoryPage.h"
#include "ui/RaidHistoryFormat.h"
#include <cstdlib>
#include <iostream>
using namespace noven::ui;
void Require(bool ok,const char* text) {if(!ok){std::cerr<<text<<'\n';std::exit(1);}}
int wmain(int argc,wchar_t** argv) {
    Require(argc==2,"locale directory");std::wstring error;
    Require(UiLocalization().DiscoverLocales(argv[1],error),"locale loading");
    const UiTheme theme;RaidHistoryPage page;
    noven::data::ItemCatalog catalog;Require(catalog.Load(std::filesystem::path(argv[1]).parent_path()/"data"/"items_catalog.tsv",error),"item catalog");
    page.SetItemCatalog(catalog);
    noven::raid::RaidSession raid;raid.localSessionId="local-one";raid.eftRaidId="eft-one";
    raid.startObserved=raid.endObserved=true;raid.mapId="reserve";
    raid.gameMode=noven::raid::GameMode::PvE;
    page.SetSessions({raid},{},false);page.Prepare(1600,900,theme);
    Require(page.Browser().Rows().size()==1&&page.Select("local-one"),"completed raid browsable/selectable");
    const auto builds=page.Browser().Builds();for(int i=0;i<1000;++i)page.Prepare(1600,900,theme);
    Require(page.Browser().Builds()==builds,"idle layout does not rebuild list");
    const float left=theme.sidebarWidth+theme.contentPadding;
    page.MouseDown(left+30,100);(void)page.MouseUp(left+30,100);
    for(wchar_t c:std::wstring(L"eft-one"))Require(page.Char(c),"shared persistent search input");
    Require(page.Browser().Rows().size()==1,"raid identity search");
    Require(page.Key(VK_LEFT,false)&&page.Char(L'x'),"shared caret input");
    Require(page.Browser().Rows().empty(),"no results state");
    Require(page.Key(VK_ESCAPE,false),"clear query");
    noven::data::RecentScanEntry scan;scan.stableItemId="item";scan.localSessionId="local-one";
    page.SetScans({scan});page.Prepare(1600,900,theme);
    Require(page.VisibleImages().size()==1,"visible linked scan requests image");
    scan.localSessionId="different";page.SetScans({scan});
    Require(page.VisibleImages().empty(),"unrelated identity never enters detail");
    page.Prepare(theme.sidebarWidth+500,700,theme);Require(page.SelectedId()=="local-one","responsive layout retains selection");
    Require(!page.Select("missing"),"missing linked raid safe");
    page.SetSessions({},std::nullopt,true);Require(page.Browser().Rows().empty(),"unavailable store does not fabricate rows");
    Require(Tr(TextKey::Unknown)==L"未知","unknown localized Chinese");
    Require(RaidDurationText(61000)==L"1 分钟 1 秒"&&RaidDurationText(std::nullopt)==L"未知","localized duration substitutes count placeholders and preserves missing values");
    Require(RaidTimeText(1767225600000)==L"2026-01-01 00:00:00","wall-clock timestamp has no timezone shift");
    Require(UiLocalization().SetLocale("en-US")&&Tr(TextKey::Unknown)==L"Unknown","unknown localized English");
    Require(RaidDurationText(61000)==L"1 min 1 s","English duration substitutes both counts");
    scan.stableItemId="66b5f22b78bbc0200425f904";
    Require(page.ScanName(scan)==L"Camelbak Tri-Zip assault backpack (MultiCam)","catalog translation rather than historical Chinese name");
    Require(!scan.fleaPrice,"localizing name never recalculates historical Unknown price");
    std::vector<noven::raid::RaidSession> maps;
    for(int i=0;i<30;++i){auto next=raid;next.mapId="map-"+std::to_string(i);maps.push_back(next);}
    page.SetSessions(std::move(maps),{},false);page.Prepare(theme.sidebarWidth+500,500,theme);
    page.MouseDown(left+20,180);(void)page.MouseUp(left+20,180);
    Require(page.Wheel(-2400,left+30,230),"bounded narrow-window map menu scrolls");
    page.MouseDown(left+30,225);(void)page.MouseUp(left+30,225);
    Require(page.Browser().Filter().mapId.has_value()&&page.Browser().Rows().size()==1,"scrolled map option selects exact identity");
    page.MouseDown(left+20,180);(void)page.MouseUp(left+20,180);
    Require(page.Key(VK_ESCAPE,false),"Escape closes a filter popup without changing the search");
    std::cout<<"Native raid history resident-page contracts PASS\n";
}
