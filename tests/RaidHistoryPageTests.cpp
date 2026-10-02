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
    Require(page.Animating(),"selection starts detail reveal");
    for(int i=0;i<20;++i)page.Tick(0.016F);
    Require(!page.Animating(),"detail animation settles without idle work");
    Require(page.Expanded(),"selected card expands inline");
    const float cardLeft=theme.sidebarWidth+theme.contentPadding;
    page.MouseDown(cardLeft+30,258);(void)page.MouseUp(cardLeft+30,258);
    Require(!page.Expanded()&&page.Animating(),"same card click starts collapse without losing selection");
    for(int i=0;i<30;++i)page.Tick(0.016F);
    Require(!page.Animating()&&page.VisibleImages().empty(),"collapsed card stops animation and image requests");
    page.MouseDown(cardLeft+30,258);(void)page.MouseUp(cardLeft+30,258);
    Require(page.Expanded(),"same header reopens the card");
    for(int i=0;i<30;++i)page.Tick(0.016F);
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
    for(int i=0;i<30;++i)page.Tick(0.016F);
    Require(!page.Animating(),"closed menu does not keep the frame timer running");
    page.MouseDown(left+20,180);(void)page.MouseUp(left+20,180);
    Require(page.Animating(),"filter menu opening starts bounded motion");
    page.Tick(0.05F);Require(page.Key(VK_ESCAPE,false),"opening menu can reverse into closing");
    for(int i=0;i<30;++i)page.Tick(0.016F);
    Require(!page.Animating(),"reversed opening settles");
    page.Select("missing-two");page.Blur();Require(!page.Animating(),"leaving page settles all presentation motion");
    RaidHistoryPage cards;auto second=raid;second.localSessionId="local-two";
    cards.SetSessions({raid,second},{},false);cards.Prepare(1600,900,theme);cards.Select("local-one");
    for(int i=0;i<30;++i)cards.Tick(0.016F);
    cards.MouseDown(left+30,796);(void)cards.MouseUp(left+30,796);
    Require(cards.SelectedId()=="local-two"&&cards.Expanded(),"following card hit position includes inline expansion height");
    Require(cards.ListScroll()==0,"selection does not jump the viewport immediately");
    cards.Tick(0.016F);
    Require(cards.ListScroll()>0&&cards.ListScroll()<104,"selected card starts smooth top anchoring");
    for(int i=0;i<60;++i)cards.Tick(0.016F);
    Require(cards.ListScroll()==104&&!cards.Animating(),"last card reaches first visual position with bounded trailing space");
    cards.MouseDown(left+30,258);(void)cards.MouseUp(left+30,258);
    Require(!cards.Expanded(),"retargeted header closes the correct card");
    for(int i=0;i<60;++i){cards.Tick(0.016F);cards.Prepare(1600,900,theme);}
    cards.Select("local-two");cards.Tick(0.016F);
    Require(cards.Wheel(-120,left+30,300),"manual wheel overrides automatic card focus");
    const auto manualScroll=cards.ListScroll();for(int i=0;i<60;++i)cards.Tick(0.016F);
    Require(cards.ListScroll()==manualScroll,"focus motion never fights manual scrolling");
    std::cout<<"Native raid history resident-page contracts PASS\n";
}
