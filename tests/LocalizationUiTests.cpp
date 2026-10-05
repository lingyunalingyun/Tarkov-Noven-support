#include "ui/MainWindowUi.h"
#include "ui/Dropdown.h"
#include "ui/NavigationButton.h"
#include "ui/PricePagination.h"
#include "ui/OverflowText.h"
#include "ui/SearchBox.h"
#include "ui/ExpandableCard.h"
#include "ui/LineChart.h"
#include "ui/ItemTypeLabel.h"
#include "ui/ValueFormat.h"
#include "ui/localization/LocalizationService.h"
#include <cstdlib>
#include <iostream>

void Require(bool value, const char* message) {
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
}
int wmain(int argc, wchar_t** argv) try {
    using namespace noven::ui;
    const auto pagerTrack = D2D1::RectF(100, 200, 112, 700);
    const auto fullPageBar = MakeScrollbar(pagerTrack, 3600, 3100);
    const auto shortPageBar = MakeScrollbar(pagerTrack, 1320, 0);
    PriceTabTransition settledMode;
    PriceSearchTransition pageReflow;
    pageReflow.progress = 0;
    pageReflow.outgoingScrollbar = {fullPageBar, 0.4F};
    const auto startBar = SamplePriceScrollbar(settledMode, pageReflow, {}, shortPageBar);
    Require(startBar.bar && startBar.bar->thumb.top == fullPageBar->thumb.top
        && startBar.bar->thumb.bottom == fullPageBar->thumb.bottom && startBar.opacity == 0.4F,
        "page scrollbar starts at its old position, size and opacity even with settled mode tabs");
    pageReflow.progress = 0.5F;
    const auto halfwayBar = SamplePriceScrollbar(settledMode, pageReflow, {}, shortPageBar);
    Require(halfwayBar.bar && halfwayBar.bar->thumb.top < fullPageBar->thumb.top
        && halfwayBar.bar->thumb.bottom - halfwayBar.bar->thumb.top
            > fullPageBar->thumb.bottom - fullPageBar->thumb.top,
        "last-page scrollbar animates both thumb position and length with list progress");
    PriceSearchTransition retargeted;
    retargeted.progress = 0;
    retargeted.outgoingScrollbar = halfwayBar;
    const auto retargetBar = SamplePriceScrollbar(settledMode, retargeted, {}, fullPageBar);
    Require(retargetBar.bar->thumb.top == halfwayBar.bar->thumb.top
        && retargetBar.bar->thumb.bottom == halfwayBar.bar->thumb.bottom
        && retargetBar.opacity == halfwayBar.opacity,
        "rapid paging retains the sampled visual thumb rather than snapping to a previous target");
    pageReflow.progress = 1;
    const auto settledBar = SamplePriceScrollbar(settledMode, pageReflow, {}, shortPageBar);
    Require(settledBar.bar->thumb.top == shortPageBar->thumb.top
        && settledBar.bar->thumb.bottom == shortPageBar->thumb.bottom && settledBar.opacity == 1,
        "scrollbar settles at the actual page geometry");
    pageReflow.progress = 0;
    Require(SamplePriceScrollbar(settledMode, pageReflow, {}, {}).opacity == 0.4F,
        "a scrollbar becoming unnecessary fades from the captured opacity");
    pageReflow.progress = 1;
    Require(!SamplePriceScrollbar(settledMode, pageReflow, {}, {}).bar,
        "a fitting page removes its scrollbar after the transition");
    Require(PricePageCount(0) == 1 && PricePageCount(30) == 1
        && PricePageCount(31) == 2 && PricePageCount(120) == 4,
        "price pagination handles empty, exact and partial pages");
    Require(PricePageFromInput(L"2", 61) == 1 && PricePageFromInput(L"0", 61) == 0
        && PricePageFromInput(L"999999999999999999999", 61) == 2
        && PricePageFromInput(L"0002", 61) == 1
        && !PricePageFromInput(L"", 61) && !PricePageFromInput(L"2x", 61)
        && PricePageFromInput(L"999", 0) == 0,
        "numeric page jumps clamp bounds safely and reject empty or nonnumeric values");
    for (float width : {240.0F, 900.0F}) {
        const auto rect = D2D1::RectF(400, 200, 400 + width, 236);
        Require(!HitPricePager(rect, 401, 210, 0, 31)
            && HitPricePager(rect, 399 + width, 210, 0, 31) == 1
            && HitPricePager(rect, 401, 210, 1, 31) == -1
            && !HitPricePager(rect, 399 + width, 210, 1, 31)
            && HitPricePager(rect, 400 + width / 2, 210, 1, 31) == 0
            && !HitPricePager(rect, 401, 237, 1, 31),
            "pager shared geometry disables first/last and clips hits at all widths");
    }
    const std::vector<TabBarItem<int>> dynamicTabs{{1,L"One"},{2,L"Two"}};
    Require(HitTestTabBar<int>(dynamicTabs,{100,20,60,100,20},250,30)==2
        &&!HitTestTabBar<int>(dynamicTabs,{100,20,60,100,20},300,30),"dynamic tabs preserve shared hit boundaries");
    Require(FormatSignedRoubles(2000) == L"+₽2,000"
        && FormatSignedRoubles(-1234.5) == L"−₽1,234.5"
        && FormatSignedRoubles(0) == L"₽0"
        && FormatSignedRoubles(1.125) == L"+₽1.12",
        "shared signed amount formatter preserves sign, grouping, zero and display precision");
    Require(ItemTypeKey({"container", "noFlea"}) == TextKey::TypeContainer,
        "upstream container type produces a container badge");
    Require(ItemTypeKey({"wearable", "gun"}) == TextKey::TypeGun
        && ItemTypeKey({"grenade", "ammo"}) == TextKey::TypeGrenade,
        "specific badges precede broad upstream types regardless of input order");
    Require(ItemTypeKey({"ammo"}) == TextKey::TypeAmmo
        && ItemTypeKey({"wearable"}) == TextKey::TypeWearable,
        "ammo and equipment types are supported");
    Require(ItemTypeKey({"noFlea", "markedOnly", "future-type"}).empty()
        && ItemTypeKey({}).empty(), "unknown types and market flags do not fabricate categories");
    ExpandableCardState reusableCard;
    reusableCard.Retarget(true, 9);
    reusableCard.Advance(0.15F, 200);
    Require(reusableCard.extent > 0 && reusableCard.extent < 200,
        "generic card accepts caller-owned content height without history data");
    const float retainedHeight = reusableCard.extent;
    reusableCard.Retarget(false, 9);
    reusableCard.Advance(0, 200);
    Require(reusableCard.extent == retainedHeight && reusableCard.ScrollTarget(100) == 900,
        "generic card retarget preserves pose and exposes independent scroll target");
    reusableCard.Advance(0.30F, 200);
    Require(reusableCard.extent == 0 && reusableCard.expansionProgress == 1,
        "generic card settles in 300ms");
    Require(SamplePolylineMorph({}, {}, 0).empty(), "generic empty chart is safe");
    const std::vector<D2D1_POINT_2F> genericLine{{0, 0.2F}, {1, 0.8F}};
    Require(SamplePolylineMorph({}, genericLine, 0).size() == 2
        && ChartTimePosition(10, 10, 10) == 0.5F,
        "generic chart supports first data and single timestamp without price services");
    PriceDetailsState expansion;
    PriceDetailsState anchored;
    anchored.id = "test";
    anchored.open = true;
    for (std::size_t index : {0U, 4U, 9U}) {
        anchored.rowIndex = index;
        const float extra = DetailsScrollExtra(anchored, 10, 120, 800);
        Require(1200 + extra - 800 >= index * 120,
            "first middle and last cards can reach viewport top before expansion completes");
    }
    anchored.open = false;
    Require(DetailsScrollExtra(anchored, 10, 120, 800) == 0,
        "closed details release trailing scroll space");
    expansion.open = true;
    expansion.expansionProgress = 0;
    for (int i = 0; i < 60; ++i) {
        const float previous = expansion.extent;
        AdvanceDetailsExpansion(expansion, 0.016F);
        Require(expansion.extent >= previous && expansion.extent <= kPriceDetailsHeight,
            "card opens monotonically without overshoot");
    }
    Require(expansion.extent == kPriceDetailsHeight,
        "card settles at exact layout height");
    expansion.open = false;
    expansion.extentFrom = expansion.extent;
    expansion.expansionProgress = 0;
    AdvanceDetailsExpansion(expansion, 0.15F);
    const float interruptedHeight = expansion.extent;
    expansion.open = true;
    expansion.extentFrom = expansion.extent;
    expansion.expansionProgress = 0;
    AdvanceDetailsExpansion(expansion, 0);
    Require(expansion.extent == interruptedHeight, "rapid toggle preserves current card height");
    expansion.open = false;
    for (int i = 0; i < 60; ++i) {
        const float previous = expansion.extent;
        AdvanceDetailsExpansion(expansion, 0.016F);
        Require(expansion.extent >= 0 && expansion.extent <= previous,
            "card closes monotonically without bounce");
    }
    Require(expansion.extent == 0 && expansion.expansionProgress == 1,
        "closing settles and stops the animation timer");
    PriceDetailsState historyAnimation;
    historyAnimation.days = 90;
    historyAnimation.tabProgress = historyAnimation.chartProgress = 0;
    AdvanceHistoryTransition(historyAnimation, 0.1F);
    Require(historyAnimation.tabProgress > 0 && historyAnimation.tabProgress < 1
        && historyAnimation.chartProgress > 0 && historyAnimation.chartProgress < 1,
        "range and chart animations advance independently");
    for (int i = 0; i < 60; ++i) AdvanceHistoryTransition(historyAnimation, 0.016F);
    Require(historyAnimation.underlineIndex == 2 && historyAnimation.tabProgress == 1
        && historyAnimation.chartProgress == 1, "history animation settles and stops");
    const auto moving = AdvanceHistoryHover(0, 5, 0.016F);
    PriceDetailsState morph;
    morph.chartFrom = {{0.1F, 0.2F}, {0.5F, 1.0F}, {0.9F, 0.4F}};
    morph.chartTo = {{0.5F, 0.6F}, {1.0F, 0.8F}};
    morph.chartProgress = 0;
    auto pose = HistoryLinePose(morph);
    Require(pose.size() == 3 && pose[1].y == 1.0F && pose.front().x == 0.1F,
        "chart morph starts at the intact old curve with unequal sample counts");
    AdvanceHistoryTransition(morph, 0.1F);
    pose = HistoryLinePose(morph);
    Require(pose.front().x != 0.1F && pose.front().x != 0.5F,
        "chart vertices move continuously rather than fading");
    morph.chartFrom = pose;
    morph.chartTo = {{0.2F, 0.1F}};
    morph.chartProgress = 0;
    Require(HistoryLinePose(morph).front().x == pose.front().x,
        "interrupted morph resumes from the current visual pose");
    for (int i = 0; i < 60; ++i) AdvanceHistoryTransition(morph, 0.016F);
    Require(HistoryLinePose(morph).size() == 1 && HistoryLinePose(morph)[0].x == 0.2F,
        "chart morph settles at exact target samples including single-point history");
    noven::data::HistorySnapshot pending;
    pending.loading = true;
    SetHistoryChart(morph, pending);
    Require(HistoryLinePose(morph).size() == 1, "pending data preserves the visible curve");
    pending.loading = false;
    SetHistoryChart(morph, pending);
    Require(HistoryLinePose(morph).empty(), "confirmed empty history never fabricates a curve");
    Require(moving > 0 && moving < 5, "history marker eases toward target");
    Require(AdvanceHistoryHover(moving, 0, 0.016F) < moving,
        "history marker reverses from current visual position");
    float settledMarker = 0;
    for (int i = 0; i < 90; ++i) settledMarker = AdvanceHistoryHover(settledMarker, 5, 0.016F);
    Require(settledMarker == 5, "history hover timer settles exactly and stops");
    tm sampleDay{}; sampleDay.tm_year = 126; sampleDay.tm_mon = 8; sampleDay.tm_mday = 20;
    sampleDay.tm_hour = 12; sampleDay.tm_isdst = -1;
    const auto noon = static_cast<std::int64_t>(mktime(&sampleDay)) * 1000;
    noven::data::HistorySnapshot hoverData;
    hoverData.days = 7; hoverData.asOfMs = noon + 2 * noven::data::kHistoryDayMs;
    hoverData.points = {{noon, 100}, {noon + 3600000, 300}, {noon + 7200000, 200}};
    const auto hoverPlot = D2D1::RectF(0, 0, 700, 100);
    const auto hoverAt = HistoryPlotPoint(hoverData.points[1],
        hoverData.points.front().timeMs, hoverData.points.back().timeMs, 300, hoverPlot);
    const auto hover = HitHistory(hoverData, hoverPlot, hoverAt);
    Require(hover && hover->low == 100 && hover->average == 200 && hover->high == 300
        && hover->count == 3 && hover->point.timeMs == hoverData.points[1].timeMs,
        "hover reports nearest point and local-day sample extrema and mean");
    Require(!HitHistory(hoverData, hoverPlot, D2D1::Point2F(-1, 50))
        && !HitHistory(hoverData, hoverPlot, D2D1::Point2F(50, 101)),
        "outside chart has no tooltip");
    const auto gap = HitHistory(hoverData, hoverPlot, D2D1::Point2F(50, 99));
    Require(gap && gap->point.timeMs == noon && gap->count == 3,
        "empty horizontal interval snaps to nearest sample day regardless of cursor height");
    const auto rightEdge = HitHistory(hoverData, hoverPlot, D2D1::Point2F(700, 0));
    Require(rightEdge && rightEdge->point.timeMs == noon + 7200000,
        "right edge snaps to last sample");
    auto spaced = hoverData;
    spaced.points = {{noon, 100}, {noon + noven::data::kHistoryDayMs, 400}};
    const auto later = HitHistory(spaced, hoverPlot,
        HistoryPlotPoint({noon + 18 * 3600000, 200},
            spaced.points.front().timeMs, spaced.points.back().timeMs, 400, hoverPlot));
    Require(later && later->point.timeMs == noon + noven::data::kHistoryDayMs
        && later->low == 400 && later->count == 1, "nearest X sample owns tooltip day and statistics");
    hoverData.points = {{noon, 1234}};
    const auto single = HitHistory(hoverData, hoverPlot,
        HistoryPlotPoint(hoverData.points[0], noon, noon, 1234, hoverPlot));
    Require(single && single->low == 1234 && single->average == 1234 && single->high == 1234,
        "single daily sample has equal extrema and mean");
    Require(HistoryMoney(1234567) == L"₽1,234,567", "hover prices use thousands separators");
    const auto plot = D2D1::RectF(10, 20, 210, 120);
    for (const float width : {200.0F, 1000.0F}) {
        const auto resized = D2D1::RectF(10, 20, 10 + width, 120);
        Require(HistoryPlotPoint({1000, 50}, 1000, 9000, 100, resized).x == resized.left
            && HistoryPlotPoint({9000, 100}, 1000, 9000, 100, resized).x == resized.right,
            "actual sample endpoints fill both edges at every plot width");
    }
    Require(HistoryPlotPoint({1000, 50}, 1000, 1000, 100, plot).x == 110,
        "single timestamp stays centered without division by zero");
    PriceDetailsState fitted;
    SetHistoryChart(fitted, spaced);
    Require(fitted.chartTo.front().x == 0 && fitted.chartTo.back().x == 1,
        "animated chart uses the same fitted endpoints as hover mapping");
    const auto firstPoint = HistoryPlotPoint({1000, 50}, 0, 10000, 100, plot);
    const auto lastPoint = HistoryPlotPoint({9000, 100}, 0, 10000, 100, plot);
    Require(firstPoint.x == 30 && firstPoint.y == 70
        && lastPoint.x == 190 && lastPoint.y == 20,
        "history line uses real timestamps and raw sample prices without endpoint extrapolation");
    Require(HistoryPlotPoint({5000, 100}, 0, 10000, 100, plot).y == lastPoint.y,
        "constant-price samples remain horizontal");
    const auto filtered = SampleListReflow(3, 3, 1, false, 0.28F);
    Require(filtered.opacity == 0 && filtered.slot == 3, "filtered cards fade before reflow");
    Require(SampleListReflow(3, 0, 1, true, 0.28F).slot == 3,
        "matching cards hold position during fade phase");
    const auto settled = SampleListReflow(3, 0, 0.5F, true, 1);
    Require(settled.slot == 0 && settled.opacity == 1, "matching cards settle at target");
    Require(SampleListReflow(3, 0, 1, true, 0.55F).slot < 0, "reflow spring overshoots gently");
    const auto interrupted = SampleListReflow(2, 0, 0.4F, true, 0.5F);
    const auto restarted = SampleListReflow(interrupted.slot, 1, interrupted.opacity, true, 0);
    Require(restarted.slot == interrupted.slot && restarted.opacity == interrupted.opacity,
        "interrupted reflow retains current pose");
    Microsoft::WRL::ComPtr<IDWriteFactory> textFactory;
    PageTransition<PageId> pageMotion;
    const auto start=PageTransition<PageId>::Clock::time_point{};
    Require(!pageMotion.Active() && pageMotion.Sample(BuiltinPageId::Scanner).opacity==1,"page transition starts settled");
    pageMotion.Start(BuiltinPageId::Scanner,start);
    pageMotion.Tick(start+std::chrono::milliseconds(90));
    const auto outgoing=pageMotion.Sample(BuiltinPageId::Prices);
    Require(outgoing.page==BuiltinPageId::Scanner && outgoing.opacity>0 && outgoing.opacity<1 && outgoing.shift<0,"old page fades and moves");
    pageMotion.Start(BuiltinPageId::Prices,start+std::chrono::milliseconds(90));
    Require(pageMotion.Sample(BuiltinPageId::Tasks).opacity==outgoing.opacity
        && pageMotion.ShowingOutgoing(BuiltinPageId::Scanner),"rapid retarget preserves outgoing identity and opacity");
    pageMotion.Tick(start+std::chrono::milliseconds(290));
    const auto incoming=pageMotion.Sample(BuiltinPageId::Tasks);
    Require(incoming.page==BuiltinPageId::Tasks && incoming.opacity>0 && incoming.opacity<1 && incoming.shift>0,"new page enters after fade");
    pageMotion.Tick(start+std::chrono::milliseconds(500));
    Require(!pageMotion.Active() && !pageMotion.ShowingOutgoing(BuiltinPageId::Scanner)
        && pageMotion.Sample(BuiltinPageId::Tasks).opacity==1 && pageMotion.Sample(BuiltinPageId::Tasks).shift==0,"page transition releases outgoing and settles");
    Require(HitNavigationButton(D2D1::RectF(10,20,34,64),10,20)
        && !HitNavigationButton(D2D1::RectF(10,20,34,64),34,20)
        && !HitNavigationButton(D2D1::RectF(10,20,34,64),20,30,false),"navigation button shares bounds and disabled state");
    const float overflowDuration=(std::min)(6.0F,1.8F+84.0F/42.0F);
    const float overflowCycle=0.75F+overflowDuration+2.5F;
    Require(SampleOverflowTextOffset(84.0F,0.5F)==0.0F
        && SampleOverflowTextOffset(84.0F,0.75F+overflowDuration+2.0F)==84.0F,
        "overflow text pauses at both endpoints");
    Require(std::abs(SampleOverflowTextOffset(84.0F,overflowCycle+1.2F)
        -SampleOverflowTextOffset(84.0F,1.2F))<0.01F,
        "overflow text repeats continuously after its end pause");
    const auto imeAnchor = SearchImeAnchor(D2D1::Point2F(100, 40),
        D2D1::Matrix3x2F::Translation(0, 8), 144, 144);
    Require(imeAnchor.x == 150 && imeAnchor.y == 72,
        "IME anchor includes DPI and page transform");
    SearchBox editor; editor.SetText(L"电路板"); editor.Focus();
    Require(editor.HandleKeyDown(VK_LEFT,false) && editor.Caret()==2,"left moves caret");
    Require(editor.HandleChar(L'新') && editor.Text()==L"电路新板","insert at caret");
    Require(editor.HandleKeyDown(VK_BACK,false) && editor.Text()==L"电路板" && editor.Caret()==2,"backspace before caret");
    Require(editor.HandleKeyDown(VK_RIGHT,false) && editor.Caret()==3,"right moves caret");
    editor.SetText(L"A\U0001F600B");
    Require(editor.HandleKeyDown(VK_LEFT,false) && editor.HandleKeyDown(VK_LEFT,false),
        "left handles surrogate-pair navigation");
    Require(editor.Caret()==1,"left skips surrogate pair");
    Require(editor.HandleKeyDown(VK_RIGHT,false) && editor.HandleKeyDown(VK_BACK,false),
        "right and Backspace handle surrogate-pair navigation");
    Require(editor.Text()==L"AB" && editor.Caret()==1,"backspace removes whole surrogate pair");
    Require(editor.HandleKeyDown('A',true) && editor.HandleKeyDown(VK_LEFT,false),
        "Ctrl+A selection collapses with Left");
    Require(editor.Caret()==0,"left collapses select all to start");
    Require(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(textFactory.GetAddressOf()))), "search text factory");
    Microsoft::WRL::ComPtr<IDWriteTextFormat> searchFormat;
    Require(SUCCEEDED(textFactory->CreateTextFormat(L"Microsoft YaHei UI", nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        16, L"zh-CN", &searchFormat)), "search text format");
    for (const std::wstring_view query : {L"", L"啊实打实大苏打啊", L"WiWi NL545",
            L"中文 Tri-Zip  ", L"超长搜索文字超长搜索文字超长搜索文字"}) {
        for (const float width : {60.0F, 600.0F}) {
            const auto layout = LayoutSearchText(*textFactory.Get(), *searchFormat.Get(), query, width, 38);
            Require(layout.layout != nullptr, "search layout created");
            DWRITE_TEXT_METRICS metrics{};
            layout.layout->GetMetrics(&metrics);
            Require(std::abs(layout.caretX - metrics.widthIncludingTrailingWhitespace) < 0.1F,
                "caret follows shaped text including Chinese and trailing spaces");
            Require(layout.caretX - layout.scrollX >= 0 && layout.caretX - layout.scrollX <= width - 1,
                "end caret stays in viewport for long queries");
        }
    }
    const auto middleCaret=LayoutSearchText(*textFactory.Get(),*searchFormat.Get(),L"电路板",600,38,1);
    const auto prefixCaret=LayoutSearchText(*textFactory.Get(),*searchFormat.Get(),L"电",600,38);
    Require(std::abs(middleCaret.caretX-prefixCaret.caretX)<.1F,"middle caret uses shaped text position");
    const DropdownLayout menu{D2D1::RectF(10, 20, 128, 52)};
    Require(menu.Option(0).top == 64 && menu.Option(2).bottom == 160,
        "dropdown rows preserve accepted DIP geometry");
    Require(menu.Panel(3).bottom == 164, "dropdown panel encloses rows with padding");
    Require(HitTestDropdownRect(menu.Option(0), 20, 70)
        && !HitTestDropdownRect(menu.Option(0), 20, 70, false)
        && !HitTestDropdownRect(menu.Option(0), 124, 70), "dropdown hit boundaries and disabled state");
    Require(SampleDropdownTransition(0).opacity == 0
        && SampleDropdownTransition(1).opacity == 1
        && SampleDropdownTransition(1).scale == 1, "dropdown animation endpoints");
    Require(SampleDropdownTransition(0.5F).scale > 1, "dropdown retains subtle spring overshoot");
    Require(AdvanceDropdownTransition(0, false, 0.24F) == 1
        && AdvanceDropdownTransition(1, true, 0.14F) == 0
        && AdvanceDropdownTransition(0.5F, true, 0.035F) == 0.25F,
        "dropdown open close timing and partial reversal");
    Require(argc == 2, "locale directory required");
    std::wstring error;
    Require(UiLocalization().DiscoverLocales(argv[1], error), "UI source loads");
    {
        const UiTheme theme;
        TasksPage tasks;
        noven::data::ItemCatalog taskCatalog;
        Require(taskCatalog.Load(std::filesystem::path(argv[1]).parent_path()/"data"/"items_catalog.tsv",error),"Tasks item catalog loads");
        tasks.Initialize(std::filesystem::path(argv[1]).parent_path()/"data",taskCatalog);
        tasks.Prepare(1400, 700, theme);
        Require(!tasks.Narrow() && !tasks.SelectedTrader().empty() && !tasks.SelectedTask().empty(),
            "Tasks prototype initializes a wide two-column selection");
        tasks.MouseDown(theme.sidebarWidth + theme.contentPadding + 20, 100);
        for (const wchar_t c : std::wstring(L"Debut")) Require(tasks.Char(c),
            "Tasks search accepts committed text");
        Require(tasks.QueryText() == L"Debut" && tasks.SelectedTrader() == "54cb50c76803fa8b248b4571",
            "Tasks search filters generated trader data");
        UiLocalization().SetLocale("en-US");
        tasks.Prepare(1000, 600, theme);
        Require(tasks.Narrow() && tasks.QueryText() == L"Debut"
            && tasks.SelectedTrader() == "54cb50c76803fa8b248b4571",
            "Tasks narrow layout and locale refresh preserve query and identity");
        tasks.Wheel(-WHEEL_DELTA, 700, 500);
        for (int i = 0; i < 30; ++i) tasks.Tick(0.016F);
        Require(tasks.Scroll() > 0, "Tasks detail owns independent vertical scrolling");
        Require(tasks.Key(VK_ESCAPE,false),"Tasks search clears before chain navigation test");
        for(const wchar_t c:std::wstring(L"Postman Pat - Part 1"))
            Require(tasks.Char(c),"Tasks chain source search accepts text");
        Require(tasks.SelectedTask()=="59675ea386f77414b32bded2","Tasks chain source selected");
        Require(tasks.Key(VK_ESCAPE,false),"Tasks chain source remains selected after clearing query");
        tasks.Prepare(1400,700,theme);
        const float taskLeft=theme.sidebarWidth+theme.contentPadding;
        const float taskRight=1400.0F-theme.contentPadding;
        const float taskRail=(taskRight-taskLeft-16.0F)*0.21F;
        const float chainX=taskLeft+taskRail+16.0F+48.0F;
        tasks.MouseDown(chainX,660.0F);
        (void)tasks.MouseUp(chainX,660.0F);
        Require(tasks.SelectedTask()=="596760e186f7741e11214d58",
            "clicking a follow-up row navigates by stable task ID");
        Require(tasks.GoBackTask()&&tasks.SelectedTask()=="59675ea386f77414b32bded2",
            "task-local back history restores the previous task");
        noven::data::MapCatalog taskMap;Require(taskMap.Load(std::filesystem::path(argv[1]).parent_path()/"data",error),"task link source loads");
        tasks.SetMapLinks(taskMap);tasks.MouseDown(taskLeft+20,100);
        for(const wchar_t c:std::wstring(L"Pathfinder"))Require(tasks.Char(c),"map-linked task search accepts text");
        tasks.Prepare(1400,1000,theme);
        for(int i=0;i<40;++i)tasks.Tick(.016F);
        const std::string pathfinder="5ae449c386f7744bde357697";
        Require(tasks.SelectedTask()==pathfinder,"Pathfinder selected by ordinary task search");
        std::string previousMapPoint;
        for(const auto id:{"5bb60cbc88a45011a8235cc5","6a60968c58aab7961885e537","6a6096d81284478fd859003a"}){
            const auto row=tasks.ObjectiveBounds(pathfinder+"_"+id);Require(row.has_value(),"linked objective exposes shared row geometry");
            tasks.MouseDown(row->left+40,row->top+12);const auto action=tasks.MouseUp(row->left+40,row->top+12);
            Require(action&&action->destination==TasksPage::Action::Destination::Map&&action->id!=previousMapPoint
                &&taskMap.Point(action->id)->sourceId.starts_with(pathfinder+"_"+id),"each Pathfinder objective click emits its own exact map point identity");
            previousMapPoint=action->id;
        }
        UiLocalization().SetLocale("zh-CN");
    }
    const HWND window = CreateWindowExW(0, L"STATIC", L"Noven localization test", WS_OVERLAPPEDWINDOW,
        0, 0, 1100, 800, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    Require(window != nullptr, "hidden test window created");
    noven::data::ItemCatalog catalog;
    noven::data::ItemEconomyStore economy;
    Require(catalog.Load(std::filesystem::path(argv[1]).parent_path() / "data" / "items_catalog.tsv", error),
        "detail interaction catalog loads");
    {
        MainWindowUi ui;
        Require(ui.Initialize(window, error), "native text formats initialize");
        ui.SetScannerState({noven::data::GameMode::Pve, true, 5441});
        const float scale = GetDpiForWindow(window) / 96.0F;
        RECT client{}; GetClientRect(window, &client);
        const float height = client.bottom / scale;
        const auto click = [&](float x, float y) {
            ui.MouseDown(static_cast<int>(x * scale), static_cast<int>(y * scale));
            return ui.MouseUp(static_cast<int>(x * scale), static_cast<int>(y * scale));
        };
        const UiTheme theme;
        ui.SetPriceDataSources(catalog, economy);
        ui.SetHideoutDataSources(std::filesystem::path(argv[1]).parent_path()/"data",catalog,economy);
        ui.SetTaskDataSources(std::filesystem::path(argv[1]).parent_path()/"data",catalog);
        Sidebar sidebar{ui.Registry()};
        sidebar.StartSelection(BuiltinPageId::Scanner,BuiltinPageId::Prices,height,theme);
        Require(sidebar.Animating(),"sidebar selection starts a bounded transition");
        sidebar.Tick(0.09F);
        const auto springTop=sidebar.SelectionRect(BuiltinPageId::Prices,height,theme).top;
        Require(springTop>sidebar.ItemRect(BuiltinPageId::Prices,height,theme).top,"sidebar spring briefly overshoots its target");
        sidebar.StartSelection(BuiltinPageId::Prices,BuiltinPageId::Map,height,theme);
        Require(sidebar.SelectionRect(BuiltinPageId::Map,height,theme).top==springTop,"rapid sidebar retarget preserves current position");
        for(int i=0;i<30;++i)sidebar.Tick(0.016F);
        Require(!sidebar.Animating(),"rapid sidebar retarget settles without idle frame work");
        Require(sidebar.SelectionRect(BuiltinPageId::Map,height,theme).top==sidebar.ItemRect(BuiltinPageId::Map,height,theme).top,"spring settles exactly at selected row");
        const auto selectPage = [&](PageId page) {
            const auto rect = sidebar.ItemRect(page, height, theme);
            Require(!click(rect.left + 30, (rect.top + rect.bottom) / 2), "page selection emits no GameMode change");
            Require(ui.ActivePage() == page, "page selection preserved");
            ui.Paint();
            for(int i=0;i<60 && ui.AnimationActive();++i) { Sleep(16);if(!ui.AnimationTick()) break; }
            ui.Paint();
        };
        selectPage(BuiltinPageId::Settings);
        const auto& locales = UiLocalization().AvailableLocales();
        std::size_t english = 0;
        while (english < locales.size() && locales[english].locale != "en-US") ++english;
        Require(english < locales.size(), "English row discovered");
        Require(!click(theme.sidebarWidth + theme.contentPadding + 30, 175 + english * 42.0F),
            "language click emits no GameMode change");
        Require(UiLocalization().ActiveLocale() == "en-US", "Settings click switches locale immediately");
        for (const auto& page : ui.Registry().Pages()) selectPage(page.id);
        Require(!click(theme.sidebarWidth + theme.contentPadding + 30, 175), "Chinese selection leaves game mode alone");
        Require(UiLocalization().ActiveLocale() == "zh-CN", "Settings switches back to Chinese");
        selectPage(BuiltinPageId::Scanner);
        const PageHost host{ui.Registry()};
        selectPage(BuiltinPageId::Hideout);
        ui.Paint();
        Require(!click(theme.sidebarWidth+theme.contentPadding+150,150),
            "Hideout PvE selection never returns a Scanner mode change");
        for(int i=0;i<40 && ui.AnimationActive();++i) { Sleep(16); if(!ui.AnimationTick()) break; }
        ui.Paint();
        Require(!click(theme.sidebarWidth+theme.contentPadding+40,100),"Hideout search focus");
        for(wchar_t c:std::wstring(L"Workbench")) Require(ui.Char(c),"Hideout committed search input");
        for(int i=0;i<40 && ui.AnimationActive();++i) { Sleep(16);if(!ui.AnimationTick()) break; }
        ui.Paint();
        Require(!click(theme.sidebarWidth+theme.contentPadding+225,385),"Hideout viewed level emits no scanner mode");
        ui.Paint();
        (void)ui.MouseWheel(static_cast<int>((theme.sidebarWidth+theme.contentPadding+200)*scale),
            static_cast<int>(650*scale),-2400);
        for(int i=0;i<60 && ui.AnimationActive();++i) { Sleep(16); if(!ui.AnimationTick()) break; }
        ui.Paint();
        selectPage(BuiltinPageId::Prices);
        const float width = client.right / scale;
        const float pagerRight = width - theme.contentPadding;
        const float pagerY = host.PriceListTop(width, theme) - 26;
        Require(ui.PricePage() == 0 && ui.PriceTotal() == catalog.ItemCount(),
            "Prices exposes all catalog matches, not only the first 120");
        Require(!click(pagerRight - 20, pagerY) && ui.PricePage() == 1,
            "header next button selects the next 30 items");
        Require(ui.AnimationActive(), "page selection animates through the existing list transition");
        selectPage(BuiltinPageId::Scanner); selectPage(BuiltinPageId::Prices);
        Require(ui.PricePage() == 1, "navigation preserves the current Prices page");
        (void)ui.MouseWheel(static_cast<int>((pagerRight - 20) * scale),
            static_cast<int>(500 * scale), -24000);
        for(int i=0;i<100 && ui.AnimationActive();++i) { Sleep(16);if(!ui.AnimationTick()) break; }
        ui.Paint();
        Require(!click(theme.sidebarWidth + theme.contentPadding + 20, pagerY)
            && ui.PricePage() == 0, "sticky header previous button works while scrolled to the bottom");
        const auto& capturedBar = ui.PriceListTransition().outgoingScrollbar;
        Require(capturedBar.bar && capturedBar.bar->thumb.top > capturedBar.bar->track.top,
            "native paging captures the scrolled outgoing thumb before resetting list scroll");
        const float pagerCenter = (theme.sidebarWidth + theme.contentPadding + pagerRight) / 2;
        Require(!click(pagerCenter, pagerY) && ui.Char(L'5') && ui.Char(L'x'),
            "page field handles digits and consumes invalid text separately from search");
        Require(ui.KeyDown(VK_RETURN, false) && ui.PricePage() == 4
            && ui.PriceSearch().Text().empty() && ui.AnimationActive(),
            "numeric page jump selects its target with animation without changing search");
        Require(!click(pagerCenter, pagerY) && ui.Char(L'9') && ui.KeyDown(VK_ESCAPE, false)
            && ui.PricePage() == 4, "Escape cancels page entry without navigating");
        Require(!click(pagerCenter, pagerY) && ui.Char(L'0') && ui.KeyDown(VK_RETURN, false)
            && ui.PricePage() == 0, "zero page entry clamps to the first page");
        Require(!click(pagerCenter, pagerY) && ui.Char(L'9') && ui.Char(L'9') && ui.Char(L'9')
            && ui.Char(L'9') && ui.KeyDown(VK_RETURN, false)
            && ui.PricePage() == PricePageCount(ui.PriceTotal()) - 1,
            "out of range numeric entry clamps to the last page");
        Require(!click(pagerCenter, pagerY) && ui.Char(L'2') && ui.KeyDown(VK_RETURN, false)
            && ui.PricePage() == 1, "rapid page jumps retarget the current list transition");
        Require(!click(theme.sidebarWidth+theme.contentPadding+40,100),"Prices search focus");
        for(const wchar_t c:std::wstring(L"电路板")) Require(ui.Char(c),"Prices committed input");
        Require(ui.PricePage() == 0, "changing the search resets pagination");
        Require(ui.KeyDown(VK_LEFT,false) && ui.PriceSearch().Caret()==2,"Prices forwards left key to shared editor");
        ui.Paint();
        Require(ui.PriceSearch().Caret()==2,"Prices paint preserves interior caret");
        Require(ui.Char(L'新') && ui.PriceSearch().Text()==L"电路新板","Prices inserts at interior caret");
        Require(ui.KeyDown(VK_BACK,false) && ui.PriceSearch().Text()==L"电路板","Prices Backspace follows caret");
        Require(ui.KeyDown(VK_RIGHT,false) && ui.PriceSearch().Caret()==3,"Prices forwards right key");
        ui.Paint();
        Require(ui.KeyDown(VK_ESCAPE,false),"clear Prices search after editing regression");
        for(int i=0;i<60 && ui.AnimationActive();++i) { Sleep(16);if(!ui.AnimationTick()) break; }
        ui.Paint();
        const float cardX = theme.sidebarWidth + theme.contentPadding + 30;
        const float cardY = host.PriceListTop(width, theme) + 25;
        Require(!click(cardX, cardY) && ui.AnimationActive(),
            "card expands without changing scanner GameMode");
        for (int tick = 0; tick < 90 && ui.AnimationActive(); ++tick) { Sleep(16); if(!ui.AnimationTick()) break; }
        Require(!ui.AnimationActive(), "detail expansion settles");
        ui.Paint();
        Require(!click(cardX, cardY) && ui.AnimationActive(), "same card collapses");
        for (int tick = 0; tick < 90 && ui.AnimationActive(); ++tick) { Sleep(16); if(!ui.AnimationTick()) break; }
        Require(!ui.AnimationActive(), "detail collapse settles");
        ui.Paint();
        Require(!click(cardX, cardY + host.PriceRowHeight(width, theme)),
            "second card opens without changing scanner mode");
        for (int tick = 0; tick < 120 && ui.AnimationActive(); ++tick) { Sleep(16); if(!ui.AnimationTick()) break; }
        ui.Paint();
        Require(!click(cardX, cardY), "expanded second card is clickable in the first visual slot");
        for (int tick = 0; tick < 120 && ui.AnimationActive(); ++tick) { Sleep(16); if(!ui.AnimationTick()) break; }
        Require(!ui.AnimationActive(), "anchored card collapse settles");
        selectPage(BuiltinPageId::Hideout);
        (void)ui.MouseWheel(static_cast<int>((theme.sidebarWidth+theme.contentPadding+200)*scale),
            static_cast<int>(650*scale),24000);
        for(int i=0;i<100 && ui.AnimationActive();++i) { Sleep(16);if(!ui.AnimationTick()) break; }
        ui.Paint();
        Require(!click(theme.sidebarWidth+theme.contentPadding+40,520),"item navigation emits no scanner mode change");
        Require(ui.ActivePage()==BuiltinPageId::Prices && ui.AnimationActive(),"Hideout material opens Prices detail");
        ui.Paint();
        Require(ui.CanGoBack(),"cross-page item navigation exposes back button");
        Require(!click(theme.sidebarWidth+24,42) && ui.ActivePage()==BuiltinPageId::Hideout
            && !ui.CanGoBack(),"back arrow restores source page and consumes return entry");
        for(int i=0;i<60 && ui.AnimationActive();++i) { Sleep(16);if(!ui.AnimationTick()) break; }
        ui.Paint();
        Require(!click(theme.sidebarWidth+theme.contentPadding+40,520)
            && ui.ActivePage()==BuiltinPageId::Prices,"return preserves selected Hideout material and scroll");
        Require(ui.GoBack() && ui.ActivePage()==BuiltinPageId::Hideout,"mouse back command returns to Hideout");
        for(int i=0;i<60 && ui.AnimationActive();++i) { Sleep(16);if(!ui.AnimationTick()) break; }
        Require(!ui.GoBack(),"no return action without cross-page history");
        Require(!click(theme.sidebarWidth+theme.contentPadding+40,520),"repeat item navigation");
        selectPage(BuiltinPageId::Scanner);
        Require(!ui.CanGoBack(),"explicit sidebar navigation clears contextual return");
        for (const float responsiveWidth : {880.0F, 1000.0F, 1280.0F, 1920.0F}) {
            const float right = responsiveWidth - theme.contentPadding;
            const bool narrow = responsiveWidth - theme.sidebarWidth - 2 * theme.contentPadding < 690;
            const float toolbarY = narrow ? 198.0F : 148.0F;
            Require(host.PriceControlAt(right - 20, toolbarY, theme, responsiveWidth, std::nullopt)
                == PriceToolbarControl::SideDropdown, "responsive toolbar stays inside list boundary");
            const auto bar = host.PriceScrollGeometry(responsiveWidth, 700, theme, 100, 0);
            Require(bar && bar->track.top > toolbarY + 16,
                "list viewport clears the wrapped toolbar");
            Require(bar->maximum == host.PriceMaxScroll(700, 100, responsiveWidth, theme),
                "responsive scroll clamp and thumb share content geometry");
            const auto expanded = host.PriceScrollGeometry(responsiveWidth, 700, theme, 100, 0, kPriceDetailsHeight);
            Require(expanded && expanded->maximum == bar->maximum + kPriceDetailsHeight
                && expanded->maximum == host.PriceMaxScroll(700, 100, responsiveWidth, theme, kPriceDetailsHeight),
                "expanded card height participates in scroll range and thumb");
        }
        const auto rect = host.ModeSelectorRect(theme);
        click(rect.left + 30, rect.top + 10);
        Require(!click(rect.left + 30, rect.bottom + 4 + 32 + 10), "PvE selection survived locale and page switches");
        ui.Paint();
        selectPage(BuiltinPageId::Map);ui.Paint();
        const auto floor=ui.Map().Layout().stack.Plate(0);
        Require(!click(floor.anchor.x,floor.anchor.y-2),"map floor click emits no scanner mode change");
        for(int tick=0;tick<60&&ui.AnimationActive();++tick){Sleep(16);if(!ui.AnimationTick())break;}
        ui.Paint();Require(ui.Map().Selected(),"Map placeholder replaced with interactive floor selection");
        const auto selectedFloor=ui.Map().FloorId();const auto viewBounds=ui.Map().Viewport().Bounds();
        Require(ui.MouseWheel(static_cast<int>((viewBounds.left+50)*scale),
            static_cast<int>((viewBounds.top+80)*scale),120),"map wheel handled inside viewport");
        const float mapScale=ui.Map().Viewport().Scale();
        Require(ui.MouseWheel(static_cast<int>((viewBounds.left+50)*scale),static_cast<int>((viewBounds.top+80)*scale),-120,true)
            &&ui.Map().FloorId()!=selectedFloor&&ui.Map().Viewport().Scale()==mapScale,"native Ctrl-wheel routes modifier to floor selection without zoom");
        Require(ui.MouseWheel(static_cast<int>((viewBounds.left+50)*scale),static_cast<int>((viewBounds.top+80)*scale),120,true)
            &&ui.Map().FloorId()==selectedFloor,"native Ctrl-wheel reverses floor selection during transition");
        Require(!click(ui.Map().Layout().search.left+20,100),"map search gains focus without mode change");
        Require(ui.Char(L'演')&&ui.Char(L'示'),"Map uses shared committed Unicode input");
        Require(!click(viewBounds.left+30,viewBounds.top+100),"map click blurs search");
        const auto mapQuery=ui.Map().Search().Text();
        const auto layerButton=ui.Map().Layout().filters[1];
        Require(!click(layerButton.left+20,layerButton.top+12),"map layer filter opens without changing game mode");
        for(int tick=0;tick<60&&ui.AnimationActive();++tick){Sleep(16);if(!ui.AnimationTick())break;}
        ui.Paint();const auto layerRow=ui.Map().FilterList().Row(0);
        Require(!click(layerRow.left+12,layerRow.top+12)&&!ui.Map().Filters().grid,"native filter hit toggles grid");
        const auto mapId=ui.Map().MapId();
        selectPage(BuiltinPageId::Scanner);selectPage(BuiltinPageId::Map);ui.Paint();
        Require(ui.Map().FloorId()==selectedFloor&&ui.Map().Viewport().Scale()==mapScale&&ui.Map().Search().Text()==mapQuery,
            "main navigation preserves selected Map floor, query and zoom");
        Require(ui.Map().MapId()==mapId&&!ui.Map().Filters().grid&&ui.Map().Panel()==MapFilterPanel::Layers,
            "main navigation preserves map identity, filter state and flyout");
        for(const auto& info:ui.Registry().Pages()) {
            selectPage(info.id);
            Require(!ui.AnimationActive(),"main-page animation settles for every sidebar page");
        }
        for(const auto page:{BuiltinPageId::Prices,BuiltinPageId::Tasks,BuiltinPageId::Hideout}) {
            const auto item=sidebar.ItemRect(page,height,theme);
            Require(!click(item.left+30,(item.top+item.bottom)/2),"rapid main-page selection emits no scanner mode change");
            Require(ui.ActivePage()==page && ui.AnimationActive(),"rapid selection retargets main-page animation");
            ui.Paint();
        }
        for(int i=0;i<60 && ui.AnimationActive();++i) { Sleep(16);if(!ui.AnimationTick()) break; }
        Require(!ui.AnimationActive() && ui.ActivePage()==BuiltinPageId::Hideout,"rapid page transitions settle at final destination");
        Require(ui.SetMapDataSources(std::filesystem::path(argv[1]).parent_path(),error),"native Map binds Interchange local data and images");
        selectPage(BuiltinPageId::Map);ui.Paint();
        Require(ui.Map().RealData()&&ui.Map().Points().size()==1634&&ui.Map().Search().Text().empty(),"native production Map replaces demo data and clears old catalog search");
        const auto realFloor=ui.Map().Layout().stack.Plate(1).anchor;
        Require(!click(realFloor.x,realFloor.y-2),"real floor selection preserves game mode");
        for(int tick=0;tick<60&&ui.AnimationActive();++tick){Sleep(16);if(!ui.AnimationTick())break;}
        ui.Paint();
        Require(ui.Map().FloorId()=="First_Floor"&&ui.Map().Selected(),"native production floor transition draws real image");
        const auto realScale=ui.Map().Viewport().Scale();
        selectPage(BuiltinPageId::Prices);selectPage(BuiltinPageId::Map);ui.Paint();
        Require(ui.Map().FloorId()=="First_Floor"&&ui.Map().Viewport().Scale()==realScale,"real map state persists across navigation");
        selectPage(BuiltinPageId::Tasks);Require(!click(theme.sidebarWidth+theme.contentPadding+20,100),"focus Tasks for map jump");
        Require(ui.KeyDown(VK_ESCAPE,false),"clear prior Tasks query before map link search");for(const wchar_t c:std::wstring(L"探路者"))Require(ui.Char(c),"Pathfinder query accepts Chinese");
        ui.Paint();
        const std::string taskId="5ae449c386f7744bde357697";
        const std::string objectiveId=taskId+"_5bb60cbc88a45011a8235cc5";
        auto objectiveRow=ui.Tasks().ObjectiveBounds(objectiveId);Require(objectiveRow.has_value(),"task map row exists in shell layout");
        if(objectiveRow->top+12>height-30){
            const int wheel=-WHEEL_DELTA*static_cast<int>(std::ceil((objectiveRow->top+12-(height-60))/66));
            Require(ui.MouseWheel(static_cast<int>((objectiveRow->left+40)*scale),static_cast<int>((height-50)*scale),wheel),"task detail scroll reveals linked row");
            for(int i=0;i<60&&ui.AnimationActive();++i){Sleep(16);if(!ui.AnimationTick())break;}ui.Paint();objectiveRow=ui.Tasks().ObjectiveBounds(objectiveId);
        }
        Require(objectiveRow->top>=274&&objectiveRow->top+12<height-24,"task map link is visible after scroll");
        const auto sourceScroll=ui.Tasks().Scroll();const auto sourceQuery=ui.Tasks().QueryText();
        Require(!click(objectiveRow->left+40,objectiveRow->top+12)&&ui.ActivePage()==BuiltinPageId::Map&&ui.CanGoBack(),"native task objective click navigates to map with contextual back");
        Require(ui.Map().SelectedPoint()&&ui.Map().SelectedPoint()->category==MapPointCategory::Task,"task navigation reveals selected map target");
        Require(ui.GoBack()&&ui.ActivePage()==BuiltinPageId::Tasks&&ui.Tasks().SelectedTask()==taskId
            &&ui.Tasks().QueryText()==sourceQuery&&ui.Tasks().Scroll()==sourceScroll,"contextual back restores task, query and exact detail scroll");
        noven::raid::RaidSession recorded;
        recorded.localSessionId="synthetic-ui-raid";recorded.eftRaidId="synthetic-eft";
        recorded.mapId="reserve";recorded.startObserved=recorded.endObserved=true;
        recorded.gameMode=noven::raid::GameMode::PvE;
        recorded.startedAt=1767225600000;recorded.endedAt=*recorded.startedAt+60000;recorded.duration=60000;
        noven::data::RecentScanEntry linkedScan;
        linkedScan.scanId=1;linkedScan.localSessionId=recorded.localSessionId;
        linkedScan.stableItemId="66b5f22b78bbc0200425f904";linkedScan.canonicalName="Synthetic snapshot";
        linkedScan.gameMode=noven::data::GameMode::Pve;linkedScan.fleaPrice=70000;
        ui.SetRaidSessions({recorded},std::nullopt,false);ui.SetRecentScans({linkedScan});
        selectPage(BuiltinPageId::RaidHistory);
        const float raidLeft=theme.sidebarWidth+theme.contentPadding;
        const float raidWidth=client.right/scale-theme.contentPadding-raidLeft;
        const float raidTop=raidWidth<720?278.0F:238.0F;
        Require(!click(raidLeft+30,raidTop+20)&&ui.RaidHistory().SelectedId()==recorded.localSessionId,"native raid list selects completed identity");
        for(int frame=0;frame<60&&ui.AnimationActive();++frame) {Sleep(16);(void)ui.AnimationTick();}
        ui.Paint();
        const float detailX=raidWidth<720?raidLeft+30:raidLeft+raidWidth*.4F+40;
        (void)ui.MouseWheel(static_cast<int>(detailX*scale),static_cast<int>((raidTop+80)*scale),-480);
        Require(ui.AnimationActive(),"raid wheel input schedules native smooth scrolling");
        for(int frame=0;frame<60&&ui.AnimationActive();++frame) {Sleep(16);(void)ui.AnimationTick();}
        Require(!ui.RaidHistory().Animating(),"raid wheel motion settles before contextual navigation");
        ui.Paint();
        const auto selected=ui.RaidHistory().SelectedId();const auto listScroll=ui.RaidHistory().ListScroll();const auto detailScroll=ui.RaidHistory().DetailScroll();
        const auto scanBounds=ui.RaidHistory().ScanBounds(linkedScan.scanId);Require(scanBounds.has_value(),"linked scan has shared visible hit geometry");
        Require(!click(scanBounds->left+30,scanBounds->top+8)&&ui.ActivePage()==BuiltinPageId::Prices&&ui.CanGoBack(),"linked snapshot opens Prices by stable identity and source mode");
        Require(ui.GoBack()&&ui.ActivePage()==BuiltinPageId::RaidHistory&&ui.RaidHistory().SelectedId()==selected
            &&ui.RaidHistory().ListScroll()==listScroll&&ui.RaidHistory().DetailScroll()==detailScroll,"contextual and side-back command preserve resident raid selection and both scroll positions");
        noven::events::EventRecord eventFixture;
        eventFixture.eventId="synthetic-ui-event-0";eventFixture.title="Fixture official event";
        for(int i=0;i<80;++i)eventFixture.summary+="Stored official announcement text. ";
        eventFixture.sourceStatus=noven::events::EventStatus::Active;
        const auto eventMapId=ui.Map().Catalog(noven::data::GameMode::Pvp).Maps().front().id;
        eventFixture.mapIds={eventMapId,"unresolved-map"};eventFixture.taskIds={taskId};eventFixture.itemIds={linkedScan.stableItemId};
        std::vector<noven::events::EventRecord> eventFixtures;
        for(int i=0;i<8;++i){auto copy=eventFixture;copy.eventId="synthetic-ui-event-"+std::to_string(i);eventFixtures.push_back(std::move(copy));}
        ui.SetEvents(eventFixtures,{noven::events::RefreshPhase::Ready,{},{}},100);
        selectPage(BuiltinPageId::Events);
        Require(!click(raidLeft+20,100),"Events search focus");for(wchar_t c:std::wstring(L"fixture"))Require(ui.Char(c),"Events shared Unicode input");
        ui.Paint();Require(!click(raidLeft+raidWidth*.3F,150),"Events status tab does not emit game mode");
        for(int i=0;i<60&&ui.AnimationActive();++i){Sleep(16);(void)ui.AnimationTick();}
        (void)ui.MouseWheel(static_cast<int>((raidLeft+30)*scale),static_cast<int>(400*scale),-240);
        for(int i=0;i<60&&ui.AnimationActive();++i){Sleep(16);(void)ui.AnimationTick();}ui.Paint();
        Require(!click(raidLeft+30,260)&&!ui.Events().SelectedId().empty(),"Events list selects a stable event without reordering");
        for(int i=0;i<60&&ui.AnimationActive();++i){Sleep(16);(void)ui.AnimationTick();}ui.Paint();
        const float eventDetailX=ui.Events().Narrow()?raidLeft+30:width-theme.contentPadding-60;
        (void)ui.MouseWheel(static_cast<int>(eventDetailX*scale),static_cast<int>(400*scale),-240);
        for(int i=0;i<60&&ui.AnimationActive();++i){Sleep(16);(void)ui.AnimationTick();}ui.Paint();
        const auto eventSelected=ui.Events().SelectedId();const auto eventListScroll=ui.Events().ListScroll(),eventDetailScroll=ui.Events().DetailScroll();
        const auto eventRows=ui.Events().Browser().Rows();
        Require(!ui.OpenEventAssociation({EventAction::Kind::Map,"unresolved-map"})&&ui.ActivePage()==BuiltinPageId::Events,"unresolved map cannot produce a guessed jump");
        for(const auto action:{EventAction{EventAction::Kind::Map,eventMapId},EventAction{EventAction::Kind::Task,taskId},EventAction{EventAction::Kind::Item,linkedScan.stableItemId}}) {
            Require(ui.OpenEventAssociation(action)&&ui.CanGoBack(),"Events association opens through shared contextual navigation");
            if(action.kind==EventAction::Kind::Item)Require(ui.PriceSearch().Text()!=std::wstring(action.id.begin(),action.id.end())&&ui.PriceTotal()==1,"exact item jump displays its name, not its storage identity");
            if(action.kind==EventAction::Kind::Map)Require(ui.ActivePage()==BuiltinPageId::Map&&ui.Map().MapId()==eventMapId,"Events uses exact map identity");
            if(action.kind==EventAction::Kind::Task)Require(ui.ActivePage()==BuiltinPageId::Tasks&&ui.Tasks().SelectedTask()==taskId
                &&ui.Tasks().SelectedTrader()==ui.Tasks().Catalog().Task("regular",taskId)->traderId,"Events uses exact task and trader identity");
            if(action.kind==EventAction::Kind::Item)Require(ui.ActivePage()==BuiltinPageId::Prices&&ui.PriceExactId()==action.id,"Events uses exact item ID independently of display text");
            Require(ui.GoBack()&&ui.ActivePage()==BuiltinPageId::Events,"contextual and side-back return to Events");
            Require(ui.Events().SelectedId()==eventSelected&&ui.Events().Search().Text()==L"fixture"
                &&ui.Events().Browser().Filter()==noven::events::EventStatus::Active,"Events selection/query/filter remain resident");
            Require(ui.Events().ListScroll()==eventListScroll&&ui.Events().DetailScroll()==eventDetailScroll,"Events preserves exact visible list/detail offsets");
            Require(ui.Events().Browser().Rows()==eventRows,"Events selection and navigation do not reorder rows");
            for(int i=0;i<60&&ui.AnimationActive();++i){Sleep(16);(void)ui.AnimationTick();}ui.Paint();
        }
        ui.SetEvents(eventFixtures,{noven::events::RefreshPhase::Failed,"synthetic offline",{}},100);ui.Paint();
        Require(ui.Events().Browser().Rows()==eventRows&&ui.Events().RefreshText()==Tr(TextKey::EventCached),"failed refresh never clears valid visible event data");
        auto recentClick=linkedScan;recentClick.gameMode=noven::data::GameMode::Pvp;
        ui.SetRecentScans({recentClick});selectPage(BuiltinPageId::RecentScans);
        Require(!click(theme.sidebarWidth+theme.contentPadding+30,170)&&ui.ActivePage()==BuiltinPageId::Prices
            &&ui.PriceTotal()==1&&ui.CanGoBack(),"recent card opens exact item detail through contextual navigation");
        Require(ui.GoBack()&&ui.ActivePage()==BuiltinPageId::RecentScans,"recent-card return keeps resident page");
        selectPage(BuiltinPageId::Settings);
        unsigned settingsCalls{};ui.SetPreferencesHandler([&](const auto& next){++settingsCalls;return next.scanKey!=VK_F8;});
        Require(!click(theme.sidebarWidth+theme.contentPadding+30,455)&&ui.RecordingShortcut(),"native shortcut control starts capture");
        Require(ui.KeyDown(VK_F6,true)&&!ui.RecordingShortcut()&&ui.Preferences().scanKey==VK_F6
            &&ui.Preferences().scanModifiers==MOD_CONTROL&&settingsCalls==1,"shortcut capture passes modifiers through app boundary");
        click(theme.sidebarWidth+theme.contentPadding+30,455);(void)ui.KeyDown(VK_F8,false);
        Require(ui.RecordingShortcut()&&ui.Preferences().scanKey==VK_F6,"failed shortcut application retains previous setting");
        Require(ui.KeyDown(VK_ESCAPE,false)&&!ui.RecordingShortcut(),"escape cancels shortcut capture");
        unsigned manualScans{};ui.SetRaidScanHandler([&]{++manualScans;return true;});selectPage(BuiltinPageId::RaidHistory);
        click(width-theme.contentPadding-50,110);Require(manualScans==1,"manual raid scan stays behind service callback");
        Require(ui.RaidScanPending()&&ui.RaidScanLabel()==Tr("raid.scan_pending"),"manual request shows immediate pending feedback");
        click(width-theme.contentPadding-50,110);Require(manualScans==1,"pending scan blocks repeated requests");
        ui.SetRaidScanStatus(false,true,false);Require(ui.RaidScanLabel()==Tr("raid.scan_completed"),"unchanged history still shows scan completion");
        ui.SetRaidScanStatus(false,false,true);Require(ui.RaidScanLabel()==Tr("raid.scan_failed"),"scan failure is visible without clearing history");
        noven::plugins::PluginManifest localManifest;
        localManifest.manifestVersion=1;localManifest.apiVersion=1;localManifest.id="com.example.loot-route";
        localManifest.name="Loot Route";localManifest.version="1.0.0";localManifest.requestedPermissions={"ui.page.register"};
        noven::plugins::PluginSnapshot localPlugins;
        localPlugins.records.push_back({L"plugins/com.example.loot-route",noven::plugins::PluginState::Valid,localManifest,{}});
        const auto registeredCount=ui.Registry().Pages().size();ui.SetPlugins(localPlugins);
        unsigned pluginRefreshes{};
        ui.SetPluginRefreshHandler([&]{++pluginRefreshes;localPlugins.records.push_back({L"plugins/invalid",noven::plugins::PluginState::InvalidManifest,{},{{"plugins.diag.json",{}}}});ui.SetPlugins(localPlugins);});
        selectPage(BuiltinPageId::Plugins);
        Require(ui.Plugins().Rows().size()==1&&ui.Plugins().Rows()[0].title==L"Loot Route","native Plugins page consumes original metadata snapshot");
        const auto pluginRefresh=ui.Plugins().RefreshBounds();
        ui.MouseDown(static_cast<int>((pluginRefresh.left+20)*scale),static_cast<int>((pluginRefresh.top+15)*scale));
        ui.CancelScrollDrag();
        (void)ui.MouseUp(static_cast<int>((pluginRefresh.left+20)*scale),static_cast<int>((pluginRefresh.top+15)*scale));
        Require(pluginRefreshes==0,"capture loss cancels pending Plugins refresh press");
        Require(!click(pluginRefresh.left+20,pluginRefresh.top+15)&&pluginRefreshes==1,"native refresh requests discovery through App callback only");
        ui.Paint();Require(ui.Plugins().Rows().size()==2,"native refresh replaces displayed plugin snapshot");
        Require(ui.Registry().Pages().size()==registeredCount&&!ui.Registry().Contains(PageId{"plugin.com.example.loot-route.main"}),"requested registration permission cannot change main navigation");
        for(const auto page:{BuiltinPageId::Prices,BuiltinPageId::Plugins,BuiltinPageId::Scanner,BuiltinPageId::Plugins}) {
            const auto row=sidebar.ItemRect(page,height,theme);Require(!click(row.left+30,(row.top+row.bottom)*.5F)&&ui.ActivePage()==page,"rapid Plugins navigation keeps stable identity");ui.Paint();
        }
        for(int i=0;i<60&&ui.AnimationActive();++i){Sleep(16);(void)ui.AnimationTick();}ui.Paint();
        Require(!ui.AnimationActive()&&pluginRefreshes==1&&ui.Plugins().Rows().size()==2,"navigation neither polls discovery nor loses plugin snapshot");
        const PageId futurePage{"plugin.com.example.loot-route"};
        Require(ui.Registry().Register({futurePage,PageSection::Secondary,"nav.events","page.events.description",
            PageIcon::GenericPlugin,-1,PageSource::Plugin,UiExtensionPolicy::Extensible}),"future page registers metadata without an enum or executable code");
        const auto futureLayout=sidebar.Layout(height,theme);
        const auto* futureRow=futureLayout.Find(futurePage);
        Require(futureRow&&futureRow->visible,"registered future page appears in native sidebar geometry");
        const float futureX=(futureRow->rect.left+futureRow->rect.right)*.5F;
        const float futureY=(futureRow->rect.top+futureRow->rect.bottom)*.5F;
        Require(sidebar.HitTest(futureX,futureY,height,theme)==futurePage,"future row hit test returns exact stable identity");
        Require(!click(futureX,futureY)&&ui.ActivePage()==futurePage,"native navigation accepts future namespaced identity");
        for(int i=0;i<60&&ui.AnimationActive();++i){Sleep(16);(void)ui.AnimationTick();}ui.Paint();
        Require(!ui.AnimationActive(),"future page transition becomes idle");
        Require(ui.Registry().Unregister(futurePage)&&ui.ActivePage()==BuiltinPageId::Scanner,"removed active metadata safely falls back to scanner");
        Require(!sidebar.Layout(height,theme).Find(futurePage),"layout cache invalidates when registry changes");
    }
    DestroyWindow(window);
    std::cout << "Native localization interaction tests passed (hidden window, not visual acceptance)\n";
} catch (const std::exception& error) {
    std::cerr << "Localization UI test failed: " << error.what() << '\n';
    return 1;
}
