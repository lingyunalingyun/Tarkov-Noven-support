#include "overlay/DebugVisualizationWindow.h"

#include "common/DebugLog.h"

#include <algorithm>
#include <cmath>

namespace noven::overlay {

namespace {

constexpr wchar_t kDebugWindowClassName[] = L"NovenTarkovSupportDebugWindow";
constexpr BYTE kRoiAlpha = 72;
constexpr BYTE kSpatialAlpha = 190;

HWND CreateDebugWindow(
    HINSTANCE instance,
    DebugVisualizationWindow* owner,
    const wchar_t* title
) {
    return CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE
            | WS_EX_LAYERED | WS_EX_TRANSPARENT,
        kDebugWindowClassName,
        title,
        WS_POPUP,
        0,
        0,
        1,
        1,
        nullptr,
        nullptr,
        instance,
        owner
    );
}

} // namespace

DebugVisualizationWindow::~DebugVisualizationWindow() {
    Destroy();
}

bool DebugVisualizationWindow::Create(HINSTANCE instance, std::wstring& error) {
    if (roi_window_ != nullptr && spatial_window_ != nullptr) {
        return true;
    }
    instance_ = instance;

    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.hInstance = instance_;
    window_class.lpfnWndProc = &DebugVisualizationWindow::WindowProc;
    window_class.lpszClassName = kDebugWindowClassName;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground = nullptr;
    if (RegisterClassExW(&window_class) == 0
        && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        error = L"Could not register debug visualization window class";
        return false;
    }

    roi_window_ = CreateDebugWindow(instance_, this, L"Noven scan ROI");
    spatial_window_ = CreateDebugWindow(instance_, this, L"Noven spatial debug");
    if (roi_window_ == nullptr || spatial_window_ == nullptr) {
        error = L"Could not create debug visualization windows";
        Destroy();
        return false;
    }
    SetLayeredWindowAttributes(roi_window_, 0, kRoiAlpha, LWA_ALPHA);
    SetLayeredWindowAttributes(spatial_window_, 0, kSpatialAlpha, LWA_ALPHA);
    ShowWindow(roi_window_, SW_HIDE);
    ShowWindow(spatial_window_, SW_HIDE);
    common::DebugLog(L"[debug-visualization] created hidden ROI and spatial windows");
    return true;
}

void DebugVisualizationWindow::Destroy() noexcept {
    HideSpatial();
    if (roi_window_ != nullptr) {
        DestroyWindow(roi_window_);
        roi_window_ = nullptr;
    }
    if (spatial_window_ != nullptr) {
        DestroyWindow(spatial_window_);
        spatial_window_ = nullptr;
    }
}

void DebugVisualizationWindow::ShowRoi(const capture::Rect& roi) {
    if (roi_window_ == nullptr || roi.Empty()) {
        return;
    }
    SetWindowPos(
        roi_window_,
        HWND_TOPMOST,
        roi.left,
        roi.top,
        roi.Width(),
        roi.Height(),
        SWP_NOACTIVATE | SWP_SHOWWINDOW
    );
    InvalidateRect(roi_window_, nullptr, FALSE);
    ShowWindow(roi_window_, SW_SHOWNOACTIVATE);
}

bool DebugVisualizationWindow::ShowSpatial(
    const std::filesystem::path& image_path,
    const capture::Rect& roi,
    POINT scan_anchor
) {
    if (spatial_window_ == nullptr || roi.Empty()) {
        return false;
    }
    HBITMAP bitmap = static_cast<HBITMAP>(LoadImageW(
        nullptr,
        image_path.c_str(),
        IMAGE_BITMAP,
        0,
        0,
        LR_LOADFROMFILE | LR_CREATEDIBSECTION
    ));
    if (bitmap == nullptr) {
        common::DebugLog(
            L"[debug-visualization] spatial image load failed: " + image_path.wstring()
        );
        return false;
    }

    BITMAP bitmap_info{};
    if (GetObjectW(bitmap, sizeof(bitmap_info), &bitmap_info) == 0
        || bitmap_info.bmWidth <= 0 || bitmap_info.bmHeight <= 0) {
        DeleteObject(bitmap);
        common::DebugLog(L"[debug-visualization] spatial image has invalid dimensions");
        return false;
    }

    if (spatial_bitmap_ != nullptr) {
        DeleteObject(spatial_bitmap_);
    }
    spatial_bitmap_ = bitmap;
    spatial_width_ = bitmap_info.bmWidth;
    spatial_height_ = bitmap_info.bmHeight;
    scan_anchor_ = scan_anchor;
    spatial_visible_ = true;

    SetWindowPos(
        spatial_window_,
        HWND_TOPMOST,
        roi.left,
        roi.top,
        spatial_width_,
        spatial_height_,
        SWP_NOACTIVATE | SWP_SHOWWINDOW
    );
    InvalidateRect(spatial_window_, nullptr, FALSE);
    ShowWindow(spatial_window_, SW_SHOWNOACTIVATE);
    common::DebugLog(
        L"[debug-visualization] spatial shown anchor=("
        + std::to_wstring(scan_anchor.x) + L"," + std::to_wstring(scan_anchor.y)
        + L") size=" + std::to_wstring(spatial_width_) + L"x"
        + std::to_wstring(spatial_height_)
    );
    return true;
}

void DebugVisualizationWindow::HideSpatial() noexcept {
    spatial_visible_ = false;
    if (spatial_window_ != nullptr) {
        ShowWindow(spatial_window_, SW_HIDE);
    }
    if (spatial_bitmap_ != nullptr) {
        DeleteObject(spatial_bitmap_);
        spatial_bitmap_ = nullptr;
    }
    spatial_width_ = 0;
    spatial_height_ = 0;
}

bool DebugVisualizationWindow::ShouldHideSpatial(POINT current_cursor) const noexcept {
    if (!spatial_visible_) {
        return false;
    }
    const double delta_x = static_cast<double>(current_cursor.x - scan_anchor_.x);
    const double delta_y = static_cast<double>(current_cursor.y - scan_anchor_.y);
    const double distance = std::hypot(delta_x, delta_y);
    return distance > static_cast<double>(MovementThresholdPixels());
}

UINT DebugVisualizationWindow::MovementThresholdPixels() const noexcept {
    UINT dpi = spatial_window_ == nullptr ? 0 : GetDpiForWindow(spatial_window_);
    if (dpi == 0) {
        dpi = GetDpiForSystem();
    }
    if (dpi == 0) {
        dpi = USER_DEFAULT_SCREEN_DPI;
    }
    return std::max<UINT>(1, MulDiv(kMovementThresholdLogicalPixels, dpi, 96));
}

void DebugVisualizationWindow::Paint(HWND window, HDC device_context) const {
    RECT client_rect{};
    GetClientRect(window, &client_rect);
    if (window == roi_window_) {
        HBRUSH fill = CreateSolidBrush(RGB(20, 80, 120));
        FillRect(device_context, &client_rect, fill);
        DeleteObject(fill);
        HBRUSH border = CreateSolidBrush(RGB(80, 220, 255));
        FrameRect(device_context, &client_rect, border);
        InflateRect(&client_rect, -2, -2);
        FrameRect(device_context, &client_rect, border);
        DeleteObject(border);
        return;
    }
    if (window == spatial_window_ && spatial_bitmap_ != nullptr) {
        HDC bitmap_context = CreateCompatibleDC(device_context);
        if (bitmap_context == nullptr) {
            return;
        }
        HGDIOBJ previous = SelectObject(bitmap_context, spatial_bitmap_);
        BitBlt(
            device_context,
            0,
            0,
            spatial_width_,
            spatial_height_,
            bitmap_context,
            0,
            0,
            SRCCOPY
        );
        SelectObject(bitmap_context, previous);
        DeleteDC(bitmap_context);
    }
}

LRESULT CALLBACK DebugVisualizationWindow::WindowProc(
    HWND window,
    UINT message,
    WPARAM w_param,
    LPARAM l_param
) {
    auto* owner = reinterpret_cast<DebugVisualizationWindow*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create_struct = reinterpret_cast<const CREATESTRUCTW*>(l_param);
        owner = static_cast<DebugVisualizationWindow*>(create_struct->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(owner));
    }
    if (owner == nullptr) {
        return DefWindowProcW(window, message, w_param, l_param);
    }

    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC device_context = BeginPaint(window, &paint);
        owner->Paint(window, device_context);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_NCDESTROY:
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        break;
    default:
        break;
    }
    return DefWindowProcW(window, message, w_param, l_param);
}

} // namespace noven::overlay
