#include "ui/MainWindowUi.h"
#include "ui/Dropdown.h"
#include "ui/NavigationButton.h"
#include "ui/PricePagination.h"
#include "ui/ItemTypeLabel.h"
#include "ui/localization/LocalizationService.h"
#include "ui/EventFormat.h"
#include "data/LocalizedName.h"

#include <algorithm>
#include <cmath>
#include <shellapi.h>

namespace noven::ui {

namespace {
std::string WideToUtf8(std::wstring_view text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
        text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
    return result;
}
}

bool MainWindowUi::Initialize(HWND window, std::wstring& error) {
    window_ = window;
    dpi_ = GetDpiForWindow(window);
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                 d2d_factory_.GetAddressOf()))) {
        error = L"Could not initialize Direct2D for the main window";
        return false;
    }
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(write_factory_.GetAddressOf())))) {
        error = L"Could not initialize DirectWrite for the main window";
        return false;
    }
    if(!CreateTextFormats(error)||!CreateRenderTarget(error))return false;
    return true;
}

void MainWindowUi::EventClockTick(){
    if(navigation_.Active()!=BuiltinPageId::Events||!IsWindowVisible(window_)||IsIconic(window_))return;
    const auto now=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    if(events_.ClockTick(now))Invalidate();
}
bool MainWindowUi::OpenEventAssociation(const EventAction& action) {
    if(navigation_.Active()!=BuiltinPageId::Events)return false;
    const auto* event=events_.Browser().Find(events_.SelectedId());if(!event)return false;
    const auto associations=events_.Browser().Associations(*event);
    const auto kind=action.kind==EventAction::Kind::Map?events::EntityKind::Map:action.kind==EventAction::Kind::Task?events::EntityKind::Task:events::EntityKind::Item;
    if(action.kind!=EventAction::Kind::Map&&action.kind!=EventAction::Kind::Task&&action.kind!=EventAction::Kind::Item)return false;
    if(!std::ranges::any_of(associations,[&](const auto& a){return a.kind==kind&&a.id==action.id;}))return false;
    // 未指定范围时仅使用浏览目标默认模式，不把它写回官方活动事实。
    // Unspecified scope uses destination defaults only; never write that choice into official facts.
    const auto mode=event->modes==std::vector{events::EventMode::PvE}?data::GameMode::Pve:data::GameMode::Pvp;
    if(action.kind==EventAction::Kind::Item) {
        if(!event_items_||!event_items_->FindById(action.id))return false;
        OpenPriceItem(action.id,mode);return navigation_.Active()==BuiltinPageId::Prices;
    }
    if(action.kind==EventAction::Kind::Task) {
        if(!tasks_.OpenTask(action.id,mode))return false;
        return_pages_.push_back(BuiltinPageId::Events);SelectPage(BuiltinPageId::Tasks);return true;
    }
    if(!map_.Catalog(mode).Map(action.id))return false;
    map_.SetMode(mode);map_.SelectMap(action.id);map_.Overview();
    return_pages_.push_back(BuiltinPageId::Events);SelectPage(BuiltinPageId::Map);return true;
}
void MainWindowUi::MapClockTick(){
    // 时钟刷新与 60Hz 动画分离，只重绘当前地图页。
    // Clock refresh is separate from 60Hz animation and repaints only the active Map page.
    if(!IsWindowVisible(window_)||IsIconic(window_))return;
    if(navigation_.Active()==BuiltinPageId::Map&&map_.ClockTick(MapUtcMilliseconds()))Invalidate();
}

void MainWindowUi::SetPriceDataSources(const data::ItemCatalog& catalog,
                                       const data::ItemEconomyStore& economy) {
    price_browser_ = std::make_unique<data::PriceBrowserModel>(catalog, economy);
    raid_history_.SetItemCatalog(catalog);
    event_items_=&catalog;BindEventCatalogs();
    RefreshPriceRows();
}

void MainWindowUi::OpenPriceItem(const std::string& id,data::GameMode mode) {
    if(!price_browser_) return;
    // 跨页导航使用稳定 ID 和来源模式，不触碰 Scanner 或藏身处浏览状态。
    // Cross-page navigation uses stable ID/source mode without changing Scanner or Hideout state.
    const auto found=price_browser_->Query(id,mode);
    if(found.empty() || found.front().item->id!=id) return;
    price_mode_=mode;price_exact_id_=id;
    price_query_=EventWide(data::LocalizedName(found.front().item->nameZh,found.front().item->nameEn,UiLocalization().ActiveLocale()));
    price_search_.SetText(price_query_); price_search_.Blur();
    price_scroll_=price_scroll_target_=0;
    price_transition_={}; price_transition_.underlineIndex=static_cast<float>(mode);
    price_dropdown_.reset();
    RefreshPriceRows();
    price_details_.id=id; price_details_.open=true; price_details_.Retarget(true,0);
    return_pages_.push_back(navigation_.Active());
    SelectPage(BuiltinPageId::Prices);
    RequestPriceHistory();RequestVisiblePriceImages();Invalidate();
}
bool MainWindowUi::OnBackButton(int x,int y) const noexcept {
    const float dx=x/Scale(),dy=y/Scale();
    return HitNavigationButton(D2D1::RectF(theme_.sidebarWidth+8,26,theme_.sidebarWidth+40,58),dx,dy,CanGoBack());
}
bool MainWindowUi::GoBack() {
    if(navigation_.Active()==BuiltinPageId::Tasks&&tasks_.GoBackTask()){
        CancelScrollDrag();Invalidate();return true;
    }
    if(return_pages_.empty()) return false;
    // 返回复用仍驻留的页面状态，不重新初始化查询、等级或滚动位置。
    // Return to resident page state without reinitializing query, level or scroll position.
    const auto target=return_pages_.back();return_pages_.pop_back();SelectPage(target);
    back_hovered_=back_pressed_=false;price_search_.Blur();
    CancelScrollDrag();Invalidate();return true;
}
void MainWindowUi::SetPluginRuntime(const std::vector<plugins::HostSnapshot>& snapshots) {
    const auto active=navigation_.Active();const auto outgoing=page_transition_.Sample(active).page;
    std::vector<PageId> known;for(const auto& page:registry_.Pages())known.push_back(page.id);
    const auto revision=registry_.Revision();
    plugin_pages_.Sync(snapshots);plugins_.SetRuntime(snapshots);
    for(auto it=plugin_views_.begin();it!=plugin_views_.end();)if(!plugin_pages_.Find(it->first))it=plugin_views_.erase(it);else ++it;
    for(const auto& descriptor:registry_.Pages())if(const auto* page=plugin_pages_.Find(descriptor.id))plugin_views_[descriptor.id].SetPage(*page);
    if(registry_.Revision()!=revision){
        sidebar_.EndDrag();sidebar_.Tick(1);pressed_.reset();hovered_.reset();
        for(const auto& page:registry_.Pages())if(page.source==PageSource::Plugin&&std::find(known.begin(),known.end(),page.id)==known.end())sidebar_.EnsureVisible(page.id,DipHeight(),theme_);
    }
    // 停止/崩溃必须撤销旧页面及输入，不允许过渡帧或返回栈继续引用失效身份。
    // Stop/crash revokes pages/input; neither transition frames nor the return stack may retain dead identities.
    if(!registry_.Contains(active)){navigation_.Select(BuiltinPageId::Plugins);CancelScrollDrag();}
    if(!registry_.Contains(active)||!registry_.Contains(outgoing)){page_transition_={};sidebar_.Tick(1);pressed_.reset();hovered_.reset();back_pressed_=false;}
    std::erase_if(return_pages_,[&](const auto& id){return !registry_.Contains(id);});Invalidate();
}
bool MainWindowUi::SelectPage(PageId page) {
    if(page==navigation_.Active()||!registry_.Contains(page)) return false;
    sidebar_.EnsureVisible(page,DipHeight(),theme_);
    sidebar_.StartSelection(navigation_.Active(),page,DipHeight(),theme_);
    page_transition_.Start(navigation_.Active());
    if(!navigation_.Select(page)) return false;
    if (page != BuiltinPageId::Prices) price_page_input_.Blur();
    if(page!=BuiltinPageId::Settings)preferences_.Blur();
    if(page!=BuiltinPageId::Scanner){mode_menu_open_=false;mode_menu_progress_=0;}
    if(page!=BuiltinPageId::RaidHistory)raid_history_.Blur();
    if(page!=BuiltinPageId::Events)events_.Blur();
    if(page!=BuiltinPageId::Plugins)plugins_.Blur();
    for(auto& [id,view]:plugin_views_)if(id!=page)view.Cancel();
    if(page==BuiltinPageId::Events){EventClockTick();SetTimer(window_,EventClockTimerId,60000,nullptr);}else KillTimer(window_,EventClockTimerId);
    if(page==BuiltinPageId::Map)SetTimer(window_,MapClockTimerId,250,nullptr);else KillTimer(window_,MapClockTimerId);
    if(page==BuiltinPageId::Tasks)tasks_.Activate();
    Invalidate();return true;
}
void MainWindowUi::RefreshPriceRows(bool animateSearch, bool resetPage) {
    if (!price_browser_) return;
    ScrollbarPose outgoingScrollbar;
    if (animateSearch) {
        outgoingScrollbar = SamplePriceScrollbar(price_transition_, price_search_transition_,
            PageHost::PriceScrollGeometry(DipWidth(), DipHeight(), theme_,
                price_transition_.outgoingCount, price_transition_.outgoingScroll),
            PageHost::PriceScrollGeometry(DipWidth(), DipHeight(), theme_,
                price_rows_.size(), price_scroll_, PriceDetailsScrollExtra()));
    }
    if (resetPage) { price_page_ = 0; price_page_input_.Blur(); }
    price_search_duration_ = 0.65F;
    price_details_ = {};
    std::vector<PriceReflowRow> previous;
    const float rowHeight = PageHost::PriceRowHeight(DipWidth(), theme_);
    if (animateSearch) {
        // 只保留仍可见的旧卡片，避免快速输入不断积累已消失的结果。
        // Retain only visible old cards so rapid typing cannot accumulate vanished results.
        const auto keep = [&](const data::PriceRow& row, float slot, float opacity) {
            slot -= price_scroll_ / rowHeight;
            if (opacity > 0.001F && slot >= -1 && slot < DipHeight() / rowHeight)
                previous.push_back({row, slot, slot, opacity, false});
        };
        if (price_search_transition_.progress < 1) {
            for (const auto& row : price_search_transition_.rows) {
                const auto pose = SampleListReflow(row.fromSlot, row.toSlot, row.opacity,
                    row.retained, price_search_transition_.progress);
                keep(row.row, pose.slot, pose.opacity);
            }
        } else {
            for (std::size_t i = 0; i < price_rows_.size(); ++i)
                keep(price_rows_[i], static_cast<float>(i), 1);
        }
        price_scroll_ = price_scroll_target_ = 0;
    }
    std::vector<data::PriceTagAlias> tagAliases;
    if (price_query_.find(L'#') != std::wstring::npos) {
        // 使用语言服务副本读取所有标签译名，切换 UI 语言不会改变既有 # 查询的含义。
        // Read all tag translations from a service copy so UI locale changes preserve existing # query meaning.
        auto labels = UiLocalization();
        for (const auto& locale : labels.AvailableLocales()) {
            labels.SetLocale(locale.locale);
            for (const auto& mapping : ItemTypeMappings)
                tagAliases.push_back({WideToUtf8(labels.Get(mapping.key)), std::string(mapping.type)});
        }
    }
    const auto query=price_exact_id_.empty()?WideToUtf8(price_query_):price_exact_id_;
    price_rows_ = price_browser_->Query(query, price_mode_,
        price_sort_, price_sort_descending_, price_trader_side_, PricePageSize, tagAliases,
        price_page_ * PricePageSize, &price_total_);
    if (price_page_ >= PricePageCount(price_total_)) {
        price_page_ = PricePageCount(price_total_) - 1;
        price_rows_ = price_browser_->Query(query, price_mode_,
            price_sort_, price_sort_descending_, price_trader_side_, PricePageSize, tagAliases,
            price_page_ * PricePageSize);
    }
    price_search_transition_ = {};
    if (animateSearch) {
        price_search_transition_.outgoingScrollbar = outgoingScrollbar;
        for (std::size_t i = 0; i < price_rows_.size(); ++i) {
            const auto found = std::find_if(previous.begin(), previous.end(), [&](const auto& old) {
                return old.row.item->id == price_rows_[i].item->id;
            });
            if (found != previous.end()) {
                found->row = price_rows_[i];
                found->toSlot = static_cast<float>(i);
                found->retained = true;
            } else {
                previous.push_back({price_rows_[i], static_cast<float>(i) + 0.5F,
                    static_cast<float>(i), 0, true});
            }
        }
        price_search_transition_.rows = std::move(previous);
        price_search_transition_.progress = 0;
        price_search_started_ = std::chrono::steady_clock::now();
    }
    price_data_updated_ = price_browser_->LastUpdated(price_mode_);
    for (const std::string& id : price_image_ids_) {
        if (std::none_of(recent_.begin(), recent_.end(), [&](const auto& entry) {
                return entry.stableItemId == id;
            }) && !hideout_image_ids_.contains(id) && !task_image_ids_.contains(id)) image_cache_.Forget(id);
    }
    price_image_ids_.clear();
    price_image_window_start_ = static_cast<std::size_t>(-1);
    RequestVisiblePriceImages();
    Invalidate();
}

void MainWindowUi::RequestVisibleHideoutImages() {
    const bool visiblePage=navigation_.Active()==BuiltinPageId::Hideout
        || page_transition_.ShowingOutgoing(BuiltinPageId::Hideout);
    const auto visible=visiblePage ? hideout_.VisibleImages() : std::vector<std::string>{};
    const std::unordered_set<std::string> next(visible.begin(),visible.end());
    for (const auto& id : hideout_image_ids_) {
        if (!next.contains(id) && !price_image_ids_.contains(id) && !task_image_ids_.contains(id)
            && std::none_of(recent_.begin(),recent_.end(),[&](const auto& e){return e.stableItemId==id;})) {
            image_cache_.Forget(id); image_pixels_.erase(id); item_bitmaps_.erase(id);
        }
    }
    for (const auto& id : next) if (!hideout_image_ids_.contains(id)) image_cache_.Request(id);
    hideout_image_ids_=next;
}

void MainWindowUi::RequestVisibleTaskImages() {
    const bool visiblePage=navigation_.Active()==BuiltinPageId::Tasks
        || page_transition_.ShowingOutgoing(BuiltinPageId::Tasks);
    const auto visible=visiblePage?tasks_.VisibleImages():std::vector<std::string>{};
    const std::unordered_set<std::string> next(visible.begin(),visible.end());
    for(const auto& id:task_image_ids_)if(!next.contains(id)){
        const bool recent=std::any_of(recent_.begin(),recent_.end(),[&](const auto& entry){
            return entry.stableItemId==id;
        });
        if(!price_image_ids_.contains(id)&&!hideout_image_ids_.contains(id)&&!recent){
            image_cache_.Forget(id);image_pixels_.erase(id);item_bitmaps_.erase(id);
        }
    }
    for(const auto& id:next)if(!task_image_ids_.contains(id))image_cache_.Request(id,true);
    task_image_ids_=next;
}

void MainWindowUi::RequestVisiblePriceImages() {
    constexpr std::size_t kPriceImageRequestLimit = 32;
    std::size_t first = 0;
    float covered = 0;
    while (first < price_rows_.size()) {
        const float height = PageHost::PriceRowHeight(DipWidth(), theme_)
            + (price_rows_[first].item->id == price_details_.id ? price_details_.extent : 0);
        if (covered + height > price_scroll_) break;
        covered += height; ++first;
    }
    if (first == price_image_window_start_) return;
    for (const std::string& id : price_image_ids_) {
        if (std::none_of(recent_.begin(), recent_.end(), [&](const auto& entry) {
                return entry.stableItemId == id;
            }) && !hideout_image_ids_.contains(id) && !task_image_ids_.contains(id)) image_cache_.Forget(id);
    }
    price_image_ids_.clear();
    price_image_window_start_ = first;
    for (std::size_t i = first; i < (std::min)(first + kPriceImageRequestLimit, price_rows_.size()); ++i) {
        price_image_ids_.insert(price_rows_[i].item->id);
        image_cache_.Request(price_rows_[i].item->id);
    }
}

bool MainWindowUi::KeyDown(WPARAM key, bool control) {
    if(navigation_.Active()==BuiltinPageId::Plugins){const bool handled=plugins_.Key(key,control);if(handled)Invalidate();return handled;}
    if(preferences_.Recording()) {
        const UINT modifiers=(control?MOD_CONTROL:0)|((GetKeyState(VK_MENU)&0x8000)?MOD_ALT:0)
            |((GetKeyState(VK_SHIFT)&0x8000)?MOD_SHIFT:0)|((GetKeyState(VK_LWIN)&0x8000||GetKeyState(VK_RWIN)&0x8000)?MOD_WIN:0);
        preferences_.Key(static_cast<UINT>(key),modifiers);Invalidate();return true;
    }
    if(navigation_.Active()==BuiltinPageId::Events){const bool handled=events_.Key(key,control);if(handled)Invalidate();return handled;}
    if(navigation_.Active()==BuiltinPageId::RaidHistory) {const bool handled=raid_history_.Key(key,control);if(handled)Invalidate();return handled;}
    if (navigation_.Active()==BuiltinPageId::Map) {
        const bool handled=map_.Key(key,control); if(handled) Invalidate(); return handled;
    }
    if (navigation_.Active()==BuiltinPageId::Hideout) {
        const bool handled=hideout_.Key(key,control); if (handled) Invalidate(); return handled;
    }
    if (navigation_.Active()==BuiltinPageId::Tasks) {
        const bool handled=tasks_.Key(key,control); if (handled) Invalidate(); return handled;
    }
    if (navigation_.Active() != BuiltinPageId::Prices) return false;
    if (price_page_input_.Focused()) {
        if (key == VK_RETURN) {
            const auto page = PricePageFromInput(price_page_input_.Text(), price_total_);
            price_page_input_.Blur();
            if (page) SelectPricePage(*page);
            Invalidate(); return true;
        }
        if (key == VK_ESCAPE) { price_page_input_.Blur(); Invalidate(); return true; }
        const bool handled = price_page_input_.HandleKeyDown(key, control);
        if (handled) Invalidate();
        return handled;
    }
    if (!price_search_.HandleKeyDown(key, control)) return false;
    if (price_query_ != price_search_.Text()) {
        price_exact_id_.clear();
        price_query_ = price_search_.Text();
        RefreshPriceRows(true);
    }
    Invalidate();
    return true;
}

bool MainWindowUi::Char(wchar_t character) {
    if(navigation_.Active()==BuiltinPageId::Plugins){const bool handled=plugins_.Char(character);if(handled)Invalidate();return handled;}
    if(navigation_.Active()==BuiltinPageId::Events){const bool handled=events_.Char(character);if(handled)Invalidate();return handled;}
    if(navigation_.Active()==BuiltinPageId::RaidHistory) {const bool handled=raid_history_.Char(character);if(handled)Invalidate();return handled;}
    if (navigation_.Active()==BuiltinPageId::Map) {
        const bool handled=map_.Char(character); if(handled) Invalidate(); return handled;
    }
    if (navigation_.Active()==BuiltinPageId::Hideout) {
        const bool handled=hideout_.Char(character); if (handled) Invalidate(); return handled;
    }
    if (navigation_.Active()==BuiltinPageId::Tasks) {
        const bool handled=tasks_.Char(character); if (handled) Invalidate(); return handled;
    }
    if (navigation_.Active() != BuiltinPageId::Prices) return false;
    if (price_page_input_.Focused()) {
        if (character >= L'0' && character <= L'9') {
            (void)price_page_input_.HandleChar(character); Invalidate();
        }
        return true;
    }
    if (!price_search_.HandleChar(character)) return false;
    price_exact_id_.clear();
    price_query_ = price_search_.Text();
    RefreshPriceRows(true);
    Invalidate();
    return true;
}

bool MainWindowUi::CreateTextFormats(std::wstring& error) {
    const auto& id = UiLocalization().ActiveLocale();
    const std::wstring locale(id.begin(), id.end());
    const auto* family = id.starts_with("zh") ? L"Microsoft YaHei UI" : L"Segoe UI";
    Microsoft::WRL::ComPtr<IDWriteTextFormat> title, pageTitle, label, body, smallText;
    const auto format = [&](float size, DWRITE_FONT_WEIGHT weight,
                            Microsoft::WRL::ComPtr<IDWriteTextFormat>& destination) {
        return SUCCEEDED(write_factory_->CreateTextFormat(
            family, nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, size, locale.c_str(), destination.GetAddressOf()));
    };
    if (!format(25, DWRITE_FONT_WEIGHT_SEMI_BOLD, title)
        || !format(36, DWRITE_FONT_WEIGHT_SEMI_BOLD, pageTitle)
        || !format(16, DWRITE_FONT_WEIGHT_SEMI_BOLD, label)
        || !format(15, DWRITE_FONT_WEIGHT_NORMAL, body)
        || !format(12, DWRITE_FONT_WEIGHT_NORMAL, smallText)) {
        error = L"Could not create main-window text formats";
        return false;
    }
    title_format_ = std::move(title); page_title_format_ = std::move(pageTitle);
    label_format_ = std::move(label); body_format_ = std::move(body); small_format_ = std::move(smallText);
    for (IDWriteTextFormat* text : {title_format_.Get(), page_title_format_.Get(),
                                   label_format_.Get(), body_format_.Get(),
                                   small_format_.Get()}) {
        text->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        text->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
    return true;
}

// 语言列表只使用服务发现的数据；滚动与命中均使用客户区 DIP。
// Language rows use discovered metadata only; scrolling and hit testing use client-area DIPs.
void MainWindowUi::DrawLanguageSettings(const UiCanvas& canvas, float width, float height) {
    const float left = theme_.sidebarWidth + theme_.contentPadding;
    const float right = (std::min)(width - theme_.contentPadding, left + 620);
    const float listRight = left + (right - left) * 0.42F;
    canvas.Text(Tr(TextKey::Language), canvas.label, D2D1::RectF(left, 90, right, 121), theme_.primaryText);
    canvas.Text(Tr(TextKey::LanguageHint), canvas.smallFormat, D2D1::RectF(left, 122, right, 150), theme_.secondaryText);
    const auto& locales = UiLocalization().AvailableLocales();
    language_scroll_ = std::clamp(language_scroll_, 0.0F,
        (std::max)(0.0F, static_cast<float>(locales.size()) * 42 - (height - 178)));
    canvas.target.PushAxisAlignedClip(D2D1::RectF(left, 156, listRight, (std::max)(156.0F, height - 22)),
        D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    for (std::size_t i = 0; i < locales.size(); ++i) {
        const float top = 156 + static_cast<float>(i) * 42 - language_scroll_;
        if (top + 42 < 156 || top > height - 22) continue;
        const bool selected = locales[i].locale == UiLocalization().ActiveLocale();
        canvas.Round(D2D1::RectF(left, top, listRight, top + 38), theme_.cornerRadius,
            selected ? theme_.selected : hovered_language_ == i ? theme_.hover : theme_.surface);
        canvas.Text(locales[i].name, canvas.body, D2D1::RectF(left + 16, top, listRight - 16, top + 38),
            selected ? theme_.accent : theme_.primaryText);
    }
    canvas.target.PopAxisAlignedClip();

    // 独立布局启用换行，不改变共享文字格式或语言列表的滚动高度。
    // Wrap independent layouts without changing shared formats or the locale scroll height.
    const float attributionLeft = listRight + 24;
    const auto wrapped = [&](std::wstring_view text, IDWriteTextFormat& format,
                             float top, D2D1_COLOR_F color) {
        Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
        if (FAILED(write_factory_->CreateTextLayout(text.data(), static_cast<UINT32>(text.size()),
                &format, (std::max)(1.0F, right - attributionLeft),
                (std::max)(1.0F, height - 22 - top), &layout))) return 0.0F;
        layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        DWRITE_TEXT_METRICS metrics{};
        layout->GetMetrics(&metrics);
        canvas.brush.SetColor(color);
        canvas.target.DrawTextLayout(D2D1::Point2F(attributionLeft, top), layout.Get(),
            &canvas.brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
        return metrics.height;
    };
    float attributionTop = 156;
    attributionTop += wrapped(Tr("settings.attribution.title"), canvas.label,
        attributionTop, theme_.primaryText) + 12;
    attribution_button_ = {};
    const auto link = Tr("settings.attribution.link") + L" · NOTICE.md";
    Microsoft::WRL::ComPtr<IDWriteTextLayout> linkLayout;
    if (SUCCEEDED(write_factory_->CreateTextLayout(link.data(), static_cast<UINT32>(link.size()),
            &canvas.body, (std::max)(1.0F, right - attributionLeft - 24), 1000, &linkLayout))) {
        linkLayout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        linkLayout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        DWRITE_TEXT_METRICS metrics{};
        linkLayout->GetMetrics(&metrics);
        const float buttonHeight = (std::max)(44.0F, metrics.height + 24);
        if (attributionTop + buttonHeight <= height - 22) {
            attribution_button_ = D2D1::RectF(attributionLeft, attributionTop, right,
                attributionTop + buttonHeight);
            canvas.Round(attribution_button_, theme_.cornerRadius,
                attribution_pressed_ ? theme_.hover : theme_.selected);
            canvas.brush.SetColor(theme_.accent);
            canvas.target.DrawRoundedRectangle(D2D1::RoundedRect(attribution_button_,
                theme_.cornerRadius, theme_.cornerRadius), &canvas.brush, 1);
            canvas.target.DrawTextLayout(D2D1::Point2F(attributionLeft + 12, attributionTop + 12),
                linkLayout.Get(), &canvas.brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
    }
    preferences_.Draw(canvas,theme_,left,right);
}

std::optional<std::size_t> MainWindowUi::LanguageAt(int x, int y) const {
    if (navigation_.Active() != BuiltinPageId::Settings) return std::nullopt;
    RECT client{}; GetClientRect(window_, &client);
    const float left = theme_.sidebarWidth + theme_.contentPadding;
    const float right = (std::min)(client.right / Scale() - theme_.contentPadding, left + 620);
    const float listRight = left + (right - left) * 0.42F;
    const float dx = x / Scale(), dy = y / Scale();
    if (dx < left || dx >= listRight
        || dy < 156 || dy >= DipHeight() - 22) return std::nullopt;
    const float offset = dy - 156 + language_scroll_;
    const auto index = static_cast<std::size_t>(offset / 42);
    if (index >= UiLocalization().AvailableLocales().size() || std::fmod(offset, 42.0F) >= 38) return std::nullopt;
    return index;
}

bool MainWindowUi::CreateRenderTarget(std::wstring& error) {
    RECT client{};
    GetClientRect(window_, &client);
    const auto pixels = D2D1::SizeU(std::max(1L, client.right),
                                    std::max(1L, client.bottom));
    const HRESULT hr = d2d_factory_->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                                     D2D1::PixelFormat(), static_cast<float>(dpi_),
                                     static_cast<float>(dpi_)),
        D2D1::HwndRenderTargetProperties(window_, pixels),
        render_target_.GetAddressOf());
    if (FAILED(hr)) {
        error = L"Could not create main-window Direct2D render target";
        return false;
    }
    if (FAILED(render_target_->CreateSolidColorBrush(theme_.primaryText,
                                                     brush_.GetAddressOf()))) {
        error = L"Could not create main-window brush";
        render_target_.Reset();
        return false;
    }
    item_bitmaps_.clear();
    for (const auto& [id, image] : image_pixels_) BuildItemBitmap(image);
    return true;
}

void MainWindowUi::BuildItemBitmap(const ItemImage& image) {
    if (!render_target_) return;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
    if (SUCCEEDED(render_target_->CreateBitmap(D2D1::SizeU(image.width, image.height),
            image.pixels.data(), image.width * 4,
            D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96), &bitmap))) {
        item_bitmaps_[image.id] = std::move(bitmap);
    }
}

void MainWindowUi::ItemImagesReady() {
    // Direct2D 资源只在 UI 线程创建；保留 CPU 像素用于设备丢失后的重建。
    // Create Direct2D resources only on the UI thread; retain pixels for device recovery.
    for (auto& image : image_cache_.TakeReady()) {
        if (std::none_of(recent_.begin(), recent_.end(), [&](const auto& entry) {
                return entry.stableItemId == image.id;
            }) && !price_image_ids_.contains(image.id) && !hideout_image_ids_.contains(image.id)
                && !task_image_ids_.contains(image.id)) {
            image_cache_.Forget(image.id);
            continue;
        }
        BuildItemBitmap(image);
        image_pixels_[image.id] = std::move(image);
    }
    Invalidate();
}

void MainWindowUi::Paint() {
    PAINTSTRUCT paint{};
    BeginPaint(window_, &paint);
    if (render_target_ == nullptr) {
        std::wstring error;
        CreateRenderTarget(error);
    }
    if (render_target_ != nullptr && brush_ != nullptr) {
        if (price_browser_ && price_browser_->LastUpdated(price_mode_) != price_data_updated_)
            RefreshPriceRows(false, false);
        const D2D1_SIZE_F size = render_target_->GetSize();
        hideout_.Prepare(size.width,size.height,theme_);
        tasks_.Prepare(size.width,size.height,theme_);
        map_.Prepare(size.width,size.height,theme_);
        if(navigation_.Active()==BuiltinPageId::RaidHistory||page_transition_.ShowingOutgoing(BuiltinPageId::RaidHistory))raid_history_.Prepare(size.width,size.height,theme_);
        if(navigation_.Active()==BuiltinPageId::RaidHistory)for(const auto& id:raid_history_.VisibleImages())image_cache_.Request(id);
        if(navigation_.Active()==BuiltinPageId::Events||page_transition_.ShowingOutgoing(BuiltinPageId::Events))events_.Prepare(size.width,size.height,theme_,write_factory_.Get(),body_format_.Get(),label_format_.Get());
        if(navigation_.Active()==BuiltinPageId::Events)for(const auto& id:events_.VisibleImages())image_cache_.Request(id);
        if(navigation_.Active()==BuiltinPageId::Plugins||page_transition_.ShowingOutgoing(BuiltinPageId::Plugins))plugins_.Prepare(size.width,size.height,theme_,write_factory_.Get(),body_format_.Get(),label_format_.Get());
        RequestVisibleHideoutImages();
        RequestVisibleTaskImages();
        UiCanvas canvas{*render_target_.Get(), *brush_.Get(), *title_format_.Get(),
                        *page_title_format_.Get(), *label_format_.Get(),
                        *body_format_.Get(), *small_format_.Get(), write_factory_.Get()};
        render_target_->BeginDraw();
        render_target_->Clear(theme_.background);
        sidebar_.Draw(canvas, theme_, size.height, navigation_.Active(),
                      hovered_, pressed_);
        render_target_->PushAxisAlignedClip(
            D2D1::RectF(theme_.sidebarWidth, 0, size.width, size.height),
            D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        // 注册表负责身份和元数据；具体内置页仍由原有成员分派，避免改写业务与上下文状态。
        // The registry owns identity/metadata; concrete built-in dispatch remains transitional and preserves resident state.
        const auto drawPage=[&](PageId page) {
        canvas.inputWindow = page == navigation_.Active() ? window_ : nullptr;
        if(const auto it=plugin_views_.find(page);it!=plugin_views_.end()){
            it->second.Prepare(size.width,size.height,theme_,write_factory_.Get(),body_format_.Get(),label_format_.Get());it->second.Draw(canvas,theme_);
        }
        else if (page==BuiltinPageId::Hideout) hideout_.Draw(canvas,theme_,item_bitmaps_);
        else if (page==BuiltinPageId::Tasks) tasks_.Draw(canvas,theme_,item_bitmaps_);
        else if (page==BuiltinPageId::Map) map_.Draw(canvas,theme_);
        else if (page==BuiltinPageId::RaidHistory)raid_history_.Draw(canvas,theme_,item_bitmaps_);
        else if (page==BuiltinPageId::Events)events_.Draw(canvas,theme_,item_bitmaps_);
        else if (page==BuiltinPageId::Plugins)plugins_.Draw(canvas,theme_);
        else pages_.Draw(canvas, theme_, size.width, size.height,
                    page, scanner_, mode_menu_open_,
                    mode_hovered_, hovered_mode_, recent_, recent_filter_,
                    hovered_recent_tab_, recent_scroll_, recent_transition_,
                    price_mode_, hovered_price_tab_, price_scroll_, price_transition_, price_search_transition_, price_details_,
                    hovered_price_control_, price_dropdown_, price_dropdown_progress_,
                    price_sort_, price_sort_descending_, price_trader_side_,
                    price_search_,
                    (std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now().time_since_epoch()).count() / 500) % 2 == 0,
                    price_rows_, item_bitmaps_, price_page_, price_total_, &price_page_input_,mode_menu_progress_);
        if (page == BuiltinPageId::Settings) DrawLanguageSettings(canvas, size.width, size.height);
        if(page==BuiltinPageId::RaidHistory) {
            raid_scan_button_=raid_history_.ScanButtonBounds();
            DrawTextButton(canvas,theme_,raid_scan_button_,RaidScanLabel(),false,raid_scan_pressed_);
        }
        };
        {
            const auto pose=page_transition_.Sample(navigation_.Active());
            ScopedContentTransition transition(canvas,D2D1::Point2F(0,0),pose.opacity,1);
            D2D1_MATRIX_3X2_F transform;canvas.target.GetTransform(&transform);
            canvas.target.SetTransform(D2D1::Matrix3x2F::Translation(0,pose.shift)*transform);
            drawPage(pose.page);
        }
        if(CanGoBack()) {
            const float x=theme_.sidebarWidth+24,y=42;
            DrawNavigationButton(canvas,theme_,D2D1::RectF(x-16,y-16,x+16,y+16),
                NavigationGlyph::Back,true,back_hovered_,back_pressed_);
        }
        render_target_->PopAxisAlignedClip();
        if (render_target_->EndDraw() == D2DERR_RECREATE_TARGET) {
            brush_.Reset();
            render_target_.Reset();
            Invalidate();
        }
    }
    EndPaint(window_, &paint);
}

void MainWindowUi::Resize(UINT width, UINT height) {
    CancelScrollDrag();
    sidebar_.Tick(1);sidebar_.EnsureVisible(navigation_.Active(),DipHeight(),theme_);
    if (render_target_ != nullptr && width != 0 && height != 0) {
        if (render_target_->Resize(D2D1::SizeU(width, height)) == D2DERR_RECREATE_TARGET) {
            brush_.Reset();
            render_target_.Reset();
        }
    }
    recent_scroll_ = (std::min)(recent_scroll_,
        PageHost::RecentMaxScroll(DipHeight(),
            PageHost::RecentFilteredCount(recent_, recent_filter_)));
    recent_scroll_target_ = (std::min)(recent_scroll_target_,
        PageHost::RecentMaxScroll(DipHeight(),
            PageHost::RecentFilteredCount(recent_, recent_filter_)));
    price_scroll_ = (std::min)(price_scroll_, PageHost::PriceMaxScroll(DipHeight(), price_rows_.size(), DipWidth(), theme_, PriceDetailsScrollExtra()));
    price_scroll_target_ = (std::min)(price_scroll_target_, PageHost::PriceMaxScroll(DipHeight(), price_rows_.size(), DipWidth(), theme_, PriceDetailsScrollExtra()));
    if (navigation_.Active() == BuiltinPageId::Prices) RequestVisiblePriceImages();
    Invalidate();
}

void MainWindowUi::DpiChanged(UINT dpi) {
    CancelScrollDrag();
    dpi_ = dpi;
    if (render_target_ != nullptr) render_target_->SetDpi(
        static_cast<float>(dpi), static_cast<float>(dpi));
    Invalidate();
}

float MainWindowUi::DipHeight() const noexcept {
    RECT client{};
    GetClientRect(window_, &client);
    return static_cast<float>(client.bottom) / Scale();
}

float MainWindowUi::DipWidth() const noexcept {
    RECT client{};
    GetClientRect(window_, &client);
    return static_cast<float>(client.right) / Scale();
}

std::optional<RecentScrollbar> MainWindowUi::Scrollbar() const noexcept {
    // 过渡滑块仅作视觉插值，落定后才使用目标列表的拖动映射。
    // The transitioning thumb is visual only; enable target-list dragging once settled.
    if (navigation_.Active() != BuiltinPageId::RecentScans
        || recent_transition_.progress < 1.0F) return std::nullopt;
    RECT client{};
    GetClientRect(window_, &client);
    return PageHost::RecentScrollGeometry(static_cast<float>(client.right) / Scale(),
        DipHeight(), theme_, PageHost::RecentFilteredCount(recent_, recent_filter_),
        recent_scroll_);
}

std::optional<RecentScrollbar> MainWindowUi::PriceScrollbar() const noexcept {
    if (navigation_.Active() != BuiltinPageId::Prices || price_transition_.progress < 1.0F
        || price_search_transition_.progress < 1.0F)
        return std::nullopt;
    RECT client{}; GetClientRect(window_, &client);
    return PageHost::PriceScrollGeometry(static_cast<float>(client.right) / Scale(),
        DipHeight(), theme_, price_rows_.size(), price_scroll_, PriceDetailsScrollExtra());
}

std::optional<int> MainWindowUi::PricePagerAt(int x, int y) const {
    if (navigation_.Active() != BuiltinPageId::Prices || price_dropdown_
        || price_transition_.progress < 1) return {};
    const float left = theme_.sidebarWidth + theme_.contentPadding;
    const float right = DipWidth() - theme_.contentPadding;
    const float top = PageHost::PriceListTop(DipWidth(), theme_);
    const float dx = x / Scale(), dy = y / Scale();
    return HitPricePager(D2D1::RectF(left, top - PricePagerHeight, right, top - 8),
        dx, dy, price_page_, price_total_);
}

void MainWindowUi::SelectPricePage(std::size_t page) {
    page = (std::min)(page, PricePageCount(price_total_) - 1);
    price_page_input_.Blur();
    if (page == price_page_) return;
    price_page_ = page;
    // 复用列表重排动画并保留当前可见旧卡片，不增加常驻动画时钟。
    // Reuse list reflow with visible outgoing cards without adding a permanent animation clock.
    RefreshPriceRows(true, false);
    price_search_duration_ = 0.30F;
    CancelScrollDrag();
}

std::optional<std::size_t> MainWindowUi::PriceCardAt(int x, int y) const {
    if (navigation_.Active() != BuiltinPageId::Prices || price_dropdown_
        || price_search_transition_.progress < 1 || price_transition_.progress < 1) return std::nullopt;
    const float dx = x / Scale(), dy = y / Scale();
    float top = PageHost::PriceListTop(DipWidth(), theme_) - price_scroll_;
    if (dx < theme_.sidebarWidth + theme_.contentPadding || dx >= DipWidth() - theme_.contentPadding
        || dy < PageHost::PriceListTop(DipWidth(), theme_) || dy >= DipHeight() - 22) return std::nullopt;
    const float rowHeight = PageHost::PriceRowHeight(DipWidth(), theme_);
    for (std::size_t i = 0; i < price_rows_.size(); ++i) {
        if (dy >= top && dy < top + rowHeight - 10) return i;
        top += rowHeight + (price_rows_[i].item->id == price_details_.id ? price_details_.extent : 0);
    }
    return std::nullopt;
}

std::optional<int> MainWindowUi::PriceHistoryRangeAt(int x, int y) const {
    if (navigation_.Active() != BuiltinPageId::Prices || !price_details_.open
        || price_dropdown_ || price_details_.extent < kPriceDetailsHeight - 1) return std::nullopt;
    const float dx = x / Scale(), dy = y / Scale();
    if (dy < PageHost::PriceListTop(DipWidth(), theme_) || dy >= DipHeight() - 22) return std::nullopt;
    const auto found = std::find_if(price_rows_.begin(), price_rows_.end(), [&](const auto& row) {
        return row.item->id == price_details_.id;
    });
    if (found == price_rows_.end()) return std::nullopt;
    const float top = PageHost::PriceListTop(DipWidth(), theme_) - price_scroll_
        + (static_cast<float>(found - price_rows_.begin()) + 1) * PageHost::PriceRowHeight(DipWidth(), theme_) - 10;
    const auto bounds = D2D1::RectF(theme_.sidebarWidth + theme_.contentPadding, top,
        DipWidth() - theme_.contentPadding, top + kPriceDetailsHeight);
    for (std::size_t i = 0; i < kHistoryRanges.size(); ++i)
        if (HitTestDropdownRect(HistoryRangeRect(bounds, i), dx, dy)) return kHistoryRanges[i];
    return std::nullopt;
}

float MainWindowUi::PriceDetailsScrollExtra() const {
    return DetailsScrollExtra(price_details_, price_rows_.size(),
        PageHost::PriceRowHeight(DipWidth(), theme_),
        (std::max)(0.0F, DipHeight() - PageHost::PriceListTop(DipWidth(), theme_) - 22));
}

void MainWindowUi::RequestPriceHistory() {
    price_details_.hoverIndex.reset();
    price_details_.history.loading = true;
    price_history_.Request(price_details_.id, price_mode_, price_details_.days);
}

void MainWindowUi::PriceHistoryReady() {
    auto snapshot = price_history_.TakeReady();
    if (!snapshot || !price_details_.open || snapshot->itemId != price_details_.id
        || snapshot->mode != price_mode_ || snapshot->days != price_details_.days) return;
    if (snapshot->loading && snapshot->points.empty()) return;
    SetHistoryChart(price_details_, *snapshot);
    price_details_.history = std::move(*snapshot);
    price_details_.hoverIndex.reset();
    UpdateHistoryHover();
    Invalidate();
}

void MainWindowUi::UpdateHistoryHover() {
    const auto clear = [&] { price_details_.hoverIndex.reset(); };
    if (navigation_.Active() != BuiltinPageId::Prices || !price_details_.open
        || !price_details_.pointer || price_dropdown_) { clear(); return; }
    if (price_details_.chartProgress < 1) { clear(); return; }
    const auto row = std::find_if(price_rows_.begin(), price_rows_.end(), [&](const auto& item) {
        return item.item->id == price_details_.id;
    });
    if (row == price_rows_.end()) { clear(); return; }
    const float top = PageHost::PriceListTop(DipWidth(), theme_) - price_scroll_
        + (static_cast<float>(row - price_rows_.begin()) + 1) * PageHost::PriceRowHeight(DipWidth(), theme_) - 10;
    const auto plot = HistoryPlotRect(D2D1::RectF(theme_.sidebarWidth + theme_.contentPadding,
        top, DipWidth() - theme_.contentPadding, top + kPriceDetailsHeight));
    const auto hover = HitHistory(price_details_.history, plot, *price_details_.pointer);
    if (!hover) { clear(); return; }
    const auto& points = price_details_.history.points;
    const auto point = std::lower_bound(points.begin(), points.end(), hover->point.timeMs,
        [](const auto& sample, auto time) { return sample.timeMs < time; });
    price_details_.hoverTarget = static_cast<float>(point - points.begin());
    if (!price_details_.hoverIndex) price_details_.hoverIndex = price_details_.hoverTarget;
}

bool MainWindowUi::OnPriceSearch(int x, int y) const noexcept {
    if (navigation_.Active() != BuiltinPageId::Prices) return false;
    RECT client{}; GetClientRect(window_, &client);
    const float left = theme_.sidebarWidth + theme_.contentPadding;
    const float right = static_cast<float>(client.right) / Scale() - theme_.contentPadding;
    return price_search_.HitTest(D2D1::RectF(left, 84, right, 122),
        static_cast<float>(x) / Scale(), static_cast<float>(y) / Scale());
}

std::optional<PriceToolbarControl> MainWindowUi::PriceControlAt(int x, int y) const noexcept {
    if (navigation_.Active() != BuiltinPageId::Prices) return std::nullopt;
    RECT client{};
    GetClientRect(window_, &client);
    return pages_.PriceControlAt(static_cast<float>(x) / Scale(),
        static_cast<float>(y) / Scale(), theme_, static_cast<float>(client.right) / Scale(),
        price_dropdown_closing_ || price_dropdown_progress_ < 0.8F ? std::nullopt : price_dropdown_);
}

std::optional<PageId> MainWindowUi::HitTest(int x, int y) const noexcept {
    return sidebar_.HitTest(static_cast<float>(x) / Scale(),
                            static_cast<float>(y) / Scale(), DipHeight(), theme_);
}

bool MainWindowUi::OnModeSelector(int x, int y) const noexcept {
    if (navigation_.Active() != BuiltinPageId::Scanner) return false;
    const auto rect = pages_.ModeSelectorRect(theme_);
    const float dip_x = static_cast<float>(x) / Scale();
    const float dip_y = static_cast<float>(y) / Scale();
    return dip_x >= rect.left && dip_x < rect.right
        && dip_y >= rect.top && dip_y < rect.bottom;
}

std::optional<data::GameMode> MainWindowUi::ModeOptionAt(int x, int y) const noexcept {
    if (!mode_menu_open_ || navigation_.Active() != BuiltinPageId::Scanner)
        return std::nullopt;
    return pages_.ModeOptionAt(static_cast<float>(x) / Scale(),
                               static_cast<float>(y) / Scale(), theme_);
}

void MainWindowUi::Invalidate() const {
    if (window_ != nullptr) InvalidateRect(window_, nullptr, FALSE);
}

void MainWindowUi::MouseMove(int x, int y) {
    if(sidebar_.Move(x/Scale(),y/Scale(),DipHeight(),theme_)){pressed_.reset();hovered_.reset();Invalidate();return;}
    if(auto* view=ActivePluginView();view&&view->Move(x/Scale(),y/Scale()))Invalidate();
    if(navigation_.Active()==BuiltinPageId::Plugins&&plugins_.Move(x/Scale(),y/Scale()))Invalidate();
    if(navigation_.Active()==BuiltinPageId::Map&&map_.MouseMove(x/Scale(),y/Scale()))Invalidate();
    if(navigation_.Active()==BuiltinPageId::RaidHistory&&raid_history_.MouseMove(x/Scale(),y/Scale()))Invalidate();
    if(navigation_.Active()==BuiltinPageId::Events&&events_.MouseMove(x/Scale(),y/Scale()))Invalidate();
    const bool back=OnBackButton(x,y);
    if(back!=back_hovered_) { back_hovered_=back;Invalidate(); }
    if (navigation_.Active()==BuiltinPageId::Hideout) { hideout_.MouseMove(x/Scale(),y/Scale()); Invalidate(); }
    if (navigation_.Active()==BuiltinPageId::Tasks) { tasks_.MouseMove(x/Scale(),y/Scale()); Invalidate(); }
    if (navigation_.Active() == BuiltinPageId::Prices && price_details_.open) {
        price_details_.pointer.reset();
        const float dy = y / Scale();
        if (!price_dropdown_ && dy >= PageHost::PriceListTop(DipWidth(), theme_) && dy < DipHeight() - 22)
            price_details_.pointer = D2D1::Point2F(x / Scale(), dy);
        UpdateHistoryHover();
        Invalidate();
    }
    const auto language = LanguageAt(x, y);
    if (language != hovered_language_) { hovered_language_ = language; Invalidate(); }
    if (recent_scroll_grab_) {
        if (const auto bar = Scrollbar()) {
            // 拖动直接跟手，滚轮仍使用原有平滑动画。
            // Dragging follows the pointer directly; the wheel keeps its smooth animation.
            recent_scroll_ = recent_scroll_target_ = bar->OffsetFromThumbTop(
                static_cast<float>(y) / Scale() - *recent_scroll_grab_);
            Invalidate();
        }
        return;
    }
    if (price_scroll_grab_) {
        if (const auto bar = PriceScrollbar()) {
            price_scroll_ = price_scroll_target_ = bar->OffsetFromThumbTop(
                static_cast<float>(y) / Scale() - *price_scroll_grab_);
            Invalidate();
        }
        return;
    }
    const auto next = HitTest(x, y);
    const bool mode_hovered = OnModeSelector(x, y);
    const auto hovered_mode = ModeOptionAt(x, y);
    const auto recent_tab = navigation_.Active() == BuiltinPageId::RecentScans
        ? pages_.RecentTabAt(static_cast<float>(x) / Scale(),
                             static_cast<float>(y) / Scale(), theme_)
        : std::nullopt;
    const auto price_tab = navigation_.Active() == BuiltinPageId::Prices
        ? pages_.PriceTabAt(static_cast<float>(x) / Scale(),
                             static_cast<float>(y) / Scale(), theme_)
        : std::nullopt;
    const auto price_control = PriceControlAt(x, y);
    if (next != hovered_ || mode_hovered != mode_hovered_
        || hovered_mode != hovered_mode_ || recent_tab != hovered_recent_tab_
        || price_tab != hovered_price_tab_ || price_control != hovered_price_control_) {
        hovered_ = next;
        mode_hovered_ = mode_hovered;
        hovered_mode_ = hovered_mode;
        hovered_recent_tab_ = recent_tab;
        hovered_price_tab_ = price_tab;
        hovered_price_control_ = price_control;
        Invalidate();
    }
}

void MainWindowUi::MouseLeave() {
    if(navigation_.Active()==BuiltinPageId::Plugins&&plugins_.Leave())Invalidate();
    if(auto* view=ActivePluginView()){view->Move(-1,-1);Invalidate();}
    map_.MouseLeave();
    if(back_hovered_) { back_hovered_=false;Invalidate(); }
    price_details_.hoverIndex.reset();
    if (price_details_.pointer) { price_details_.pointer.reset(); Invalidate(); }
    if (hovered_language_) { hovered_language_.reset(); Invalidate(); }
    if (hovered_.has_value() || mode_hovered_ || hovered_mode_.has_value()
        || hovered_recent_tab_.has_value() || hovered_price_tab_.has_value()
        || hovered_price_control_.has_value()) {
        hovered_.reset();
        mode_hovered_ = false;
        hovered_mode_.reset();
        hovered_recent_tab_.reset();
        hovered_price_tab_.reset();
        hovered_price_control_.reset();
        Invalidate();
    }
}

void MainWindowUi::MouseDown(int x, int y) {
    if(sidebar_.Down(x/Scale(),y/Scale(),DipHeight(),theme_)){pressed_.reset();hovered_.reset();SetFocus(window_);Invalidate();return;}
    attribution_pressed_ = false;
    page_content_press_blocked_=false;
    back_pressed_=OnBackButton(x,y);
    if(back_pressed_) { Invalidate();return; }
    if(page_transition_.Active() && x/Scale()>=theme_.sidebarWidth) {
        page_content_press_blocked_=true;return;
    }
    if(navigation_.Active()==BuiltinPageId::Plugins&&x/Scale()>=theme_.sidebarWidth){plugins_.Down(x/Scale(),y/Scale());SetFocus(window_);Invalidate();return;}
    if(auto* view=ActivePluginView();view&&x/Scale()>=theme_.sidebarWidth){view->Down(x/Scale(),y/Scale());Invalidate();return;}
    raid_scan_pressed_=navigation_.Active()==BuiltinPageId::RaidHistory&&HitNavigationButton(raid_scan_button_,x/Scale(),y/Scale());
    if(raid_scan_pressed_){if(raid_scan_pending_)raid_scan_pressed_=false;Invalidate();return;}
    if(navigation_.Active()==BuiltinPageId::Settings&&preferences_.Down(x/Scale(),y/Scale())){Invalidate();return;}
    pressed_price_card_.reset();
    pressed_history_range_.reset();
    if (HitNavigationButton(attribution_button_, x / Scale(), y / Scale(),
            navigation_.Active() == BuiltinPageId::Settings)) {
        attribution_pressed_ = true;
        pressed_language_.reset();
        pressed_.reset();
        CancelScrollDrag();
        Invalidate();
        return;
    }
    pressed_language_ = LanguageAt(x, y);
    CancelScrollDrag();
    if (navigation_.Active()==BuiltinPageId::Hideout) {
        hideout_.MouseDown(x/Scale(),y/Scale()); SetFocus(window_); Invalidate();
    }
    if(navigation_.Active()==BuiltinPageId::RaidHistory&&x/Scale()>=theme_.sidebarWidth) {
        raid_history_.MouseDown(x/Scale(),y/Scale());SetFocus(window_);Invalidate();
    }
    if(navigation_.Active()==BuiltinPageId::Events&&x/Scale()>=theme_.sidebarWidth){events_.MouseDown(x/Scale(),y/Scale());SetFocus(window_);Invalidate();}
    if (navigation_.Active()==BuiltinPageId::Tasks) {
        tasks_.MouseDown(x/Scale(),y/Scale()); SetFocus(window_); Invalidate();
    }
    if (navigation_.Active()==BuiltinPageId::Map) {
        map_.MouseDown(x/Scale(),y/Scale()); SetFocus(window_); Invalidate();
    }
    if (const auto bar = Scrollbar()) {
        const float dx = static_cast<float>(x) / Scale();
        const float dy = static_cast<float>(y) / Scale();
        if (dx >= bar->track.left && dx < bar->track.right
            && dy >= bar->track.top && dy < bar->track.bottom) {
            recent_scroll_grab_ = dy >= bar->thumb.top && dy < bar->thumb.bottom
                ? dy - bar->thumb.top : (bar->thumb.bottom - bar->thumb.top) / 2.0F;
            recent_scroll_ = recent_scroll_target_ = bar->OffsetFromThumbTop(
                dy - *recent_scroll_grab_);
            pressed_.reset();
            pressed_recent_tab_.reset();
            Invalidate();
            return;
        }
    }
    if (const auto bar = PriceScrollbar()) {
        const float dx = static_cast<float>(x) / Scale();
        const float dy = static_cast<float>(y) / Scale();
        if (dx >= bar->track.left && dx < bar->track.right
            && dy >= bar->track.top && dy < bar->track.bottom) {
            price_scroll_grab_ = dy >= bar->thumb.top && dy < bar->thumb.bottom
                ? dy - bar->thumb.top : (bar->thumb.bottom - bar->thumb.top) / 2.0F;
            price_scroll_ = price_scroll_target_ = bar->OffsetFromThumbTop(dy - *price_scroll_grab_);
            pressed_.reset();
            pressed_recent_tab_.reset();
            Invalidate();
            return;
        }
    }
    pressed_recent_card_=RecentCardAt(x,y);
    pressed_ = HitTest(x, y);
    mode_pressed_ = OnModeSelector(x, y);
    pressed_mode_ = ModeOptionAt(x, y);
    pressed_recent_tab_ = navigation_.Active() == BuiltinPageId::RecentScans
        ? pages_.RecentTabAt(static_cast<float>(x) / Scale(),
                             static_cast<float>(y) / Scale(), theme_)
        : std::nullopt;
    pressed_price_tab_ = navigation_.Active() == BuiltinPageId::Prices
        ? pages_.PriceTabAt(static_cast<float>(x) / Scale(),
                             static_cast<float>(y) / Scale(), theme_)
        : std::nullopt;
    pressed_price_control_ = PriceControlAt(x, y);
    pressed_price_pager_ = PricePagerAt(x, y);
    if (pressed_price_pager_ != 0) price_page_input_.Blur();
    pressed_price_card_ = PriceCardAt(x, y);
    pressed_history_range_ = PriceHistoryRangeAt(x, y);
    if (OnPriceSearch(x, y)) {
        price_search_.Focus();
        SetFocus(window_);
        Invalidate();
    } else if (navigation_.Active() == BuiltinPageId::Prices) {
        price_search_.Blur();
    }
    if (pressed_ || mode_pressed_ || pressed_mode_ || pressed_recent_tab_
        || pressed_price_tab_ || pressed_price_control_) Invalidate();
}

std::optional<data::GameMode> MainWindowUi::MouseUp(int x, int y) {
    if(sidebar_.EndDrag()){pressed_.reset();hovered_.reset();Invalidate();return {};}
    if(raid_scan_pressed_) {
        raid_scan_pressed_=false;
        if(HitNavigationButton(raid_scan_button_,x/Scale(),y/Scale())){
            if(raid_scan_&&raid_scan_())SetRaidScanStatus(true,false,false);
            else {SetRaidScanStatus(false,false,true);return_pages_.push_back(BuiltinPageId::RaidHistory);SelectPage(BuiltinPageId::Settings);}
        }
        Invalidate();return {};
    }
    if(navigation_.Active()==BuiltinPageId::Settings&&preferences_.Up(window_,x/Scale(),y/Scale())){Invalidate();return {};}
    if(page_content_press_blocked_) { page_content_press_blocked_=false;return std::nullopt; }
    if(navigation_.Active()==BuiltinPageId::Plugins) {
        if(plugins_.Up(x/Scale(),y/Scale())&&plugin_refresh_)plugin_refresh_();
        if(const auto action=plugins_.TakeControlAction();action&&plugin_control_)plugin_control_(*action);
        if(x/Scale()>=theme_.sidebarWidth&&!back_pressed_){Invalidate();return {};}
    }
    if(auto* view=ActivePluginView()){
        if(const auto action=view->Up(x/Scale(),y/Scale());action&&plugin_action_)if(const auto* page=plugin_pages_.Find(navigation_.Active()))plugin_action_(*page,*action);
        if(x/Scale()>=theme_.sidebarWidth&&!back_pressed_){Invalidate();return {};}
    }
    if(back_pressed_) {
        back_pressed_=false;
        if(OnBackButton(x,y)) GoBack();
        Invalidate();return std::nullopt;
    }
    if (navigation_.Active()==BuiltinPageId::Hideout) {
        if(const auto item=hideout_.MouseUp(x/Scale(),y/Scale())) {
            OpenPriceItem(*item,hideout_.Mode()); return std::nullopt;
        }
    }
    if (navigation_.Active()==BuiltinPageId::Tasks) {
        if(const auto action=tasks_.MouseUp(x/Scale(),y/Scale())) {
            if(action->destination==TasksPage::Action::Destination::Prices)OpenPriceItem(action->id,tasks_.Mode());
            else {
                map_.SetMode(action->mode);
                if(map_.OpenInteraction(action->id)){
                    return_pages_.push_back(BuiltinPageId::Tasks);SelectPage(BuiltinPageId::Map);CancelScrollDrag();Invalidate();}
            }
            return std::nullopt;
        }
    }
    if(navigation_.Active()==BuiltinPageId::RaidHistory) {
        if(const auto scan=raid_history_.MouseUp(x/Scale(),y/Scale())) {OpenPriceItem(scan->stableItemId,scan->gameMode);return std::nullopt;}
        Invalidate();
    }
    if(navigation_.Active()==BuiltinPageId::Events){
        if(const auto action=events_.MouseUp(x/Scale(),y/Scale())) {
            if(action->kind==EventAction::Kind::Refresh){if(event_refresh_)event_refresh_();}
            else if(action->kind==EventAction::Kind::Source){if(events::SafeEventSourceUrl(action->id))ShellExecuteW(window_,L"open",EventWide(action->id).c_str(),nullptr,nullptr,SW_SHOWNORMAL);}
            else OpenEventAssociation(*action);
        }
        Invalidate();
    }
    if (navigation_.Active()==BuiltinPageId::Map) {
        const auto mode=map_.Mode();map_.MouseUp(x/Scale(),y/Scale());
        if(mode!=map_.Mode())tasks_.SetMode(map_.Mode());
        Invalidate();
    }
    if (attribution_pressed_) {
        attribution_pressed_ = false;
        if (HitNavigationButton(attribution_button_, x / Scale(), y / Scale(),
                navigation_.Active() == BuiltinPageId::Settings)) {
            // 只打开安装目录中的本地通知，不使用工作目录或网络地址。
            // Open only the local installed notice, never a working-directory or network URL.
            wchar_t executable[32768]{};
            const DWORD length = GetModuleFileNameW(nullptr, executable, ARRAYSIZE(executable));
            if (length > 0 && length < ARRAYSIZE(executable)) {
                const auto notice = std::filesystem::path(executable).parent_path() / L"NOTICE.md";
                ShellExecuteW(window_, L"open", notice.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }
        }
        Invalidate();
        return std::nullopt;
    }
    const auto language = LanguageAt(x, y);
    if (pressed_language_ && pressed_language_ == language) {
        const auto previous = UiLocalization().ActiveLocale();
        UiLocalization().SetLocale(UiLocalization().AvailableLocales()[*language].locale);
        std::wstring error;
        if (!CreateTextFormats(error)) UiLocalization().SetLocale(previous);
        Invalidate();
    }
    pressed_language_.reset();
    if (recent_scroll_grab_) {
        MouseMove(x, y);
        CancelScrollDrag();
        return std::nullopt;
    }
    if (price_scroll_grab_) {
        MouseMove(x, y);
        CancelScrollDrag();
        return std::nullopt;
    }
    if(pressed_recent_card_&&pressed_recent_card_==RecentCardAt(x,y)) {
        const auto entry=recent_[*pressed_recent_card_];pressed_recent_card_.reset();
        OpenPriceItem(entry.stableItemId,entry.gameMode);return std::nullopt;
    }
    pressed_recent_card_.reset();
    const auto released = HitTest(x, y);
    if(pressed_ && pressed_==released) { return_pages_.clear();back_hovered_=false; }
    const bool page_changed = pressed_.has_value() && pressed_ == released
        && navigation_.Active() != *pressed_ && SelectPage(*pressed_);
    const auto released_mode = ModeOptionAt(x, y);
    const auto released_recent_tab = navigation_.Active() == BuiltinPageId::RecentScans
        ? pages_.RecentTabAt(static_cast<float>(x) / Scale(),
                             static_cast<float>(y) / Scale(), theme_)
        : std::nullopt;
    const auto released_price_tab = navigation_.Active() == BuiltinPageId::Prices
        ? pages_.PriceTabAt(static_cast<float>(x) / Scale(),
                             static_cast<float>(y) / Scale(), theme_)
        : std::nullopt;
    const auto released_price_control = PriceControlAt(x, y);
    if (pressed_price_pager_ && pressed_price_pager_ == PricePagerAt(x, y)) {
        if (*pressed_price_pager_ == 0) {
            if (!price_page_input_.Focused()) {
                price_page_input_.SetText(std::to_wstring(price_page_ + 1));
                price_page_input_.Focus();
                (void)price_page_input_.HandleKeyDown('A', true);
            }
            price_search_.Blur(); SetFocus(window_);
        } else SelectPricePage(*pressed_price_pager_ < 0 ? price_page_ - 1 : price_page_ + 1);
        Invalidate();
    }
    pressed_price_pager_.reset();
    if (pressed_history_range_ && pressed_history_range_ == PriceHistoryRangeAt(x, y)) {
        price_details_.previousDays = price_details_.days;
        price_details_.underlineFrom = price_details_.underlineIndex;
        price_details_.tabProgress = 0;
        price_details_.days = *pressed_history_range_;
        RequestPriceHistory();
    } else if (pressed_price_card_ && pressed_price_card_ == PriceCardAt(x, y)) {
        const auto& id = price_rows_[*pressed_price_card_].item->id;
        if (price_details_.id != id && price_details_.rowIndex < *pressed_price_card_)
            price_scroll_ = (std::max)(0.0F, price_scroll_ - price_details_.extent);
        if (price_details_.id == id) price_details_.open = !price_details_.open;
        else { price_details_ = {}; price_details_.id = id; price_details_.open = true; }
        price_details_.Retarget(price_details_.open, *pressed_price_card_);
        if (price_details_.open) {
            price_scroll_target_ = price_details_.ScrollTarget(PageHost::PriceRowHeight(DipWidth(), theme_));
            RequestPriceHistory();
        }
    }
    pressed_price_card_.reset();
    pressed_history_range_.reset();
    std::optional<data::GameMode> selection;
    if (pressed_mode_ && pressed_mode_ == released_mode) {
        if (scanner_.mode != *pressed_mode_) {
            scanner_.mode = *pressed_mode_;
            selection = scanner_.mode;
        }
        mode_menu_open_ = false;
    } else if (mode_pressed_ && OnModeSelector(x, y)) {
        mode_menu_open_ = !mode_menu_open_;
    } else if (mode_menu_open_ || page_changed) {
        mode_menu_open_ = false;
    }
    if (pressed_recent_tab_ && pressed_recent_tab_ == released_recent_tab
        && recent_filter_ != *pressed_recent_tab_) {
        recent_transition_.outgoingMode = recent_filter_;
        recent_transition_.outgoingScroll = recent_scroll_;
        recent_underline_from_ = recent_transition_.underlineIndex;
        recent_filter_ = *pressed_recent_tab_;
        recent_transition_.progress = 0.0F;
        recent_tab_started_ = std::chrono::steady_clock::now();
        recent_scroll_ = 0.0F;
        recent_scroll_target_ = 0.0F;
    }
    if (pressed_price_tab_ && pressed_price_tab_ == released_price_tab
        && price_mode_ != *pressed_price_tab_) {
        price_transition_.outgoingMode = price_mode_;
        price_transition_.outgoingScroll = price_scroll_;
        price_transition_.outgoingCount = price_rows_.size();
        price_underline_from_ = price_transition_.underlineIndex;
        price_mode_ = *pressed_price_tab_;
        price_transition_.progress = 0.0F;
        price_tab_started_ = std::chrono::steady_clock::now();
        price_scroll_ = price_scroll_target_ = 0.0F;
        RefreshPriceRows();
    }
    if (pressed_price_control_ && pressed_price_control_ == released_price_control) {
        bool refresh = true;
        switch (*pressed_price_control_) {
        case PriceToolbarControl::SortDropdown: {
            const auto next = PriceDropdown::Sort;
            if (price_dropdown_ == next && !price_dropdown_closing_) {
                price_dropdown_closing_ = true;
            } else {
                price_dropdown_ = next;
                price_dropdown_closing_ = false;
                price_dropdown_progress_ = 0.0F;
            }
            price_dropdown_started_ = std::chrono::steady_clock::now();
            refresh = false;
            break;
        }
        case PriceToolbarControl::OrderDropdown: {
            const auto next = PriceDropdown::Order;
            if (price_dropdown_ == next && !price_dropdown_closing_) {
                price_dropdown_closing_ = true;
            } else {
                price_dropdown_ = next;
                price_dropdown_closing_ = false;
                price_dropdown_progress_ = 0.0F;
            }
            price_dropdown_started_ = std::chrono::steady_clock::now();
            refresh = false;
            break;
        }
        case PriceToolbarControl::SideDropdown: {
            const auto next = PriceDropdown::Side;
            if (price_dropdown_ == next && !price_dropdown_closing_) {
                price_dropdown_closing_ = true;
            } else {
                price_dropdown_ = next;
                price_dropdown_closing_ = false;
                price_dropdown_progress_ = 0.0F;
            }
            price_dropdown_started_ = std::chrono::steady_clock::now();
            refresh = false;
            break;
        }
        case PriceToolbarControl::FleaPrice:
            price_sort_ = data::PriceSortMode::FleaPrice;
            break;
        case PriceToolbarControl::TraderPrice:
            price_sort_ = data::PriceSortMode::TraderPrice;
            break;
        case PriceToolbarControl::Ascending:
            price_sort_descending_ = false;
            break;
        case PriceToolbarControl::Descending:
            price_sort_descending_ = true;
            break;
        case PriceToolbarControl::TraderSell:
            price_trader_side_ = data::PriceTraderSide::Sell;
            break;
        case PriceToolbarControl::TraderBuy:
            price_trader_side_ = data::PriceTraderSide::Buy;
            break;
        case PriceToolbarControl::FleaChange:
            price_sort_ = data::PriceSortMode::FleaChange;
            break;
        case PriceToolbarControl::FleaSell:
            refresh = false;
            break;
        }
        if (refresh) {
            price_scroll_ = price_scroll_target_ = 0.0F;
            RefreshPriceRows();
            price_dropdown_closing_ = true;
            price_dropdown_started_ = std::chrono::steady_clock::now();
        }
    } else if (price_dropdown_ && !price_dropdown_closing_) {
        price_dropdown_closing_ = true;
        price_dropdown_started_ = std::chrono::steady_clock::now();
    }
    if (page_changed) {
        recent_transition_.progress = 1.0F;
        recent_transition_.underlineIndex = static_cast<float>(recent_filter_);
        recent_scroll_ = recent_scroll_target_;
    }
    pressed_.reset();
    mode_pressed_ = false;
    pressed_mode_.reset();
    hovered_mode_.reset();
    pressed_recent_tab_.reset();
    pressed_price_tab_.reset();
    pressed_price_control_.reset();
    Invalidate();
    return selection;
}

std::optional<std::size_t> MainWindowUi::RecentCardAt(int x,int y) const {
    if(navigation_.Active()!=BuiltinPageId::RecentScans||recent_transition_.progress<1)return {};
    const float left=theme_.sidebarWidth+theme_.contentPadding;
    const float right=(std::min)(DipWidth()-theme_.contentPadding,left+760.0F);
    const float dx=x/Scale(),dy=y/Scale();
    if(dx<left||dx>=right||dy<139||dy>=DipHeight()-22)return {};
    std::size_t row{};
    for(std::size_t i=0;i<recent_.size();++i)if(recent_[i].gameMode==recent_filter_) {
        const float top=139+static_cast<float>(row++)*130-recent_scroll_;
        if(dy>=top&&dy<top+118)return i;
    }
    return {};
}
void MainWindowUi::SetScannerState(ScannerPageState state) {
    state.shortcut=ScanShortcutText(preferences_.value.scanKey,preferences_.value.scanModifiers);
    scanner_ = state;
    Invalidate();
}

void MainWindowUi::SetRecentScans(std::vector<data::RecentScanEntry> entries) {
    raid_history_.SetScans(entries);
    CancelScrollDrag();
    const bool new_selected_entry = !entries.empty()
        && (recent_.empty() || entries.front().scanId != recent_.front().scanId)
        && entries.front().gameMode == recent_filter_;
    recent_ = std::move(entries);
    for (auto it = image_pixels_.begin(); it != image_pixels_.end();) {
        if (std::none_of(recent_.begin(), recent_.end(), [&](const auto& entry) {
                return entry.stableItemId == it->first;
            }) && !price_image_ids_.contains(it->first) && !hideout_image_ids_.contains(it->first)) {
            image_cache_.Forget(it->first);
            item_bitmaps_.erase(it->first);
            it = image_pixels_.erase(it);
        } else ++it;
    }
    for (const auto& entry : recent_) image_cache_.Request(entry.stableItemId);
    if (new_selected_entry) {
        recent_scroll_ = 0.0F;
        recent_scroll_target_ = 0.0F;
    } else {
        const float maximum = PageHost::RecentMaxScroll(DipHeight(),
            PageHost::RecentFilteredCount(recent_, recent_filter_));
        recent_scroll_ = (std::min)(recent_scroll_, maximum);
        recent_scroll_target_ = (std::min)(recent_scroll_target_, maximum);
    }
    Invalidate();
}

bool MainWindowUi::MouseWheel(int x, int y, int delta, bool control) {
    if(x/Scale()<theme_.sidebarWidth){const bool handled=sidebar_.Wheel(delta,x/Scale(),y/Scale(),DipHeight(),theme_);if(handled){pressed_.reset();hovered_.reset();Invalidate();}return handled;}
    if(auto* view=ActivePluginView()){const bool handled=view->Wheel(delta,x/Scale(),y/Scale());if(handled)Invalidate();return handled;}
    if(navigation_.Active()==BuiltinPageId::Plugins){const bool handled=plugins_.Wheel(delta,x/Scale(),y/Scale());if(handled)Invalidate();return handled;}
    if(navigation_.Active()==BuiltinPageId::RaidHistory) {const bool handled=raid_history_.Wheel(delta,x/Scale(),y/Scale());if(handled)Invalidate();return handled;}
    if(navigation_.Active()==BuiltinPageId::Events){const bool handled=events_.Wheel(delta,x/Scale(),y/Scale());if(handled)Invalidate();return handled;}
    if(navigation_.Active()==BuiltinPageId::Map){const bool handled=map_.Wheel(delta,x/Scale(),y/Scale(),control);
        if(handled)Invalidate();return handled;}
    if (navigation_.Active()==BuiltinPageId::Hideout && x/Scale()>=theme_.sidebarWidth) return hideout_.Wheel(delta,x/Scale(),y/Scale());
    if (navigation_.Active()==BuiltinPageId::Tasks && x/Scale()>=theme_.sidebarWidth) return tasks_.Wheel(delta,x/Scale(),y/Scale());
    if (navigation_.Active() == BuiltinPageId::Settings && x / Scale() >= theme_.sidebarWidth) {
        const float maximum = (std::max)(0.0F,
            static_cast<float>(UiLocalization().AvailableLocales().size()) * 42 - (DipHeight() - 178));
        language_scroll_ = std::clamp(language_scroll_ - static_cast<float>(delta) / WHEEL_DELTA * 42,
            0.0F, maximum);
        Invalidate();
        return false;
    }
    if (recent_scroll_grab_ || price_scroll_grab_) return false;
    if (static_cast<float>(x) / Scale() < theme_.sidebarWidth || y < 0) return false;
    if (navigation_.Active() == BuiltinPageId::Prices) {
        const float maximum = PageHost::PriceMaxScroll(DipHeight(), price_rows_.size(), DipWidth(), theme_, PriceDetailsScrollExtra());
        price_scroll_target_ = std::clamp(price_scroll_target_
            - static_cast<float>(delta) / WHEEL_DELTA * 66.0F, 0.0F, maximum);
    if (price_scroll_ != price_scroll_target_
            && price_scroll_tick_ == std::chrono::steady_clock::time_point{})
        price_scroll_tick_ = std::chrono::steady_clock::now();
        RequestVisiblePriceImages();
        return price_scroll_ != price_scroll_target_;
    }
    if (navigation_.Active() != BuiltinPageId::RecentScans) return false;
    const float maximum = PageHost::RecentMaxScroll(DipHeight(),
        PageHost::RecentFilteredCount(recent_, recent_filter_));
    recent_scroll_target_ = std::clamp(recent_scroll_target_
        - static_cast<float>(delta) / WHEEL_DELTA * 66.0F, 0.0F, maximum);
    if (recent_scroll_ != recent_scroll_target_
        && recent_scroll_tick_ == std::chrono::steady_clock::time_point{})
        recent_scroll_tick_ = std::chrono::steady_clock::now();
    return recent_scroll_ != recent_scroll_target_;
}

bool MainWindowUi::AnimationActive() const noexcept {
    if(const auto* view=PluginView(navigation_.Active());view&&view->Animating())return true;
    if(navigation_.Active()==BuiltinPageId::Plugins&&plugins_.Animating())return true;
    if(navigation_.Active()==BuiltinPageId::Scanner&&mode_menu_progress_!=(mode_menu_open_?1.0F:0.0F))return true;
    if(sidebar_.Animating())return true;
    if(navigation_.Active()==BuiltinPageId::Events&&events_.Animating())return true;
    const bool mapVisible=navigation_.Active()==BuiltinPageId::Map||page_transition_.ShowingOutgoing(BuiltinPageId::Map);
    const bool tasksVisible=navigation_.Active()==BuiltinPageId::Tasks
        || page_transition_.ShowingOutgoing(BuiltinPageId::Tasks);
    return (navigation_.Active()==BuiltinPageId::RaidHistory&&raid_history_.Animating()) || page_transition_.Active() || hideout_.Animating() || (tasksVisible&&tasks_.Animating()) || (mapVisible&&map_.Animating()) || recent_transition_.progress < 1.0F
        || price_transition_.progress < 1.0F
        || price_search_transition_.progress < 1.0F
        || price_details_.tabProgress < 1 || price_details_.chartProgress < 1
        || (price_details_.hoverIndex && *price_details_.hoverIndex != price_details_.hoverTarget)
        || price_details_.expansionProgress < 1
        || price_dropdown_ && ((price_dropdown_closing_ && price_dropdown_progress_ > 0.0F)
            || (!price_dropdown_closing_ && price_dropdown_progress_ < 1.0F))
        || std::abs(recent_scroll_target_ - recent_scroll_) >= 0.75F
        || std::abs(price_scroll_target_ - price_scroll_) >= 0.75F;
}

bool MainWindowUi::AnimationTick() {
    const auto now = std::chrono::steady_clock::now();
    const bool outgoingMap=page_transition_.ShowingOutgoing(BuiltinPageId::Map);
    page_transition_.Tick(now);
    if(outgoingMap&&!page_transition_.ShowingOutgoing(BuiltinPageId::Map)&&navigation_.Active()!=BuiltinPageId::Map)map_.ReleaseDetailImages();
    if (price_search_transition_.progress < 1) {
        price_search_transition_.progress = std::clamp(
            std::chrono::duration<float>(now - price_search_started_).count() / price_search_duration_, 0.0F, 1.0F);
        if (price_search_transition_.progress >= 1) price_search_transition_.rows.clear();
    }
    const float elapsed = recent_scroll_tick_ == std::chrono::steady_clock::time_point{}
        ? 0.016F
        : std::chrono::duration<float>(now - recent_scroll_tick_).count();
    recent_scroll_tick_ = now;
    sidebar_.Tick(std::clamp(elapsed,0.0F,0.05F));
    if(navigation_.Active()==BuiltinPageId::Plugins)plugins_.Tick(elapsed);
    if(auto* view=ActivePluginView())view->Tick(elapsed);
    if(navigation_.Active()==BuiltinPageId::Scanner)mode_menu_progress_=AdvanceDropdownTransition(mode_menu_progress_,!mode_menu_open_,std::clamp(elapsed,0.0F,0.05F));
    hideout_.Tick(elapsed);
    if(navigation_.Active()==BuiltinPageId::RaidHistory)raid_history_.Tick(elapsed);
    if(navigation_.Active()==BuiltinPageId::Events)events_.Tick(elapsed);
    if(navigation_.Active()==BuiltinPageId::Map||page_transition_.ShowingOutgoing(BuiltinPageId::Map))map_.Tick(elapsed);
    if (navigation_.Active()==BuiltinPageId::Tasks || page_transition_.ShowingOutgoing(BuiltinPageId::Tasks))
        tasks_.Tick(elapsed);
    const float remaining = recent_scroll_target_ - recent_scroll_;
    if (std::abs(remaining) < 0.75F) {
        recent_scroll_ = recent_scroll_target_;
    } else {
        recent_scroll_ += remaining * (1.0F - std::exp(-24.0F * std::clamp(elapsed, 0.0F, 0.05F)));
    }
    const float priceElapsed = price_scroll_tick_ == std::chrono::steady_clock::time_point{}
        ? 0.016F : std::chrono::duration<float>(now - price_scroll_tick_).count();
    price_scroll_tick_ = now;
    AdvanceDetailsExpansion(price_details_, std::clamp(priceElapsed, 0.0F, 0.05F));
    if (price_details_.expansionProgress >= 1) {
        if (!price_details_.open) price_details_.history = {};
    }
    const float maximum = PageHost::PriceMaxScroll(DipHeight(), price_rows_.size(),
        DipWidth(), theme_, PriceDetailsScrollExtra());
    price_scroll_target_ = (std::min)(price_scroll_target_, maximum);
    price_scroll_ = (std::min)(price_scroll_, maximum);
    const float priceRemaining = price_scroll_target_ - price_scroll_;
    if (std::abs(priceRemaining) < 0.75F) price_scroll_ = price_scroll_target_;
    else price_scroll_ += priceRemaining
        * (1.0F - std::exp(-24.0F * std::clamp(priceElapsed, 0.0F, 0.05F)));
    if (recent_transition_.progress < 1.0F) {
        constexpr float kTabDurationSeconds = 0.36F;
        recent_transition_.progress = std::clamp(
            std::chrono::duration<float>(now - recent_tab_started_).count()
                / kTabDurationSeconds, 0.0F, 1.0F);
        const float targetIndex = static_cast<float>(recent_filter_);
        const float lineProgress = SampleTabTransition(
            recent_transition_.progress).underlineProgress;
        recent_transition_.underlineIndex = recent_underline_from_
            + (targetIndex - recent_underline_from_) * lineProgress;
        if (recent_transition_.progress >= 1.0F)
            recent_transition_.underlineIndex = targetIndex;
    }
    if (price_transition_.progress < 1.0F) {
        constexpr float kTabDurationSeconds = 0.36F;
        price_transition_.progress = std::clamp(
            std::chrono::duration<float>(now - price_tab_started_).count()
                / kTabDurationSeconds, 0.0F, 1.0F);
        const float targetIndex = static_cast<float>(price_mode_);
        const float lineProgress = SampleTabTransition(price_transition_.progress).underlineProgress;
        price_transition_.underlineIndex = price_underline_from_
            + (targetIndex - price_underline_from_) * lineProgress;
        if (price_transition_.progress >= 1.0F) price_transition_.underlineIndex = targetIndex;
    }
    if (price_dropdown_) {
        // 用时间增量推进开合，快速反向点击时保留当前姿态。
        // Advance by elapsed time so reversing a menu retains its current pose.
        const float dropdownElapsed = std::chrono::duration<float>(now - price_dropdown_started_).count();
        price_dropdown_started_ = now;
        price_dropdown_progress_ = AdvanceDropdownTransition(
            price_dropdown_progress_, price_dropdown_closing_, dropdownElapsed);
        if (price_dropdown_closing_ && price_dropdown_progress_ <= 0.0F) {
            price_dropdown_.reset();
            price_dropdown_closing_ = false;
            price_dropdown_progress_ = 1.0F;
        }
    }
    AdvanceHistoryTransition(price_details_, std::clamp(elapsed, 0.0F, 0.05F));
    UpdateHistoryHover();
    if (price_details_.hoverIndex)
        *price_details_.hoverIndex = AdvanceHistoryHover(*price_details_.hoverIndex,
            price_details_.hoverTarget, elapsed);
    if (navigation_.Active() == BuiltinPageId::Prices) RequestVisiblePriceImages();
    if (!AnimationActive()) { recent_scroll_tick_ = {}; price_scroll_tick_ = {}; }
    Invalidate();
    return AnimationActive();
}

} // namespace noven::ui
