#include "ui/EventsPage.h"
#include "ui/EventFormat.h"
#include <iostream>
#include <stdexcept>
using namespace noven::ui;
using namespace noven::events;
void Check(bool value){if(!value)throw std::runtime_error("events page assertion");}
int wmain(int argc,wchar_t** argv){try{
    Check(argc==2);std::wstring error;Check(UiLocalization().DiscoverLocales(argv[1],error));
    Microsoft::WRL::ComPtr<IDWriteFactory> factory;
    Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(factory.GetAddressOf()))));
    Microsoft::WRL::ComPtr<IDWriteTextFormat> body,label;
    Check(SUCCEEDED(factory->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,15,L"en-US",&body)));
    Check(SUCCEEDED(factory->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_BOLD,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,18,L"en-US",&label)));
    EventsPage page;UiTheme theme;EventRecord event;event.eventId="official-telegram:1";event.title="Official title";
    event.summary="Original official text";event.announcedAt=100;event.sourceStatus=EventStatus::Active;
    EventEvidence official;official.sourceKind=SourceKind::OfficialTelegram;official.sourceUrl="https://t.me/escapefromtarkovEN/1";
    EventEvidence changes;changes.sourceKind=SourceKind::TarkovChanges;changes.type=EvidenceType::ConfigurationChange;
    changes.changedKey="config/SpawnWeight";changes.oldValue="0.2";changes.newValue="0.3";changes.sourceUrl="https://changes.tarkov-changes.com/view/2";
    event.sourceEvidence={official,changes};
    page.SetSnapshot({event},{RefreshPhase::Ready,{},{}},200);Check(page.Select(event.eventId));
    const auto prepare=[&](float width){page.Prepare(width,900,theme,factory.Get(),body.Get(),label.Get());};
    prepare(1500);Check(!page.Narrow());
    Check(!page.SourceNoteBounds());
    // 活动标签直接遵循共享姿态；快速重选不跳色/跳线，离页停止计时。
    // Event tabs follow the shared pose exactly; retargeting preserves colors/underline and leaving settles timers.
    EventsPage tabsPage;tabsPage.Prepare(1500,900,theme,factory.Get(),body.Get(),label.Get());
    TabSelectionAnimation expected;expected.Select(0,5,true);
    Check(tabsPage.FilterPosition()==0&&tabsPage.FilterWeight(0)==1&&!tabsPage.Animating());
    tabsPage.SetFilter(EventStatus::Active,{});expected.Select(1,5);
    tabsPage.Tick(.05F);expected.Tick(.05F);
    Check(tabsPage.FilterPosition()==expected.Position()&&tabsPage.FilterWeight(0)==expected.Weight(0)
        &&tabsPage.FilterWeight(1)==expected.Weight(1)&&tabsPage.Animating());
    const auto position=tabsPage.FilterPosition(),weight=tabsPage.FilterWeight(1);
    tabsPage.SetFilter(EventStatus::Ended,{});expected.Select(3,5);
    Check(tabsPage.FilterPosition()==position&&tabsPage.FilterWeight(1)==weight);
    for(int i=0;i<30;++i){tabsPage.Tick(.016F);expected.Tick(.016F);
        Check(tabsPage.FilterPosition()==expected.Position());
        for(std::size_t j=0;j<5;++j)Check(tabsPage.FilterWeight(j)==expected.Weight(j));}
    Check(!tabsPage.Animating()&&tabsPage.FilterPosition()==3&&tabsPage.FilterWeight(3)==1);
    tabsPage.SetFilter(EventStatus::Upcoming,{});tabsPage.Tick(.016F);tabsPage.Blur();
    Check(!tabsPage.Animating()&&tabsPage.FilterPosition()==2&&tabsPage.FilterWeight(2)==1);
    const float tabLeft=theme.sidebarWidth+theme.contentPadding,tabRight=1500-theme.contentPadding;
    const auto click=[&](float x,float y){tabsPage.MouseDown(x,y);tabsPage.MouseUp(x,y);};
    click(tabRight,140);Check(tabsPage.Browser().Filter()==EventStatus::Upcoming);
    click(tabLeft+1,170);Check(tabsPage.Browser().Filter()==EventStatus::Upcoming);
    click(tabLeft+1,140);Check(!tabsPage.Browser().Filter()&&tabsPage.Animating());
    Check(page.LastRefreshText().find(L"{time}")==std::wstring::npos&&page.LastRefreshText().find(EventTimeText(200))!=std::wstring::npos);
    Check(EventEvidencePreview(std::string(1024,'x')).size()==161);
    Check(page.OfficialText().size()>=4&&page.EvidenceText().size()==1);
    for(const auto& text:page.OfficialText())Check(text.find(L"SpawnWeight")==text.npos);
    Check(EventTimeText({})==Tr(TextKey::Unknown)&&EventScopeText(event)==Tr(TextKey::EventUnspecified));
    Check(!page.Browser().Find(event.eventId)->startsAt&&!page.Browser().Find(event.eventId)->endsAt);
    Check(std::ranges::any_of(page.OfficialText(),[](const auto& text){return text.find(Tr(TextKey::EventEnds)+L": "+Tr(TextKey::Unknown))!=text.npos;}));
    const auto rows=page.Browser().Rows();page.SetFilter({},L"official");prepare(1500);
    page.Wheel(-240,1000,700);for(int i=0;i<200;++i)page.Tick(.016F);Check(!page.Animating());
    const auto detailScroll=page.DetailScroll();const auto builds=page.Browser().Builds();
    for(int i=0;i<100;++i)prepare(1500);Check(builds==page.Browser().Builds());
    page.Blur();prepare(1500);Check(page.SelectedId()==event.eventId&&page.Search().Text()==L"official"&&page.DetailScroll()==detailScroll);
    page.SetSnapshot({event},{RefreshPhase::Failed,"offline",{}},200);prepare(1500);
    Check(page.RefreshText()==Tr(TextKey::EventCached)&&page.Browser().Rows()==rows);
    prepare(800);Check(page.Narrow()&&page.ShowingDetail());
    page.MouseDown(330,260);Check(!page.MouseUp(330,260));prepare(800);
    Check(!page.ShowingDetail()&&page.SelectedId()==event.eventId&&page.Search().Text()==L"official");
    Check(page.Select(event.eventId));prepare(800);Check(page.ShowingDetail());
    page.SetSnapshot({event},{RefreshPhase::Refreshing,{},{}},200);prepare(800);
    page.MouseDown(730,210);Check(!page.MouseUp(730,210));
    page.SetSnapshot({event},{RefreshPhase::Ready,{},{}},200);prepare(800);
    page.MouseDown(730,210);const auto refreshAction=page.MouseUp(730,210);
    Check(refreshAction&&refreshAction->kind==EventAction::Kind::Refresh);
    auto other=event;other.eventId="official-telegram:2";
    page.SetSnapshot({event,other},{RefreshPhase::Ready,{},{}},200);
    Check(page.Select(other.eventId));page.Tick(.016F);const auto opacity=page.DetailOpacity();
    Check(page.Select(event.eventId)&&page.DetailOpacity()==opacity);
    for(int i=0;i<200;++i)page.Tick(.016F);Check(!page.Animating()&&page.DetailOpacity()==1);
    page.SetFilter(EventStatus::Ended,{});prepare(800);Check(page.Browser().Rows().empty()&&!page.Browser().Events().empty());
    page.SetSnapshot({},{RefreshPhase::Failed,"offline",{}},{});prepare(800);Check(page.RefreshText()==Tr(TextKey::EventUnavailable));
    page.SetSnapshot({},{RefreshPhase::Ready,{},{}},{});Check(page.RefreshText()==Tr(TextKey::EventEmpty));
    Check(page.LastRefreshText().empty());
    EventRecord wiki;wiki.eventId="community-wiki:26936:Test";wiki.title="Community title";
    wiki.summary="Community description";wiki.sourceStatus=EventStatus::Active;wiki.lastUpdatedAt=200;
    EventEvidence wikiEvidence;wikiEvidence.sourceKind=SourceKind::CommunityWiki;wikiEvidence.sourceUrl=WikiEventUrl("26936:Test");
    wiki.sourceEvidence={wikiEvidence};
    page.SetSnapshot({wiki},{RefreshPhase::Ready,{},{},"Wiki failed"},200);page.SetFilter({},{});
    Check(page.Select(wiki.eventId));prepare(1500);
    Check(page.OfficialText().empty() && page.EvidenceText().empty());
    // 来源说明只出现在详情最末，字号小于正文，来源链接仍可用。
    // Attribution appears only at the detail footer, smaller than body text, with the source action intact.
    const auto note=page.SourceNoteBounds(),source=page.ActionBounds(EventAction::Kind::Source,wikiEvidence.sourceUrl);
    Check(note&&source&&note->top>source->bottom);
    Check(page.SourceNoteFontSize()>0&&page.SourceNoteFontSize()<body->GetFontSize());
    Check(!page.Browser().Find(wiki.eventId)->announcedAt && !page.Browser().Find(wiki.eventId)->startsAt);
    Check(page.RefreshText()==Tr(TextKey::EventPartialRefresh));
    event.sourceEvidence.push_back(wikiEvidence);page.SetSnapshot({event},{RefreshPhase::Ready,{},{}},200);
    Check(page.Select(event.eventId));prepare(1500);
    Check(page.SourceNoteBounds()&&page.SourceNoteFontSize()<body->GetFontSize());
    for(const auto& text:page.OfficialText())Check(text.find(Tr(TextKey::EventCommunityHint))==text.npos);
    Check(UiLocalization().SetLocale("en-US"));Check(EventTimeText({})==L"Unknown");
    page.SetSnapshot({event},{RefreshPhase::Ready,{},{}},200);
    Check(page.LastRefreshText()==L"Last successful check: "+EventTimeText(200));
    Check(EventTimeText(-1)==L"Unknown"&&EventTimeText(253402300800LL)==L"Unknown");
    std::cout<<"Native event presentation PASS (not visual acceptance)\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
