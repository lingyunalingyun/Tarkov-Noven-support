#pragma once
#include "ui/UiCanvas.h"
#include "ui/Scrollbar.h"
#include "plugins/PluginDiscovery.h"
#include "plugins/PluginRuntimeManager.h"
#include "ui/TabBar.h"
#include "ui/SearchBox.h"
#include "ui/Dropdown.h"
#include <utility>

namespace noven::ui {
struct PluginPresentation final {std::wstring title,body,status;std::string id;std::filesystem::path directory;bool enable{},disable{},error{},incompatible{};};
enum class PluginCenterTab {Marketplace,MyPlugins};
struct PluginControlAction final {std::string id;bool enable{};};
std::wstring HostStateText(plugins::HostState state);
std::vector<PluginPresentation> PresentPlugins(const plugins::PluginSnapshot& snapshot);
std::wstring PluginStateText(plugins::PluginState state);
// 保护页面只产生显式启用/禁用请求；确认与授权由 Noven 拥有，市场不接网络。
// Protected page emits explicit enable/disable requests; Noven owns consent/grants, marketplace is offline.
class PluginsPage final {
public:
    void SetSnapshot(plugins::PluginSnapshot snapshot){CancelDrag();controlAction_.reset();snapshot_=std::move(snapshot);dirty_=true;}
    void SetRuntime(std::vector<plugins::HostSnapshot> runtime){
        bool changed=runtime.size()!=runtime_.size();
        for(std::size_t i=0;!changed&&i<runtime.size();++i)changed=runtime[i].pluginId!=runtime_[i].pluginId||runtime[i].generation!=runtime_[i].generation
            ||runtime[i].state!=runtime_[i].state||runtime[i].error!=runtime_[i].error||runtime[i].loadResult!=runtime_[i].loadResult;
        if(changed){CancelDrag();controlAction_.reset();dirty_=true;runtime_=std::move(runtime);}
    }
    std::optional<PluginControlAction> TakeControlAction(){return std::exchange(controlAction_,{});}
    PluginCenterTab Tab() const noexcept {return tab_;}
    void SelectTab(PluginCenterTab tab);
    void SetSearch(std::wstring text){search_.SetText(std::move(text));dirty_=true;scroll_=target_=listScroll_=listTarget_=0;CancelDrag();}
    bool Key(WPARAM key,bool control);
    bool Char(wchar_t character);
    D2D1_RECT_F SearchBounds() const noexcept {return searchBounds_;}
    D2D1_RECT_F ContentBounds() const noexcept {return viewport_;}
    D2D1_RECT_F ListBounds() const noexcept {return listViewport_;}
    D2D1_RECT_F CategoryBounds() const noexcept {return category_.header;}
    bool CategoryOpen() const noexcept {return categoryOpen_;}
    D2D1_RECT_F RowBounds(std::size_t index) const;
    const PluginPresentation* SelectedPlugin() const;
    bool TextInsideCards() const;
    std::wstring MarketplaceText() const;
    std::optional<D2D1_RECT_F> ControlBounds(std::string_view id) const;
    const plugins::PluginSnapshot& Snapshot() const noexcept {return snapshot_;}
    const std::vector<PluginPresentation>& Rows() const noexcept {return rows_;}
    std::wstring EmptyText() const;
    void Prepare(float width,float height,const UiTheme& theme,IDWriteFactory* factory,IDWriteTextFormat* body,IDWriteTextFormat* label);
    void Draw(const UiCanvas& canvas,const UiTheme& theme) const;
    void Down(float x,float y);
    bool Up(float x,float y);
    bool Move(float x,float y);
    bool Leave(){const bool changed=hovered_||hoveredTab_.has_value();hovered_=false;hoveredTab_.reset();return changed;}
    bool Wheel(int delta,float x,float y);
    void CancelDrag() noexcept {pressed_=false;grab_.reset();listGrab_.reset();pressedTab_.reset();pressedControl_.reset();pressedRow_.reset();categoryPress_.reset();}
    void Blur(){search_.Blur();scroll_=target_;listScroll_=listTarget_;tabProgress_=1;underline_=tab_==PluginCenterTab::Marketplace?0.0F:1.0F;categoryOpen_=false;CancelDrag();Leave();}
    bool Animating() const noexcept {return std::abs(scroll_-target_)>.01F||std::abs(listScroll_-listTarget_)>.01F||tabProgress_<1||(categoryOpen_&&(categoryClosing_||categoryProgress_<1));}
    void Tick(float elapsed);
    D2D1_RECT_F RefreshBounds() const noexcept {return refresh_;}
    float Scroll() const noexcept {return scroll_;}
private:
    struct Card {Microsoft::WRL::ComPtr<IDWriteTextLayout> title,body;float height{},titleHeight{};};
    std::optional<ScrollbarGeometry> Bar() const;
    std::optional<ScrollbarGeometry> ListBar() const;
    std::optional<std::size_t> SelectedIndex() const;
    std::optional<std::size_t> RowAt(float x,float y) const;
    void SelectRow(std::size_t index);
    void DrawCategory(const UiCanvas& canvas,const UiTheme& theme) const;
    TabBarLayout TabLayout() const {return {panel_.left,95,135,(std::min)(150.0F,(std::max)(40.0F,(searchBounds_.left-panel_.left-16)/2)),23};}
    std::array<TabBarItem<PluginCenterTab>,2> Tabs() const;
    std::optional<std::size_t> ControlAt(float x,float y) const;
    plugins::PluginSnapshot snapshot_;
    std::vector<PluginPresentation> rows_;
    std::vector<Card> cards_;
    std::string locale_;
    float width_{-1},content_{},scroll_{},target_{};
    D2D1_RECT_F viewport_{},refresh_{},panel_{},searchBounds_{},listViewport_{};
    SearchBox search_;
    // 清单尚无分类字段；分类栏仅提供全部，不从名称或权限推测分类。
    // Manifests have no category field yet; offer All without inferring categories from names or permissions.
    DropdownLayout category_{};
    bool categoryOpen_{},categoryClosing_{};
    float categoryProgress_{};
    std::optional<D2D1_POINT_2F> categoryPress_;
    // 使用目录保持选择，重复 ID 和损坏清单也必须可以独立查看。
    // Directory identity keeps duplicate IDs and broken manifests independently selectable.
    std::filesystem::path selectedDirectory_;
    std::optional<std::size_t> pressedRow_;
    float listScroll_{},listTarget_{};
    std::optional<float> listGrab_;
    bool dirty_{true},pressed_{},hovered_{};
    std::optional<float> grab_;
    std::vector<plugins::HostSnapshot> runtime_;
    PluginCenterTab tab_{PluginCenterTab::MyPlugins},outgoingTab_{PluginCenterTab::MyPlugins};
    float tabProgress_{1},underlineFrom_{1},underline_{1};
    std::optional<PluginCenterTab> pressedTab_,hoveredTab_;
    std::optional<std::size_t> pressedControl_;
    std::optional<PluginControlAction> controlAction_;
    std::wstring marketplaceLabel_,myLabel_;
};
}
