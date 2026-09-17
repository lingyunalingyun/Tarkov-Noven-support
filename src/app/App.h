#pragma once

#include <windows.h>

#include <memory>

namespace noven::capture {
class DxgiDesktopDuplicationBackend;
}

namespace noven::hotkey {
class GlobalHotkey;
}

namespace noven::scanner {
class ScanTrigger;
}

namespace noven {

class App final {
public:
    App();
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    int Run(HINSTANCE instance, int show_command);

private:
    static constexpr wchar_t kWindowClassName[] = L"NovenTarkovSupportWindow";

    static LRESULT CALLBACK WindowProc(
        HWND window,
        UINT message,
        WPARAM w_param,
        LPARAM l_param
    );

    void OnHotkey(WPARAM hotkey_id);
    bool RegisterWindowClass(HINSTANCE instance) const;
    HWND CreateMainWindow(HINSTANCE instance) const;

    HINSTANCE instance_{};
    HWND window_{};
    std::unique_ptr<capture::DxgiDesktopDuplicationBackend> capture_backend_;
    std::unique_ptr<hotkey::GlobalHotkey> hotkey_;
    std::unique_ptr<scanner::ScanTrigger> scan_trigger_;
};

} // namespace noven
