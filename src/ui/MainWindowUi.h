#pragma once

#include "ui/PageHost.h"
#include "ui/Sidebar.h"
#include "ui/ItemImageCache.h"
#include "ui/SearchBox.h"
#include "data/PriceBrowser.h"
#include "ui/HideoutPage.h"
#include "ui/TasksPage.h"
#include "ui/MapPage.h"
#include "ui/RaidHistoryPage.h"
#include "ui/EventsPage.h"
#include "ui/PageTransition.h"
#include "ui/PreferencesPanel.h"

#include <d2d1.h>
#include <dwrite.h>
#include <windows.h>
#include <wrl/client.h>

#include <optional>
#include <string>
#include <chrono>
#include <memory>
#include <unordered_set>

namespace noven::ui {

// 主窗口的绘制与导航状态；不拥有 OCR、扫描器或经济缓存。
// Main-window drawing and navigation state; owns no OCR, scanner, or economy cache.
class MainWindowUi final {
public:
    void SetPreferences(const data::AppSettings& value){preferences_.value=value;scanner_.shortcut=ScanShortcutText(value.scanKey,value.scanModifiers);Invalidate();}
    void SetPreferencesHandler(std::function<bool(const data::AppSettings&)> handler){preferences_.changed=std::move(handler);}
    bool RecordingShortcut() const noexcept {return preferences_.Recording();}
    const data::AppSettings& Preferences() const noexcept {return preferences_.value;}
    void SetRaidScanHandler(std::function<bool()> handler){raid_scan_=std::move(handler);}
    bool Initialize(HWND window, std::wstring& error);
    void Paint();
    void Resize(UINT width, UINT height);
    void DpiChanged(UINT dpi);
    void MouseMove(int x, int y);
    void MouseLeave();
    void MouseDown(int x, int y);
    void CancelScrollDrag() noexcept { recent_scroll_grab_.reset(); price_scroll_grab_.reset(); hideout_.CancelDrag(); tasks_.CancelDrag(); map_.CancelDrag(); raid_history_.CancelDrag(); events_.CancelDrag(); }
    void SetEvents(std::vector<events::EventRecord> records,events::EventRefreshState state,std::optional<events::Timestamp> refreshed) {
        events_.SetSnapshot(std::move(records),std::move(state),refreshed);Invalidate();
    }
    void SetEventRefreshHandler(std::function<bool()> handler){event_refresh_=std::move(handler);}
    const EventsPage& Events() const noexcept {return events_;}
    bool OpenEventAssociation(const EventAction& action);
    static constexpr UINT_PTR EventClockTimerId=5;
    void EventClockTick();
    void SetRaidSessions(std::vector<raid::RaidSession> sessions,std::optional<raid::RaidSession> active,bool unavailable) {
        raid_history_.SetSessions(std::move(sessions),std::move(active),unavailable);Invalidate();
    }
    const RaidHistoryPage& RaidHistory() const noexcept {return raid_history_;}
    [[nodiscard]] std::optional<data::GameMode> MouseUp(int x, int y);
    [[nodiscard]] bool MouseWheel(int x, int y, int delta, bool control=false);
    [[nodiscard]] bool AnimationTick();
    [[nodiscard]] bool AnimationActive() const noexcept;
    void SetScannerState(ScannerPageState state);
    void SetRecentScans(std::vector<data::RecentScanEntry> entries);
    void StartItemImages(const std::filesystem::path& directory) { image_cache_.Start(window_, directory); }
    void StopItemImages() { image_cache_.Stop(); }
    void StartPriceHistory(const std::filesystem::path& directory) { price_history_.Start(window_, directory); }
    void StopPriceHistory() { price_history_.Stop(); }
    void PriceHistoryReady();
    void ItemImagesReady();
    static constexpr UINT_PTR MapClockTimerId=4;
    void MapClockTick();
    void SetPriceDataSources(const data::ItemCatalog& catalog, const data::ItemEconomyStore& economy);
    void SetHideoutDataSources(const std::filesystem::path& directory, const data::ItemCatalog& catalog,
        const data::ItemEconomyStore& economy) { hideout_.Initialize(directory,catalog,economy); }
    void SetTaskDataSources(const std::filesystem::path& directory,const data::ItemCatalog& catalog) {
        tasks_.Initialize(directory,catalog);BindEventCatalogs();
    }
    bool SetMapDataSources(const std::filesystem::path& assets,std::wstring& error){
        const bool loaded=map_.Initialize(assets,error);
        for(const auto mode:{data::GameMode::Pvp,data::GameMode::Pve})
            if(loaded)tasks_.SetMapLinks(map_.Catalog(mode),mode);
            else tasks_.SetMapLinks(data::MapCatalog{},mode);
        raid_history_.SetMaps(map_.Catalog(data::GameMode::Pvp));
        BindEventCatalogs();
        return loaded;}
    const TasksPage& Tasks() const noexcept{return tasks_;}
    void SetMapAssetGeneration(std::filesystem::path generation){map_.SetAssetGeneration(std::move(generation));Invalidate();}
    [[nodiscard]] bool KeyDown(WPARAM key, bool control);
    [[nodiscard]] bool Char(wchar_t character);
    [[nodiscard]] bool Ready() const noexcept { return window_ != nullptr; }
    [[nodiscard]] MainPage ActivePage() const noexcept { return navigation_.Active(); }
    bool GoBack();
    bool CanGoBack() const noexcept { return !return_pages_.empty(); }
    const SearchBox& PriceSearch() const noexcept { return price_search_; }
    const std::string& PriceExactId() const noexcept {return price_exact_id_;}
    const PriceSearchTransition& PriceListTransition() const noexcept { return price_search_transition_; }
    [[nodiscard]] std::size_t PricePage() const noexcept { return price_page_; }
    [[nodiscard]] std::size_t PriceTotal() const noexcept { return price_total_; }
    const MapPage& Map() const noexcept { return map_; }

private:
    bool SelectPage(MainPage page);
    PageTransition<MainPage> page_transition_;
    bool page_content_press_blocked_{};
    bool OnBackButton(int x,int y) const noexcept;
    std::vector<MainPage> return_pages_;
    void BindEventCatalogs(){events_.SetCatalogs(event_items_,&tasks_.Catalog(),&map_.Catalog(data::GameMode::Pvp));}
    bool back_hovered_{},back_pressed_{};
    bool CreateTextFormats(std::wstring& error);
    void DrawLanguageSettings(const UiCanvas& canvas, float width, float height);
    [[nodiscard]] std::optional<std::size_t> LanguageAt(int x, int y) const;
    bool CreateRenderTarget(std::wstring& error);
    void BuildItemBitmap(const ItemImage& image);
    void RefreshPriceRows(bool animateSearch = false, bool resetPage = true);
    [[nodiscard]] std::optional<int> PricePagerAt(int x, int y) const;
    void SelectPricePage(std::size_t page);
    void OpenPriceItem(const std::string& id,data::GameMode mode);
    std::optional<std::size_t> RecentCardAt(int x,int y) const;
    void RequestVisiblePriceImages();
    void RequestVisibleHideoutImages();
    void RequestVisibleTaskImages();
    [[nodiscard]] std::optional<std::size_t> PriceCardAt(int x, int y) const;
    [[nodiscard]] std::optional<int> PriceHistoryRangeAt(int x, int y) const;
    void RequestPriceHistory();
    [[nodiscard]] float PriceDetailsScrollExtra() const;
    void UpdateHistoryHover();
    void Invalidate() const;
    [[nodiscard]] std::optional<MainPage> HitTest(int x, int y) const noexcept;
    [[nodiscard]] bool OnModeSelector(int x, int y) const noexcept;
    [[nodiscard]] std::optional<data::GameMode> ModeOptionAt(int x, int y) const noexcept;
    [[nodiscard]] float DipHeight() const noexcept;
    [[nodiscard]] float DipWidth() const noexcept;
    [[nodiscard]] std::optional<RecentScrollbar> Scrollbar() const noexcept;
    [[nodiscard]] std::optional<RecentScrollbar> PriceScrollbar() const noexcept;
    [[nodiscard]] bool OnPriceSearch(int x, int y) const noexcept;
    [[nodiscard]] std::optional<PriceToolbarControl> PriceControlAt(int x, int y) const noexcept;
    [[nodiscard]] float Scale() const noexcept { return static_cast<float>(dpi_) / 96.0F; }

    HWND window_{};
    float language_scroll_{};
    std::optional<std::size_t> hovered_language_;
    std::optional<std::size_t> pressed_language_;
    D2D1_RECT_F attribution_button_{};
    bool attribution_pressed_{};
    UINT dpi_{96};
    NavigationState navigation_;
    ScannerPageState scanner_;
    UiTheme theme_;
    Sidebar sidebar_;
    PreferencesPanel preferences_;
    std::function<bool()> raid_scan_;
    D2D1_RECT_F raid_scan_button_{};
    bool raid_scan_pressed_{};
    PageHost pages_;
    HideoutPage hideout_;
    TasksPage tasks_;
    MapPage map_;
    RaidHistoryPage raid_history_;
    EventsPage events_;
    const data::ItemCatalog* event_items_{};
    std::function<bool()> event_refresh_;
    std::unordered_set<std::string> hideout_image_ids_;
    std::unordered_set<std::string> task_image_ids_;
    std::unique_ptr<data::PriceBrowserModel> price_browser_;
    std::vector<data::PriceRow> price_rows_;
    std::size_t price_page_{};
    std::size_t price_total_{};
    std::optional<int> pressed_price_pager_;
    SearchBox price_page_input_;
    std::wstring price_query_;
    std::string price_exact_id_;
    SearchBox price_search_;
    data::GameMode price_mode_{data::GameMode::Pvp};
    float price_scroll_{};
    float price_scroll_target_{};
    std::optional<float> price_scroll_grab_;
    std::optional<data::GameMode> hovered_price_tab_;
    std::optional<data::GameMode> pressed_price_tab_;
    std::optional<PriceToolbarControl> hovered_price_control_;
    std::optional<PriceToolbarControl> pressed_price_control_;
    std::optional<PriceDropdown> price_dropdown_;
    bool price_dropdown_closing_{};
    float price_dropdown_progress_{1.0F};
    std::chrono::steady_clock::time_point price_dropdown_started_{};
    data::PriceSortMode price_sort_{data::PriceSortMode::FleaPrice};
    bool price_sort_descending_{true};
    data::PriceTraderSide price_trader_side_{data::PriceTraderSide::Sell};
    PriceTabTransition price_transition_{};
    PriceSearchTransition price_search_transition_;
    data::PriceHistoryService price_history_;
    PriceDetailsState price_details_;
    std::optional<std::size_t> pressed_price_card_;
    std::optional<int> pressed_history_range_;
    std::chrono::steady_clock::time_point price_search_started_{};
    float price_search_duration_{0.65F};
    float price_underline_from_{};
    std::chrono::steady_clock::time_point price_tab_started_{};
    std::chrono::steady_clock::time_point price_scroll_tick_{};
    std::unordered_set<std::string> price_image_ids_;
    std::size_t price_image_window_start_{static_cast<std::size_t>(-1)};
    std::chrono::system_clock::time_point price_data_updated_{};
    std::vector<data::RecentScanEntry> recent_;
    std::optional<std::size_t> pressed_recent_card_;
    ItemImageCache image_cache_;
    std::unordered_map<std::string, ItemImage> image_pixels_;
    ItemBitmapMap item_bitmaps_;
    float recent_scroll_{};
    float recent_scroll_target_{};
    std::optional<float> recent_scroll_grab_;
    std::chrono::steady_clock::time_point recent_scroll_tick_{};
    data::GameMode recent_filter_{data::GameMode::Pvp};
    RecentTabTransition recent_transition_{};
    float recent_underline_from_{};
    std::chrono::steady_clock::time_point recent_tab_started_{};
    std::optional<data::GameMode> hovered_recent_tab_;
    std::optional<data::GameMode> pressed_recent_tab_;
    std::optional<MainPage> hovered_;
    std::optional<MainPage> pressed_;
    bool mode_menu_open_{};
    float mode_menu_progress_{};
    bool mode_hovered_{};
    bool mode_pressed_{};
    std::optional<data::GameMode> hovered_mode_;
    std::optional<data::GameMode> pressed_mode_;
    Microsoft::WRL::ComPtr<ID2D1Factory> d2d_factory_;
    Microsoft::WRL::ComPtr<IDWriteFactory> write_factory_;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> render_target_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> title_format_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> page_title_format_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> label_format_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> body_format_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> small_format_;
};

} // namespace noven::ui
