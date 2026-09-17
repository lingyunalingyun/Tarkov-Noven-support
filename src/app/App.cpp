#include "app/App.h"

#include "capture/DxgiDesktopDuplicationBackend.h"
#include "common/DebugLog.h"
#include "hotkey/GlobalHotkey.h"
#include "scanner/ScanTrigger.h"

#include <shellscalingapi.h>

#include <filesystem>
#include <memory>
#include <string>

namespace noven {

namespace {

constexpr int kCaptureHotkeyId = 1;
constexpr hotkey::HotkeyDefinition kCaptureHotkey{
    MOD_NOREPEAT,
    VK_F2,
};

std::filesystem::path ExecutableDirectory() {
    wchar_t path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, path, ARRAYSIZE(path));
    if (length == 0 || length == ARRAYSIZE(path)) {
        return std::filesystem::current_path();
    }
    return std::filesystem::path(std::wstring(path, length)).parent_path();
}

} // namespace

App::App()
    : capture_backend_(std::make_unique<capture::DxgiDesktopDuplicationBackend>()),
      hotkey_(std::make_unique<hotkey::GlobalHotkey>()),
      scan_trigger_(std::make_unique<scanner::ScanTrigger>(*capture_backend_)) {}

App::~App() = default;

bool App::RegisterWindowClass(HINSTANCE instance) const {
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.hInstance = instance;
    window_class.lpfnWndProc = &App::WindowProc;
    window_class.lpszClassName = kWindowClassName;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    return RegisterClassExW(&window_class) != 0;
}

HWND App::CreateMainWindow(HINSTANCE instance) const {
    return CreateWindowExW(
        0,
        kWindowClassName,
        L"Noven Tarkov Support",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        960,
        640,
        nullptr,
        nullptr,
        instance,
        const_cast<App*>(this)
    );
}

int App::Run(HINSTANCE instance, int show_command) {
    instance_ = instance;
    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
        common::DebugLog(
            L"[app] SetProcessDpiAwarenessContext failed (Win32 error="
            + std::to_wstring(GetLastError()) + L")"
        );
    }

    if (!RegisterWindowClass(instance)) {
        common::DebugLog(L"[app] RegisterClassExW failed");
        return 1;
    }

    window_ = CreateMainWindow(instance);
    if (window_ == nullptr) {
        common::DebugLog(L"[app] CreateWindowExW failed");
        return 1;
    }

    scan_trigger_->SetOutputDirectory(ExecutableDirectory() / L"debug-captures");
    scan_trigger_->SetRoiSize(capture::Size{800, 600});

    if (!capture_backend_->Initialize()) {
        common::DebugLog(L"[app] capture backend initialization failed");
        DestroyWindow(window_);
        window_ = nullptr;
        return 1;
    }

    if (!hotkey_->Register(window_, kCaptureHotkeyId, kCaptureHotkey)) {
        DestroyWindow(window_);
        window_ = nullptr;
        return 1;
    }

    ShowWindow(window_, show_command);
    UpdateWindow(window_);

    MSG message{};
    while (true) {
        const BOOL result = GetMessageW(&message, nullptr, 0, 0);
        if (result == -1) {
            common::DebugLog(L"[app] GetMessageW failed");
            return 1;
        }
        if (result == 0) {
            return static_cast<int>(message.wParam);
        }

        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

void App::OnHotkey(WPARAM hotkey_id) {
    if (hotkey_id == static_cast<WPARAM>(kCaptureHotkeyId)) {
        scan_trigger_->Trigger();
    }
}

LRESULT CALLBACK App::WindowProc(
    HWND window,
    UINT message,
    WPARAM w_param,
    LPARAM l_param
) {
    App* app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create_struct = reinterpret_cast<const CREATESTRUCTW*>(l_param);
        app = static_cast<App*>(create_struct->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }

    if (app != nullptr) {
        switch (message) {
        case WM_HOTKEY:
            app->OnHotkey(w_param);
            return 0;
        case WM_CLOSE:
            DestroyWindow(window);
            return 0;
        case WM_NCDESTROY:
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            break;
        default:
            break;
        }
    }

    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, w_param, l_param);
}

} // namespace noven
