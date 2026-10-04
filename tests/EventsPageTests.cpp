#include "ui/EventsPage.h"
#include "ui/EventFormat.h"
#include <iostream>
#include <stdexcept>
#include <source_location>
using namespace noven::ui;
using namespace noven::events;
void Check(bool value,std::source_location location=std::source_location::current()){
    if(!value)throw std::runtime_error("events page assertion at line "+std::to_string(location.line()));
}
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
    // 生产共享字体垂直居中时，独立标题仍顶端对齐、两行省略；静止帧复用排版。
    // Independent titles stay top-aligned and two-line trimmed with centered production fonts; idle frames reuse layouts.
    label->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    auto longEvent=event;longEvent.title=std::string(600,'W');
    EventsPage titlePage;titlePage.SetSnapshot({longEvent},{RefreshPhase::Ready,{},{}},200);
    titlePage.Prepare(1500,900,theme,factory.Get(),body.Get(),label.Get());
    auto* titleLayout=titlePage.RowTitleLayout(event.eventId);Check(titleLayout);
    Check(titleLayout->GetParagraphAlignment()==DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    Check(titleLayout->GetTextAlignment()==DWRITE_TEXT_ALIGNMENT_LEADING);
    DWRITE_TEXT_METRICS titleMetrics{};Check(SUCCEEDED(titleLayout->GetMetrics(&titleMetrics)));
    Check(titleMetrics.lineCount<=2&&titleMetrics.height<=50);
    DWRITE_TRIMMING trimming{};Microsoft::WRL::ComPtr<IDWriteInlineObject> trimmingSign;
    titleLayout->GetTrimming(&trimming,&trimmingSign);
    Check(trimming.granularity==DWRITE_TRIMMING_GRANULARITY_CHARACTER);
    for(int i=0;i<20;++i)titlePage.Prepare(1500,900,theme,factory.Get(),body.Get(),label.Get());
    Check(titlePage.RowTitleLayout(event.eventId)==titleLayout);
    Check(label->GetParagraphAlignment()==DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    const auto oldTitleWidth=titleLayout->GetMaxWidth();
    titlePage.Prepare(800,900,theme,factory.Get(),body.Get(),label.Get());
    Check(titlePage.RowTitleLayout(event.eventId)->GetMaxWidth()>oldTitleWidth);
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
    EventsPage scrollPage;std::vector<EventRecord> many;
    for(int i=0;i<15;++i){auto copy=event;copy.eventId="official-telegram:"+std::to_string(i+10);many.push_back(copy);}
    scrollPage.SetSnapshot(many,{RefreshPhase::Ready,{},{}},200);
    scrollPage.Prepare(1500,900,theme,factory.Get(),body.Get(),label.Get());
    const float listLeft=theme.sidebarWidth+theme.contentPadding;
    Check(scrollPage.Wheel(-120,listLeft+20,500));Check(scrollPage.ListScroll()==0&&scrollPage.Animating());
    scrollPage.Tick(.016F);Check(scrollPage.ListScroll()>0&&scrollPage.ListScroll()<100);
    for(int i=0;i<200;++i)scrollPage.Tick(.016F);Check(!scrollPage.Animating());
    const auto beforeFilter=scrollPage.ListScroll();scrollPage.SetFilter({},{});
    Check(scrollPage.ListScroll()==beforeFilter&&scrollPage.ListOpacity()<1);
    const auto listOpacity=scrollPage.ListOpacity();scrollPage.SetFilter(EventStatus::Active,{});
    Check(scrollPage.ListOpacity()==listOpacity);
    for(int i=0;i<200;++i)scrollPage.Tick(.016F);
    Check(!scrollPage.Animating()&&scrollPage.ListScroll()==0&&scrollPage.ListOpacity()==1);
    const float listRight=listLeft+std::clamp((1500-theme.contentPadding-listLeft)*.32F,230.0F,320.0F);
    scrollPage.MouseDown(listRight-7,820);scrollPage.MouseUp(listRight-7,820);
    Check(scrollPage.ListScroll()==0&&scrollPage.Animating());
    scrollPage.Tick(.016F);Check(scrollPage.ListScroll()>0);
    scrollPage.Blur();Check(!scrollPage.Animating());
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
    Check(UiLocalization().SetLocale("zh-CN"));
    event.machineText={{event.title,"活动标题"},{event.summary,"活动正文"}};
    page.SetSnapshot({event},{RefreshPhase::Ready,{},{}},200);prepare(1500);
    Check(page.ContentText(event.title)==L"活动标题"&&page.ContentText(event.summary)==L"活动正文");
    Check(std::ranges::find(page.OfficialText(),L"Original official text")!=page.OfficialText().end());
    const auto toggle=page.ActionBounds(EventAction::Kind::Original,{});Check(toggle.has_value());
    // 操作文字垂直居中且沿用左内边距，不影响正文布局或共享字体格式。
    // Action text is vertically centered with shared left padding, leaving prose and font formats untouched.
    const auto checkActionAlignment=[&](EventAction::Kind kind,std::string_view id){
        const auto button=page.ActionBounds(kind,id),text=page.ActionTextBounds(kind,id);Check(button&&text);
        Check(std::abs((button->top+button->bottom)-(text->top+text->bottom))<.01F);
        Check(std::abs(text->left-button->left-8)<.01F&&text->top>=button->top&&text->bottom<=button->bottom);
    };
    checkActionAlignment(EventAction::Kind::Original,{});
    checkActionAlignment(EventAction::Kind::Source,official.sourceUrl);
    page.MouseDown(toggle->left+10,toggle->top+10);Check(!page.MouseUp(toggle->left+10,toggle->top+10));prepare(1500);
    Check(page.ContentText(event.summary)==L"Original official text");
    // 原文/译文高度改变时滑块长度和位置接续；快速反向切换、淡出与离页均可落定。
    // Thumb size and position continue across text-height changes; rapid reversal, fade-out and blur settle safely.
    EventsPage barPage;auto barEvent=event;
    barEvent.summary.clear();for(int i=0;i<90;++i)barEvent.summary+="Long original announcement line.\n";
    barEvent.machineText={{barEvent.summary,"简短译文"}};
    barPage.SetSnapshot({barEvent},{RefreshPhase::Ready,{},{}},200);Check(barPage.Select(barEvent.eventId));
    const auto prepareBar=[&](float height=700){barPage.Prepare(1500,height,theme,factory.Get(),body.Get(),label.Get());};
    const auto settleBar=[&]{for(int i=0;i<100;++i)barPage.Tick(.016F);};
    const auto toggleBar=[&]{const auto r=barPage.ActionBounds(EventAction::Kind::Original,{});Check(r.has_value());
        barPage.MouseDown(r->left+8,r->top+8);barPage.MouseUp(r->left+8,r->top+8);prepareBar();};
    const auto sameThumb=[](const auto& a,const auto& b){return a.bar&&b.bar&&
        std::abs(a.bar->thumb.top-b.bar->thumb.top)<.001F&&std::abs(a.bar->thumb.bottom-b.bar->thumb.bottom)<.001F;};
    prepareBar();settleBar();const auto translatedBar=barPage.DetailBarPose();Check(translatedBar.bar.has_value());
    toggleBar();Check(barPage.Animating()&&sameThumb(translatedBar,barPage.DetailBarPose()));
    barPage.Tick(.05F);const auto intermediateBar=barPage.DetailBarPose();
    Check(intermediateBar.bar&&!sameThumb(translatedBar,intermediateBar));
    toggleBar();Check(sameThumb(intermediateBar,barPage.DetailBarPose()));
    settleBar();Check(!barPage.Animating()&&sameThumb(translatedBar,barPage.DetailBarPose()));
    toggleBar();settleBar();const auto originalBar=barPage.DetailBarPose();
    Check(originalBar.bar&&!sameThumb(originalBar,translatedBar));
    Check(originalBar.bar->thumb.bottom-originalBar.bar->thumb.top<translatedBar.bar->thumb.bottom-translatedBar.bar->thumb.top);
    prepareBar(5000);Check(barPage.DetailBarPose().bar&&barPage.Animating());
    settleBar();Check(!barPage.DetailBarPose().bar&&!barPage.Animating());
    prepareBar();settleBar();toggleBar();barPage.Blur();Check(!barPage.Animating());
    Check(UiLocalization().SetLocale("en-US"));prepare(1500);Check(!page.ActionBounds(EventAction::Kind::Original,{}));
    std::cout<<"Native event presentation PASS (not visual acceptance)\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
