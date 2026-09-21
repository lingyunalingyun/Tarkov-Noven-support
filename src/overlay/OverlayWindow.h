#pragma once

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

    [[nodiscard]] HWND Handle() const noexcept { return window_; }

private:
    static constexpr UINT_PTR kHideTimerId = 1;
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
