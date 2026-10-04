#pragma once
#include "events/EventBrowser.h"
#include "events/EventService.h"
#include "data/ItemCatalog.h"
#include "data/TaskCatalog.h"
#include "data/MapCatalog.h"
#include "ui/SearchBox.h"
#include "ui/Scrollbar.h"
#include "ui/PageComponents.h"
#include "ui/TabSelectionAnimation.h"
#include <unordered_map>

namespace noven::ui {
struct EventAction {
    enum class Kind { Map, Task, Item, Source, Refresh, Original };
    Kind kind{};std::string id;
    bool operator==(const EventAction&) const = default;
};
// 页面保留编辑/选择/滚动；所有业务数据由服务快照注入，不读缓存或远程内容。
// Resident input/selection/scroll state; service snapshots supply data, with no cache or remote parsing.
class EventsPage final {
public:
    EventsPage(){filterAnimation_.Select(0,5,true);}
    void SetSnapshot(std::vector<events::EventRecord> records,events::EventRefreshState state,std::optional<events::Timestamp> refreshed);
    void SetCatalogs(const data::ItemCatalog* items,const data::TaskCatalog* tasks,const data::MapCatalog* maps);
    void Prepare(float width,float height,const UiTheme& theme,IDWriteFactory* factory,IDWriteTextFormat* body,IDWriteTextFormat* label);
    void Draw(const UiCanvas& canvas,const UiTheme& theme,const std::unordered_map<std::string,Microsoft::WRL::ComPtr<ID2D1Bitmap>>& images) const;
    void MouseDown(float x,float y);
    std::optional<EventAction> MouseUp(float x,float y);
    bool MouseMove(float x,float y);
    bool Wheel(int delta,float x,float y);
    bool Key(WPARAM key,bool control);
    bool Char(wchar_t value);
    void CancelDrag(){grab_.reset();pressed_.reset();}
    void Blur();
    bool Animating() const noexcept;
    void Tick(float seconds);
    bool ClockTick(events::Timestamp now);
    bool Select(std::string_view id);
    void SetFilter(std::optional<events::EventStatus> status,std::wstring query);
    const events::EventBrowser& Browser() const noexcept{return browser_;}
    const std::string& SelectedId() const noexcept{return selected_;}
    const SearchBox& Search() const noexcept{return search_;}
    float ListScroll() const noexcept{return listScroll_;}
    float DetailScroll() const noexcept{return detailScroll_;}
    bool Narrow() const noexcept{return narrow_;}
    bool ShowingDetail() const noexcept{return !narrow_||(narrowDetail_&&!selected_.empty());}
    float DetailOpacity() const noexcept{return detailOpacity_;}
    float ListOpacity() const noexcept{return listOpacity_;}
    IDWriteTextLayout* RowTitleLayout(std::string_view id) const {
        const auto it=rowTitles_.find(std::string(id));return it==rowTitles_.end()?nullptr:it->second.Get();
    }
    float FilterPosition() const noexcept{return filterAnimation_.Position();}
    float FilterWeight(std::size_t index) const noexcept{return filterAnimation_.Weight(index);}
    std::vector<std::string> VisibleImages() const;
    std::optional<D2D1_RECT_F> ActionBounds(EventAction::Kind kind,std::string_view id) const;
    std::optional<D2D1_RECT_F> SourceNoteBounds() const;
    float SourceNoteFontSize() const;
    const std::vector<std::wstring>& OfficialText() const noexcept{return officialText_;}
    const std::vector<std::wstring>& EvidenceText() const noexcept{return evidenceText_;}
    std::wstring RefreshText() const;
    std::wstring LastRefreshText() const;
    std::wstring ContentText(std::string_view text) const;
private:
    struct Block {
        std::wstring text;float top{},height{};bool heading{},official{},footnote{};
        std::optional<EventAction> action;std::string imageId;
        Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    };
    void ApplyFilter();
    void RebindEntities();
    void BuildDetail(IDWriteFactory* factory,IDWriteTextFormat* body,IDWriteTextFormat* label);
    void BuildRowTitles(IDWriteFactory* factory,IDWriteTextFormat* label);
    void ClampScroll();
    std::optional<EventAction> ActionAt(float x,float y) const;
    std::optional<std::size_t> RowAt(float x,float y) const;
    std::optional<int> TabAt(float x,float y) const;
    TabBarLayout Tabs() const;
    D2D1_RECT_F BlockRect(const Block& block) const;
    std::optional<ScrollbarGeometry> Bar(bool detail) const;
    events::EventBrowser browser_;
    events::EventRefreshState refresh_;
    std::optional<events::Timestamp> refreshed_;
    const data::ItemCatalog* items_{};const data::TaskCatalog* tasks_{};const data::MapCatalog* maps_{};
    SearchBox search_;
    std::string locale_,selected_;
    std::vector<Block> blocks_;
    std::unordered_map<std::string,Microsoft::WRL::ComPtr<IDWriteTextLayout>> rowTitles_;
    std::vector<std::wstring> officialText_,evidenceText_;
    bool detailDirty_{true},titlesDirty_{true},narrow_{},narrowDetail_{},showOriginal_{};
    float titleWidth_{},listOpacity_{1};
    float detailHeight_{},detailWidth_{},listScroll_{},listTarget_{},detailScroll_{},detailTarget_{},detailOpacity_{1};
    TabSelectionAnimation filterAnimation_;
    D2D1_RECT_F searchRect_{},tabsRect_{},listRect_{},detailRect_{},refreshRect_{},listButton_{};
    std::optional<D2D1_POINT_2F> pressed_,hover_;
    std::optional<std::pair<bool,float>> grab_;
};
}
