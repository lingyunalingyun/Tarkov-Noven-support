#pragma once
#include "raid/RaidHistoryBrowser.h"
#include "raid/RaidScanAssociation.h"
#include "data/MapCatalog.h"
#include "ui/SearchBox.h"
#include "ui/Scrollbar.h"
#include <unordered_map>

namespace noven::ui {
// 常驻筛选/选择/滚动状态；服务只注入快照，页面不读盘或解析日志。
// Resident filter/selection/scroll state; service injects snapshots, with no file/log parsing here.
class RaidHistoryPage final {
public:
    void SetSessions(std::vector<raid::RaidSession> sessions, std::optional<raid::RaidSession> active, bool unavailable);
    void SetScans(std::vector<data::RecentScanEntry> scans);
    void SetMaps(const data::MapCatalog& maps);
    void Prepare(float width, float height, const UiTheme& theme);
    void Draw(const UiCanvas& canvas, const UiTheme& theme,
        const std::unordered_map<std::string,Microsoft::WRL::ComPtr<ID2D1Bitmap>>& images) const;
    void MouseDown(float x,float y);
    std::optional<data::RecentScanEntry> MouseUp(float x,float y);
    bool MouseMove(float x,float y);
    bool Wheel(int delta,float x,float y);
    bool Key(WPARAM key,bool control);
    bool Char(wchar_t value);
    void CancelDrag() { grab_.reset(); pressed_.reset(); }
    void Blur() { search_.Blur(); menu_.reset(); CancelDrag(); }
    std::vector<std::string> VisibleImages() const;
    std::optional<D2D1_RECT_F> ScanBounds(std::uint64_t scanId) const;
    const raid::RaidHistoryBrowser& Browser() const noexcept { return browser_; }
    const std::string& SelectedId() const noexcept { return selected_; }
    bool Select(std::string id);
    float ListScroll() const noexcept { return listScroll_; }
private:
    void ApplyFilter();
    void RefreshScans();
    std::vector<std::wstring> Options(int control) const;
    std::size_t OptionIndex(int control) const;
    void Choose(int control,std::size_t option);
    std::optional<ScrollbarGeometry> Bar(bool detail) const;
    float ContentHeight(bool detail) const;
    raid::RaidHistoryBrowser browser_;
    std::vector<data::RecentScanEntry> scans_;
    raid::RaidScans linked_;
    std::optional<raid::RaidSession> active_;
    std::vector<data::MapRecord> maps_;
    std::string locale_,selected_;
    SearchBox search_;
    bool unavailable_{},compact_{},compactDetail_{};
    D2D1_RECT_F searchRect_{},listRect_{},detailRect_{};
    std::array<D2D1_RECT_F,4> controls_{};
    std::optional<int> menu_;
    std::optional<D2D1_POINT_2F> pressed_;
    std::optional<std::pair<bool,float>> grab_;
    float listScroll_{},detailScroll_{};
};
}
