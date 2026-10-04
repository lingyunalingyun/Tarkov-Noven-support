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
    Check(UiLocalization().SetLocale("en-US"));Check(EventTimeText({})==L"Unknown");
    Check(EventTimeText(-1)==L"Unknown"&&EventTimeText(253402300800LL)==L"Unknown");
    std::cout<<"Native event presentation PASS (not visual acceptance)\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
