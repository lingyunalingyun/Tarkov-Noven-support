#include "ui/RaidHistoryPage.h"
#include <cstdlib>
#include <iostream>
using namespace noven::ui;
void Require(bool ok,const char* text) {if(!ok){std::cerr<<text<<'\n';std::exit(1);}}
int wmain(int argc,wchar_t** argv) {
    Require(argc==2,"locale directory");std::wstring error;
    Require(UiLocalization().DiscoverLocales(argv[1],error),"locale loading");
    const UiTheme theme;RaidHistoryPage page;
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
    Require(UiLocalization().SetLocale("en-US")&&Tr(TextKey::Unknown)==L"Unknown","unknown localized English");
    std::cout<<"Native raid history resident-page contracts PASS\n";
}
