#pragma once
#include "data/AppSettings.h"

// Win32 UI 线程拥有主窗口、覆盖层和共享服务；工作线程结果通过 WM_APP 消息交接。
// The Win32 UI thread owns the main window, overlays, and shared services;
// worker results cross over through WM_APP messages.

#include <windows.h>
#include "common/AppPaths.h"

#include <cstdint>
#include <memory>
#include <thread>
#include <atomic>
#include <map>

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
enum class GameMode;
class ItemCatalog;
class ItemEconomyStore;
class DataRefreshService;
class RecentScanStore;
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

namespace noven::ui {
class MainWindowUi;
}
namespace noven::raid {
class LocalRaidService;
}
namespace noven::events {class EventService;class WinHttpEventClient;class OfficialEventSource;class WikiEventSource;}
namespace noven::plugins {class PluginDiscovery;class PluginRuntimeManager;class PluginRuntimeController;class MarketplaceService;}

namespace noven {

class App final {
public:
    App();
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    int Run(HINSTANCE instance, int show_command);

private:
    common::AppPaths paths_{common::AppPaths::Current()};
    static constexpr wchar_t kWindowClassName[] = L"NovenTarkovSupportWindow";
    // 消息附带堆上结果指针，投递失败由发送端释放，成功由接收端接管。
    // Messages carry heap-owned results; the sender frees failed posts,
    // while the receiver takes ownership of successful posts.
    static constexpr UINT kScanResultMessage = WM_APP + 1;
    static constexpr UINT kScanStepMessage = WM_APP + 2;
    static constexpr UINT kMapAssetsMessage = WM_APP + 5;
    static constexpr UINT kRaidHistoryMessage = WM_APP + 6;
    static constexpr UINT kEventsMessage = WM_APP + 7;
    static constexpr UINT kPluginRuntimeMessage = WM_APP + 8;
    static constexpr UINT kMarketplaceMessage = WM_APP + 9;
    static constexpr UINT kDebugMouseTimerId = 2;
    static constexpr UINT kDebugMouseCheckMilliseconds = 50;
    static constexpr UINT kRecentAnimationTimerId = 3;
    static constexpr UINT kRecentAnimationFrameMilliseconds = 16;
    // 可视化仅供验证；普通结果卡寿命和扫描匹配不由此开关决定。
    // Visualization is for validation; it does not control card lifetime or matching.
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
    void OnModeChanged(data::GameMode mode);
    bool ApplyPreferences(const data::AppSettings& next);
    data::AppSettings preferences_;
    void EnsureRecentAnimationTimer();
    void StartMapAssetUpdate();
    void PublishEvents();
    void RefreshPlugins();
    void PublishPluginRuntime();
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
    std::unique_ptr<ui::MainWindowUi> main_ui_;
    std::unique_ptr<plugins::PluginDiscovery> plugin_discovery_;
    std::unique_ptr<plugins::PluginRuntimeManager> plugin_runtime_;
    std::unique_ptr<plugins::PluginRuntimeController> plugin_controller_;
    std::unique_ptr<plugins::MarketplaceService> marketplace_;
    std::shared_ptr<std::atomic_bool> plugin_notification_pending_{std::make_shared<std::atomic_bool>(false)};
    std::map<std::string,std::pair<std::uint64_t,std::string>> plugin_logs_;
    bool plugin_state_warning_{};
    std::unique_ptr<data::RecentScanStore> recent_scan_store_;
    std::uint64_t recent_scan_id_base_{};
    HANDLE recent_animation_timer_{};
    bool scan_in_progress_{};
    bool recent_animation_timer_active_{};
    bool recent_animation_uses_waitable_timer_{};
    bool mouse_tracking_{};
    std::jthread map_asset_worker_;
    std::unique_ptr<raid::LocalRaidService> local_raid_service_;
    std::unique_ptr<events::WinHttpEventClient> event_http_;
    std::unique_ptr<events::OfficialEventSource> official_event_source_;
    std::unique_ptr<events::WikiEventSource> wiki_event_source_;
    std::unique_ptr<events::EventService> event_service_;
};

} // namespace noven
