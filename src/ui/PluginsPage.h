#pragma once
#include "ui/UiCanvas.h"
#include "ui/Scrollbar.h"
#include "plugins/PluginDiscovery.h"

namespace noven::ui {
struct PluginPresentation final {std::wstring title,body,status;};
std::vector<PluginPresentation> PresentPlugins(const plugins::PluginSnapshot& snapshot);
std::wstring PluginStateText(plugins::PluginState state);
// 页面只接收服务快照；唯一动作是请求刷新，没有执行/安装/授权动作类型。
// Consume service snapshots only; refresh is the sole action, with no execute/install/grant action type.
class PluginsPage final {
public:
    void SetSnapshot(plugins::PluginSnapshot snapshot){snapshot_=std::move(snapshot);dirty_=true;}
    const plugins::PluginSnapshot& Snapshot() const noexcept {return snapshot_;}
    const std::vector<PluginPresentation>& Rows() const noexcept {return rows_;}
    std::wstring EmptyText() const;
    void Prepare(float width,float height,const UiTheme& theme,IDWriteFactory* factory,IDWriteTextFormat* body,IDWriteTextFormat* label);
    void Draw(const UiCanvas& canvas,const UiTheme& theme) const;
    void Down(float x,float y);
    bool Up(float x,float y);
    bool Move(float x,float y);
    bool Leave(){const bool changed=hovered_;hovered_=false;return changed;}
    bool Wheel(int delta,float x,float y);
    void Blur(){scroll_=target_;pressed_=false;grab_.reset();hovered_=false;}
    bool Animating() const noexcept {return std::abs(scroll_-target_)>.01F;}
    void Tick(float elapsed);
    D2D1_RECT_F RefreshBounds() const noexcept {return refresh_;}
    float Scroll() const noexcept {return scroll_;}
private:
    struct Card {Microsoft::WRL::ComPtr<IDWriteTextLayout> title,body;float top{},height{},titleHeight{};};
    std::optional<ScrollbarGeometry> Bar() const;
    plugins::PluginSnapshot snapshot_;
    std::vector<PluginPresentation> rows_;
    std::vector<Card> cards_;
    std::string locale_;
    float width_{-1},content_{},scroll_{},target_{};
    D2D1_RECT_F viewport_{},refresh_{};
    bool dirty_{true},pressed_{},hovered_{};
    std::optional<float> grab_;
};
}
