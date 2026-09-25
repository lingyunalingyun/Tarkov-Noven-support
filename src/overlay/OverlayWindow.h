#pragma once

// 常驻原生卡片窗口仅由 UI 线程更新；扫描线程通过完成消息交接显示模型。
// The persistent native card is updated only on the UI thread; the scan worker hands off a display model.

#include "overlay/OverlayRenderer.h"

#include <optional>
#include <windows.h>

namespace noven::overlay {

struct OverlayShowResult final {
    bool shown{};
    double update_render_ms{};
};

class OverlayWindow final {
public:
    OverlayWindow() = default;
    ~OverlayWindow();

    OverlayWindow(const OverlayWindow&) = delete;
    OverlayWindow& operator=(const OverlayWindow&) = delete;

    bool Create(HINSTANCE instance, std::wstring& error);
    void Destroy() noexcept;
    [[nodiscard]] OverlayShowResult Show(const ScanDisplayResult& result, POINT anchor);
    void Hide() noexcept;
    [[nodiscard]] bool Visible() const noexcept {
        return window_ != nullptr && IsWindowVisible(window_) != FALSE;
    }

    [[nodiscard]] HWND Handle() const noexcept { return window_; }

private:
    static constexpr UINT_PTR kHideTimerId = 1;
    // 自动隐藏只作用于结果卡；持久调试可视化有独立窗口和寿命。
    // Auto-hide applies only to the result card; persistent debug visualization has its own window.
    static constexpr UINT kVisibleMilliseconds = 4000;

    static LRESULT CALLBACK WindowProc(
        HWND window,
        UINT message,
        WPARAM w_param,
        LPARAM l_param
    );

    HINSTANCE instance_{};
    HWND window_{};
    OverlayRenderer renderer_;
    std::optional<ScanDisplayResult> current_result_;
};

} // namespace noven::overlay
