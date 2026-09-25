#pragma once

#include "ui/PageHost.h"
#include "ui/Sidebar.h"

#include <d2d1.h>
#include <dwrite.h>
#include <windows.h>
#include <wrl/client.h>

#include <optional>
#include <string>

namespace noven::ui {

// 主窗口的绘制与导航状态；不拥有 OCR、扫描器或经济缓存。
// Main-window drawing and navigation state; owns no OCR, scanner, or economy cache.
class MainWindowUi final {
public:
    bool Initialize(HWND window, std::wstring& error);
    void Paint();
    void Resize(UINT width, UINT height);
    void DpiChanged(UINT dpi);
    void MouseMove(int x, int y);
    void MouseLeave();
    void MouseDown(int x, int y);
    [[nodiscard]] std::optional<data::GameMode> MouseUp(int x, int y);
    void SetScannerState(ScannerPageState state);
    [[nodiscard]] bool Ready() const noexcept { return window_ != nullptr; }
    [[nodiscard]] MainPage ActivePage() const noexcept { return navigation_.Active(); }

private:
    bool CreateRenderTarget(std::wstring& error);
    void Invalidate() const;
    [[nodiscard]] std::optional<MainPage> HitTest(int x, int y) const noexcept;
    [[nodiscard]] bool OnModeSelector(int x, int y) const noexcept;
    [[nodiscard]] std::optional<data::GameMode> ModeOptionAt(int x, int y) const noexcept;
    [[nodiscard]] float DipHeight() const noexcept;
    [[nodiscard]] float Scale() const noexcept { return static_cast<float>(dpi_) / 96.0F; }

    HWND window_{};
    UINT dpi_{96};
    NavigationState navigation_;
    ScannerPageState scanner_;
    UiTheme theme_;
    Sidebar sidebar_;
    PageHost pages_;
    std::optional<MainPage> hovered_;
    std::optional<MainPage> pressed_;
    bool mode_menu_open_{};
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
