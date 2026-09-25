#pragma once

#include <windows.h>

#include <memory>

namespace noven::capture {
class DxgiDesktopDuplicationBackend;
}

namespace noven::hotkey {
class GlobalHotkey;
}

namespace noven::ocr {
class TextDetector;
class TextRecognizer;
}

namespace noven::data {
class ItemCatalog;
class ItemEconomyStore;
class DataRefreshService;
}

namespace noven::overlay {
class OverlayWindow;
class DebugVisualizationWindow;
struct ScanDisplayResult;
}

namespace noven::scanner {
class ScanTrigger;
struct ScanCompletion;
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
    static constexpr UINT kScanResultMessage = WM_APP + 1;
    static constexpr UINT kScanStepMessage = WM_APP + 2;
    static constexpr UINT kModeSelectorId = 1001;
    static constexpr UINT kDebugMouseTimerId = 2;
    static constexpr UINT kDebugMouseCheckMilliseconds = 50;
    static constexpr bool kDebugScanVisualization = true;

    static LRESULT CALLBACK WindowProc(
        HWND window,
        UINT message,
        WPARAM w_param,
        LPARAM l_param
    );

    void OnHotkey(WPARAM hotkey_id);
    void OnScanCompletionMessage(LPARAM completion_pointer);
    void OnScanStepMessage(LPARAM roi_pointer);
    void UpdateDebugRoi(POINT anchor);
    void CheckDebugVisualizationCursor();
    void OnModeChanged();
    bool CreateModeSelector();
    bool RegisterWindowClass(HINSTANCE instance) const;
    HWND CreateMainWindow(HINSTANCE instance) const;

    HINSTANCE instance_{};
    HWND window_{};
    std::unique_ptr<capture::DxgiDesktopDuplicationBackend> capture_backend_;
    std::unique_ptr<hotkey::GlobalHotkey> hotkey_;
    std::unique_ptr<ocr::TextDetector> text_detector_;
    std::unique_ptr<ocr::TextRecognizer> text_recognizer_;
    std::unique_ptr<data::ItemCatalog> item_catalog_;
    std::unique_ptr<data::ItemEconomyStore> item_economy_store_;
    std::unique_ptr<data::DataRefreshService> data_refresh_service_;
    std::unique_ptr<overlay::OverlayWindow> overlay_window_;
    std::unique_ptr<overlay::DebugVisualizationWindow> debug_visualization_window_;
    std::unique_ptr<scanner::ScanTrigger> scan_trigger_;
    bool scan_in_progress_{};
    HWND mode_selector_{};
};

} // namespace noven
