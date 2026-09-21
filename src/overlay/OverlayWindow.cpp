#include "overlay/OverlayWindow.h"

#include "common/DebugLog.h"

#include <chrono>
#include <string>

namespace noven::overlay {

namespace {

constexpr wchar_t kOverlayClassName[] = L"NovenTarkovSupportOverlayWindow";

} // namespace

OverlayWindow::~OverlayWindow() {
    Destroy();
}

bool OverlayWindow::Create(HINSTANCE instance, std::wstring& error) {
    if (window_ != nullptr) {
        return true;
    }
    instance_ = instance;
    if (!renderer_.Initialize(error)) {
        return false;
    }

    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.hInstance = instance_;
    window_class.lpfnWndProc = &OverlayWindow::WindowProc;
    window_class.lpszClassName = kOverlayClassName;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground = nullptr;
    RegisterClassExW(&window_class);

    window_ = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE
            | WS_EX_LAYERED | WS_EX_TRANSPARENT,
        kOverlayClassName,
        L"Noven Tarkov Support Result",
        WS_POPUP,
        0,
        0,
        1,
        1,
        nullptr,
        nullptr,
        instance_,
        this
    );
    if (window_ == nullptr) {
        error = L"Could not create overlay window";
        renderer_.Shutdown();
        return false;
    }
    SetLayeredWindowAttributes(window_, 0, 245, LWA_ALPHA);
    ShowWindow(window_, SW_HIDE);
    common::DebugLog(L"[overlay] created hidden");
    return true;
}

void OverlayWindow::Destroy() noexcept {
    if (window_ != nullptr) {
        KillTimer(window_, kHideTimerId);
        DestroyWindow(window_);
        window_ = nullptr;
    }
    current_result_.reset();
    renderer_.Shutdown();
}

OverlayShowResult OverlayWindow::Show(const ScanDisplayResult& result, POINT anchor) {
    if (window_ == nullptr) {
        return OverlayShowResult{};
    }
    const auto update_start = std::chrono::steady_clock::now();
    current_result_ = result;
    const HMONITOR monitor = MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitor_info{};
    monitor_info.cbSize = sizeof(monitor_info);
    if (monitor == nullptr || !GetMonitorInfoW(monitor, &monitor_info)) {
        return OverlayShowResult{};
    }
    const SIZE measured = renderer_.Measure(result);
    const OverlayPlacement placement = CalculateOverlayPlacement(
        anchor.x,
        anchor.y,
        measured.cx,
        measured.cy,
        monitor_info.rcWork.left,
        monitor_info.rcWork.top,
        monitor_info.rcWork.right,
        monitor_info.rcWork.bottom
    );
    SetWindowPos(
        window_,
        HWND_TOPMOST,
        placement.left,
        placement.top,
        placement.width,
        placement.height,
        SWP_NOACTIVATE | SWP_SHOWWINDOW
    );
    const auto render_start = std::chrono::steady_clock::now();
    std::wstring error;
    if (!renderer_.Render(window_, result, error)) {
        common::DebugLog(L"[overlay] render failed: " + error);
        return OverlayShowResult{};
    }
    const double render_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - render_start).count();
    SetTimer(window_, kHideTimerId, kVisibleMilliseconds, nullptr);
    ShowWindow(window_, SW_SHOWNOACTIVATE);
    const double update_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - update_start).count();
    common::DebugLog(
        L"[overlay] shown anchor=(" + std::to_wstring(anchor.x) + L"," 
        + std::to_wstring(anchor.y) + L") size=" + std::to_wstring(measured.cx)
        + L"x" + std::to_wstring(measured.cy)
        + L" render_ms=" + std::to_wstring(render_ms)
        + L" update_ms=" + std::to_wstring(update_ms)
    );
    return OverlayShowResult{true, update_ms};
}

void OverlayWindow::Hide() noexcept {
    if (window_ == nullptr) {
        return;
    }
    KillTimer(window_, kHideTimerId);
    ShowWindow(window_, SW_HIDE);
    current_result_.reset();
}

LRESULT CALLBACK OverlayWindow::WindowProc(
    HWND window,
    UINT message,
    WPARAM w_param,
    LPARAM l_param
) {
    OverlayWindow* overlay = reinterpret_cast<OverlayWindow*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create_struct = reinterpret_cast<const CREATESTRUCTW*>(l_param);
        overlay = static_cast<OverlayWindow*>(create_struct->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(overlay));
    }
    if (overlay == nullptr) {
        return DefWindowProcW(window, message, w_param, l_param);
    }
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        BeginPaint(window, &paint);
        if (overlay->current_result_.has_value()) {
            std::wstring error;
            overlay->renderer_.Render(window, *overlay->current_result_, error);
        }
        EndPaint(window, &paint);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_TIMER:
        if (w_param == kHideTimerId) {
            overlay->Hide();
            return 0;
        }
        break;
    case WM_NCDESTROY:
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        break;
    default:
        break;
    }
    return DefWindowProcW(window, message, w_param, l_param);
}

} // namespace noven::overlay
