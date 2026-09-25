#pragma once

#include "capture/CaptureTypes.h"

#include <windows.h>

#include <filesystem>
#include <string>

namespace noven::overlay {

class DebugVisualizationWindow final {
public:
    DebugVisualizationWindow() = default;
    ~DebugVisualizationWindow();

    DebugVisualizationWindow(const DebugVisualizationWindow&) = delete;
    DebugVisualizationWindow& operator=(const DebugVisualizationWindow&) = delete;

    bool Create(HINSTANCE instance, std::wstring& error);
    void Destroy() noexcept;

    void ShowRoi(const capture::Rect& roi);
    void HideRoi() noexcept;
    bool ShowSpatial(
        const std::filesystem::path& image_path,
        const capture::Rect& roi,
        POINT scan_anchor
    );
    void HideSpatial() noexcept;

    [[nodiscard]] bool ShouldHideSpatial(POINT current_cursor) const noexcept;
    [[nodiscard]] bool SpatialVisible() const noexcept { return spatial_visible_; }
    [[nodiscard]] bool RoiVisible() const noexcept {
        return roi_window_ != nullptr && IsWindowVisible(roi_window_) != FALSE;
    }

private:
    static constexpr UINT kMovementThresholdLogicalPixels = 8;

    static LRESULT CALLBACK WindowProc(
        HWND window,
        UINT message,
        WPARAM w_param,
        LPARAM l_param
    );

    void Paint(HWND window, HDC device_context) const;
    [[nodiscard]] UINT MovementThresholdPixels() const noexcept;

    HINSTANCE instance_{};
    HWND roi_window_{};
    HWND spatial_window_{};
    HBITMAP spatial_bitmap_{};
    LONG spatial_width_{};
    LONG spatial_height_{};
    POINT scan_anchor_{};
    bool spatial_visible_{};
};

} // namespace noven::overlay
