#include "app/App.h"

// Win32 的 DrawText 宏不能改写随后包含的 Direct2D 接口方法名。
// Keep the Win32 DrawText macro from renaming Direct2D's DrawText method.
#ifdef DrawText
#undef DrawText
#endif

#include "capture/DxgiDesktopDuplicationBackend.h"
#include "capture/Roi.h"
#include "common/DebugLog.h"
#include "data/DataRefreshService.h"
#include "data/ItemCatalog.h"
#include "data/ItemEconomyStore.h"
#include "data/RecentScanStore.h"
#include "data/MapAssetUpdater.h"
#include "data/MapAssetComposer.h"
#include "ui/localization/LocalizationService.h"
#include "hotkey/GlobalHotkey.h"
#include "ocr/TextDetector.h"
#include "ocr/TextRecognizer.h"
#include "overlay/DebugVisualizationWindow.h"
#include "overlay/OverlayTypes.h"
#include "overlay/OverlayWindow.h"
#include "scanner/ScanTrigger.h"
#include "scanner/TooltipHeuristic.h"
#include "ui/MainWindowUi.h"
#include "ui/MessageDialog.h"
#include "raid/LocalRaidService.h"
#include "raid/RaidScanAssociation.h"
#include "plugins/PluginDiscovery.h"
#include "plugins/PluginRuntimeManager.h"
#include "plugins/PluginRuntimeController.h"
#include "events/OfficialEventSource.h"
#include "events/WikiEventSource.h"
#include "events/EventService.h"
#include "events/EventEnrichment.h"

#include <dwmapi.h>
#include <shellscalingapi.h>
#include <windowsx.h>

#include <filesystem>
#include <chrono>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <utility>

namespace noven {

namespace {

constexpr int kCaptureHotkeyId = 1;
constexpr hotkey::HotkeyDefinition kCaptureHotkey{
    MOD_NOREPEAT,
    VK_F2,
};

capture::Rect VirtualScreenRect() {
    const long left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const long top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    return capture::Rect{
        left,
        top,
        left + GetSystemMetrics(SM_CXVIRTUALSCREEN),
        top + GetSystemMetrics(SM_CYVIRTUALSCREEN),
    };
}

std::filesystem::path ExecutableDirectory() {
    wchar_t path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, path, ARRAYSIZE(path));
    if (length == 0 || length == ARRAYSIZE(path)) {
        return std::filesystem::current_path();
    }
    return std::filesystem::path(std::wstring(path, length)).parent_path();
}

std::wstring FormatMeasurement(double value) {
    std::wostringstream stream;
    stream << std::fixed << std::setprecision(2) << value;
    return stream.str();
}

std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(
        CP_UTF8,
        0,
        text.c_str(),
        static_cast<int>(text.size()),
        nullptr,
        0
    );
    if (size <= 0) {
        return L"<invalid UTF-8>";
    }
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(
        CP_UTF8,
        0,
        text.c_str(),
        static_cast<int>(text.size()),
        result.data(),
        size
    );
    return result;
}

std::wstring CurrentTimestamp() {
    SYSTEMTIME time{};
    GetLocalTime(&time);
    std::wostringstream stream;
    stream << std::setfill(L'0')
           << std::setw(4) << time.wYear << L'-'
           << std::setw(2) << time.wMonth << L'-'
           << std::setw(2) << time.wDay << L' '
           << std::setw(2) << time.wHour << L':'
           << std::setw(2) << time.wMinute << L':'
           << std::setw(2) << time.wSecond << L'.'
           << std::setw(3) << time.wMilliseconds;
    return stream.str();
}

void LogValidation(const scanner::ScanValidationRecord& record) {
    std::wostringstream summary;
    summary << L"[validation] timestamp=" << CurrentTimestamp()
            << L" scan_id=" << record.scan_id
            << L" classification=" << scanner::ScanClassificationName(record.classification)
            << L" game_mode=" << data::GameModeName(record.game_mode)
            << L" profile=" << scanner::ScanProfileName(record.profile)
            << L" anchor=(" << record.anchor.x << L"," << record.anchor.y << L")"
            << L" roi=(" << record.roi.left << L"," << record.roi.top << L")-("
            << record.roi.right << L"," << record.roi.bottom << L")"
            << L" detected_boxes=" << record.detected_box_count
            << L" recognized_boxes=" << record.recognized_box_count
            << L" catalog_valid_candidates=" << record.catalog_valid_candidate_count
            << L" selected_item_id="
            << (record.selected_item_id.empty()
                    ? L"n/a" : Utf8ToWide(record.selected_item_id))
            << L" selected_display_name="
            << (record.selected_display_name.empty()
                    ? L"n/a" : Utf8ToWide(record.selected_display_name))
            << L" selected_ocr_text="
            << (record.selected_ocr_text.empty()
                    ? L"n/a" : Utf8ToWide(record.selected_ocr_text))
            << L" ocr_confidence=" << FormatMeasurement(record.selected_ocr_confidence)
            << L" catalog_match_score=" << FormatMeasurement(record.selected_match_score)
            << L" direction="
            << (record.selected_item_id.empty()
                    ? L"n/a" : scanner::ScanDirectionName(record.selected_direction))
            << L" distance=" << FormatMeasurement(record.selected_distance)
            << L" economy_lookup_success="
            << (record.economy_lookup_succeeded ? L"true" : L"false")
            << L" overlay_show_success="
            << (record.overlay_show_succeeded ? L"true" : L"false")
            << L" total_hotkey_to_overlay_ms="
            << (record.overlay_show_succeeded
                    ? FormatMeasurement(record.total_hotkey_to_overlay_ms) : L"n/a");
    common::DebugLog(summary.str());

    std::wostringstream timings;
    timings << L"[validation] timing capture_ms=" << FormatMeasurement(record.capture_ms)
            << L" detector_preprocess_ms="
            << FormatMeasurement(record.detector_preprocess_ms)
            << L" detector_inference_ms="
            << FormatMeasurement(record.detector_inference_ms)
            << L" detector_postprocess_ms="
            << FormatMeasurement(record.detector_postprocess_ms)
            << L" recognition_preprocess_ms="
            << FormatMeasurement(record.recognition_preprocess_ms)
            << L" recognition_inference_ms="
            << FormatMeasurement(record.recognition_inference_ms)
            << L" recognition_decode_ms="
            << FormatMeasurement(record.recognition_decode_ms)
            << L" catalog_matching_ms="
            << FormatMeasurement(record.catalog_matching_ms)
            << L" spatial_selection_ms="
            << FormatMeasurement(record.spatial_selection_ms)
            << L" economy_lookup_ms="
            << FormatMeasurement(record.economy_lookup_ms)
            << L" overlay_update_render_ms="
            << FormatMeasurement(record.overlay_update_render_ms)
            << L" total_hotkey_to_overlay_ms="
            << (record.overlay_show_succeeded
                    ? FormatMeasurement(record.total_hotkey_to_overlay_ms) : L"n/a");
    common::DebugLog(timings.str());
    common::DebugLog(L"[validation] manual expected= actual= result=");
    if (!record.error.empty()) {
        common::DebugLog(L"[validation] error=" + record.error);
    }
}

} // namespace

App::App()
    : capture_backend_(std::make_unique<capture::DxgiDesktopDuplicationBackend>()),
      hotkey_(std::make_unique<hotkey::GlobalHotkey>()),
      text_detector_(std::make_unique<ocr::TextDetector>()),
      text_recognizer_(std::make_unique<ocr::TextRecognizer>()),
      item_catalog_(std::make_unique<data::ItemCatalog>()),
      item_economy_store_(std::make_unique<data::ItemEconomyStore>()),
      data_refresh_service_(std::make_unique<data::DataRefreshService>(
          *item_economy_store_)),
      overlay_window_(std::make_unique<overlay::OverlayWindow>()),
      debug_visualization_window_(
          std::make_unique<overlay::DebugVisualizationWindow>()),
      scan_trigger_(std::make_unique<scanner::ScanTrigger>(
          *capture_backend_,
          *text_detector_,
          *text_recognizer_,
          *item_catalog_,
          *item_economy_store_)),
      main_ui_(std::make_unique<ui::MainWindowUi>()),
      recent_scan_store_(std::make_unique<data::RecentScanStore>()) {}

App::~App() {
    plugin_controller_.reset();plugin_runtime_.reset();
    if (event_service_) event_service_->Stop();
    if (local_raid_service_) local_raid_service_->Stop();
    map_asset_worker_.request_stop();
    if(map_asset_worker_.joinable())map_asset_worker_.join();
    if (recent_animation_timer_ != nullptr) CloseHandle(recent_animation_timer_);
}

bool App::RegisterWindowClass(HINSTANCE instance) const {
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.hInstance = instance;
    window_class.lpfnWndProc = &App::WindowProc;
    window_class.lpszClassName = kWindowClassName;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground = nullptr;

    return RegisterClassExW(&window_class) != 0;
}

HWND App::CreateMainWindow(HINSTANCE instance) const {
    const UINT dpi = GetDpiForSystem();
    return CreateWindowExW(
        0,
        kWindowClassName,
        L"Noven Tarkov Support",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        MulDiv(1080, dpi, 96),
        MulDiv(750, dpi, 96),
        nullptr,
        nullptr,
        instance,
        const_cast<App*>(this)
    );
}

int App::Run(HINSTANCE instance, int show_command) {
    instance_ = instance;
    std::wstring locale_error;
    if (!ui::UiLocalization().DiscoverLocales(ExecutableDirectory() / L"assets" / L"i18n", locale_error)) {
        common::DebugLog(locale_error);
        (void)ui::ShowMessageDialog(nullptr,L"Noven - Localization",locale_error,ui::MessageKind::Error,L"OK");
        return 1;
    }
    for (const auto& warning : ui::UiLocalization().Warnings()) common::DebugLog(L"[i18n] " + warning);
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

    std::wstring ui_error;
    if (!main_ui_->Initialize(window_, ui_error)) {
        common::DebugLog(L"[app] main UI initialization failed: " + ui_error);
        DestroyWindow(window_);
        window_ = nullptr;
        return 1;
    }
    std::wstring history_error;
    preferences_=data::AppSettings::Load(ExecutableDirectory()/L"data"/L"settings.json").value_or(data::AppSettings{});
    main_ui_->SetPreferences(preferences_);
    main_ui_->SetPreferencesHandler([this](const auto& next){return ApplyPreferences(next);});
    plugin_discovery_=std::make_unique<plugins::PluginDiscovery>(ExecutableDirectory());
    // 新插件默认禁用；仅独立的授权控制器可启动 V2，发现和刷新不启动会话。
    // New plugins default disabled; only the consent controller starts V2, never discovery/refresh.
    plugin_runtime_=std::make_unique<plugins::PluginRuntimeManager>();
    plugin_controller_=std::make_unique<plugins::PluginRuntimeController>(*plugin_discovery_,*plugin_runtime_,ExecutableDirectory()/L"data"/L"plugin-state.json");
    plugin_runtime_->SetChangeHandler([window=window_,pending=plugin_notification_pending_]{
        if(!pending->exchange(true)&&!PostMessageW(window,kPluginRuntimeMessage,0,0))pending->store(false);
    });
    RefreshPlugins();
    main_ui_->SetPluginRefreshHandler([this]{RefreshPlugins();});
    main_ui_->SetPluginControlHandler([this](const ui::PluginControlAction& action){
        const auto result=action.enable?plugin_controller_->Enable(action.id,[this](const plugins::PluginManifest& manifest){
            std::wstring text=ui::Tr("plugins.consent_warning")+L"\n\n"+Utf8ToWide(manifest.name)+L"\n"+Utf8ToWide(manifest.id)
                +L"\n"+ui::Tr("plugins.version")+L": "+Utf8ToWide(manifest.version)+L"\n"+ui::Tr("plugins.author")+L": "+Utf8ToWide(manifest.author)
                +L"\n\n"+ui::Tr("plugins.permissions");
            if(manifest.requestedPermissions.empty())text+=L"\n"+ui::Tr("plugins.no_permissions");
            for(const auto& permission:manifest.requestedPermissions)text+=L"\n"+ui::PluginPermissionText(permission,plugins::SupportedPermission(permission));
            const auto network=ui::PluginNetworkText(manifest);if(!network.empty())text+=L"\n\n"+network;
            return ui::ShowMessageDialog(window_,ui::Tr("plugins.consent_title"),text,ui::MessageKind::Warning,
                ui::Tr("plugins.enable"),ui::Tr("dialog.cancel"));
        }):plugin_controller_->Disable(action.id);
        main_ui_->SetPlugins(plugin_discovery_->Snapshot());PublishPluginRuntime();
        if(result!=plugins::ControlResult::Success&&result!=plugins::ControlResult::ConsentDeclined)
            (void)ui::ShowMessageDialog(window_,ui::Tr("nav.plugins"),ui::Tr(result==plugins::ControlResult::StateFailure?"plugins.state_error":"plugins.enable_failed"),
                ui::MessageKind::Warning,ui::Tr("dialog.ok"));
    });
    main_ui_->SetPluginActionHandler([this](const ui::PluginOwnedPage& page,std::string_view action){plugin_runtime_->Action(page.pluginId,page.generation,page.page.localId,action);});
    if (!recent_scan_store_->Load(ExecutableDirectory() / L"data" / L"recent-scans.json",
                                  history_error)) {
        common::DebugLog(L"[recent-scans] load warning: " + history_error);
    }
    recent_scan_id_base_ = recent_scan_store_->MaxScanId();
    // 页面先接收本地快照；后台结果只投递通知，由 UI 线程查询服务，不解析来源。
    // Publish cached snapshots first; worker notifications let the UI thread query the service, never its sources.
    event_http_=std::make_unique<events::WinHttpEventClient>();
    official_event_source_=std::make_unique<events::OfficialEventSource>(*event_http_);
    wiki_event_source_=std::make_unique<events::WikiEventSource>(*event_http_);
    event_service_=std::make_unique<events::EventService>(*official_event_source_,
        events::MakeEventEnrichment(ExecutableDirectory()/L"assets",*event_http_),wiki_event_source_.get(),event_http_.get());
    event_service_->SetChangedCallback([this]{
        const auto state=event_service_->RefreshState();
        common::DebugLog(state.phase==events::RefreshPhase::Ready?L"[events] refresh ready":
            state.phase==events::RefreshPhase::Refreshing?L"[events] source snapshot ready; translation running":L"[events] refresh failed; cache preserved");
        PostMessageW(window_,kEventsMessage,0,0);
    });
    if(!event_service_->Start(ExecutableDirectory()/L"data"/L"events"/L"event-catalog.json"))
        common::DebugLog(L"[events] cache unavailable; original file preserved");
    PublishEvents();
    main_ui_->SetEventRefreshHandler([this]{
        if(!event_service_||!event_service_->RequestRefresh())return false;
        PublishEvents();return true;
    });
    main_ui_->StartItemImages(ExecutableDirectory() / L"data" / L"item-images");
    main_ui_->StartPriceHistory(ExecutableDirectory() / L"data" / L"price-history");
    main_ui_->SetRecentScans(recent_scan_store_->Snapshot());
    // 本地对局服务独立于 Scanner/UI；仅明确配置路径时启动后台读取。
    // Local raid service is independent of Scanner/UI; start background reads only for explicit configuration.
    const auto raidRoot=preferences_.gameDirectory.empty()?raid::ReadEftLogRoot(ExecutableDirectory()/L"data"/L"eft-log-root.txt")
        :data::GameLogRoot(preferences_.gameDirectory);
    local_raid_service_=std::make_unique<raid::LocalRaidService>();
    local_raid_service_->SetChangedCallback([window=window_]{PostMessageW(window,kRaidHistoryMessage,0,0);});
    if(!local_raid_service_->Start(raidRoot.value_or(std::filesystem::path{}),ExecutableDirectory()/L"data"/L"raid-history.json"))
        common::DebugLog(L"[local-raid] service could not start");
    main_ui_->SetRaidScanHandler([this]{return local_raid_service_&&local_raid_service_->RequestScan();});

    std::wstring overlay_error;
    if (!overlay_window_->Create(instance, overlay_error)) {
        common::DebugLog(L"[app] overlay initialization failed: " + overlay_error);
        DestroyWindow(window_);
        window_ = nullptr;
        return 1;
    }

    if (kDebugScanVisualization) {
        std::wstring debug_visualization_error;
        if (!debug_visualization_window_->Create(instance, debug_visualization_error)) {
            common::DebugLog(
                L"[app] debug visualization initialization failed: "
                + debug_visualization_error
            );
            DestroyWindow(window_);
            window_ = nullptr;
            return 1;
        }
        POINT cursor{};
        if (GetCursorPos(&cursor)) {
            UpdateDebugRoi(cursor);
        } else {
            common::DebugLog(L"[debug-visualization] initial GetCursorPos failed");
        }
    }

    scan_trigger_->SetOutputDirectory(ExecutableDirectory() / L"debug-captures");
    scan_trigger_->SetRoiSize(capture::Size{800, 600});

    const std::filesystem::path executable_directory = ExecutableDirectory();
    std::wstring catalog_error;
    if (!item_catalog_->Load(
        executable_directory / L"assets" / L"data" / L"items_catalog.tsv",
        catalog_error
    )) {
        common::DebugLog(L"[app] item catalog initialization failed: " + catalog_error);
        common::DebugLog(L"[app] continuing with OCR-only Inventory feedback");
    }
    common::DebugLog(
        L"[catalog] loaded items=" + std::to_wstring(item_catalog_->ItemCount())
        + L" aliases=" + std::to_wstring(item_catalog_->AliasCount())
        + L" english=" + std::to_wstring(item_catalog_->EnglishFieldCount())
        + L" chinese=" + std::to_wstring(item_catalog_->ChineseFieldCount())
        + L" source_version=" + Utf8ToWide(item_catalog_->SourceVersion())
        + L" generated_at=" + Utf8ToWide(item_catalog_->GeneratedAt())
    );
    main_ui_->SetPriceDataSources(*item_catalog_, *item_economy_store_);
    main_ui_->SetHideoutDataSources(executable_directory / L"assets" / L"data", *item_catalog_, *item_economy_store_);
    main_ui_->SetTaskDataSources(executable_directory / L"assets" / L"data", *item_catalog_);
    std::wstring map_error;
    if(!main_ui_->SetMapDataSources(executable_directory / L"assets",map_error)){
        common::DebugLog(L"[map] reference unavailable: "+map_error);
    }
    // 目录加载后再启动已授权插件；只复制静态投影，不让工作线程接触 UI 目录对象。
    // Start consented plugins only after catalogs load; copy static projections without worker access to UI catalog objects.
    plugin_runtime_->SetCatalogService(std::make_shared<plugins::CatalogPluginService>(item_catalog_->Items(),
        main_ui_->Tasks().Catalog().Tasks(),main_ui_->Map().Catalog(data::GameMode::Pvp).Maps(),ui::UiLocalization().ActiveLocale()));
    plugin_runtime_->PublishRaidHistory(local_raid_service_->CompletedSessions());
    plugin_runtime_->PublishEvents(event_service_->Events());
    plugin_runtime_->PublishRecentScans(recent_scan_store_->Snapshot());
    plugin_controller_->StartEnabled();PublishPluginRuntime();
    StartMapAssetUpdate();

    data_refresh_service_->Start(executable_directory / L"data" / L"economy-cache");

    std::wstring ocr_error;
    if (!text_detector_->Initialize(
        executable_directory / L"onnxruntime.dll",
        executable_directory / L"assets" / L"models" / L"ppocrv5_mobile_det.onnx",
        ocr_error
    )) {
        common::DebugLog(L"[app] OCR detector initialization failed: " + ocr_error);
        DestroyWindow(window_);
        window_ = nullptr;
        return 1;
    }
    if (!text_detector_->WarmUp(ocr_error)) {
        common::DebugLog(L"[app] OCR detector warm-up failed: " + ocr_error);
        DestroyWindow(window_);
        window_ = nullptr;
        return 1;
    }
    common::DebugLog(L"[ocr] detector warm-up completed");

    if (!text_recognizer_->Initialize(
        executable_directory / L"onnxruntime.dll",
        executable_directory / L"assets" / L"models" / L"ppocrv5_mobile_rec.onnx",
        executable_directory / L"assets" / L"models" / L"ppocrv5_mobile_rec_dict.txt",
        ocr_error
    )) {
        common::DebugLog(L"[app] OCR recognizer initialization failed: " + ocr_error);
        DestroyWindow(window_);
        window_ = nullptr;
        return 1;
    }
    if (!text_recognizer_->WarmUp(ocr_error)) {
        common::DebugLog(L"[app] OCR recognizer warm-up failed: " + ocr_error);
        DestroyWindow(window_);
        window_ = nullptr;
        return 1;
    }
    common::DebugLog(L"[ocr] recognizer warm-up completed");

    main_ui_->SetScannerState(ui::ScannerPageState{
        preferences_.mode, true, item_catalog_->ItemCount()});
    scan_trigger_->SetGameMode(preferences_.mode);

    if (!capture_backend_->Initialize()) {
        common::DebugLog(L"[app] capture backend initialization failed");
        DestroyWindow(window_);
        window_ = nullptr;
        return 1;
    }

    scan_trigger_->SetCompletionCallback([this](scanner::ScanCompletion completion) {
        // 工作线程只投递拥有所有权的结果；窗口消息处理器在 UI 线程更新覆盖层。
        // The worker only posts an owned result; the window message handler
        // updates the overlay on the UI thread.
        const std::uint64_t scan_id = completion.validation.scan_id;
        auto* completion_pointer = new scanner::ScanCompletion(std::move(completion));
        const bool posted = PostMessageW(
            window_,
            kScanResultMessage,
            0,
            reinterpret_cast<LPARAM>(completion_pointer)
        ) != FALSE;
        common::DebugLog(
            L"[scan:" + std::to_wstring(scan_id) + L"][display] wmAppPosted="
            + std::wstring(posted ? L"true" : L"false")
        );
        if (!posted) {
            delete completion_pointer;
        }
    });
    scan_trigger_->SetScanStepCallback([this](capture::Rect roi) {
        // 调试 ROI 的虚拟桌面矩形也经消息转交，避免跨线程直接改窗口。
        // Pass the virtual-screen debug ROI through a message too; do not mutate windows cross-thread.
        auto* roi_pointer = new capture::Rect(roi);
        if (!PostMessageW(
            window_,
            kScanStepMessage,
            0,
            reinterpret_cast<LPARAM>(roi_pointer)
        )) {
            delete roi_pointer;
        }
    });
    scan_trigger_->Start();

    if (!hotkey_->Register(window_, kCaptureHotkeyId, {preferences_.scanModifiers|MOD_NOREPEAT,preferences_.scanKey})) {
        common::DebugLog(L"[hotkey] saved shortcut unavailable; attempting default");
        if(hotkey_->Register(window_,kCaptureHotkeyId,kCaptureHotkey)){
            preferences_.scanKey=VK_F2;preferences_.scanModifiers=0;main_ui_->SetPreferences(preferences_);
        }
    }

    ShowWindow(window_, show_command);
    UpdateWindow(window_);

    recent_animation_timer_ = CreateWaitableTimerExW(nullptr, nullptr,
        CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (recent_animation_timer_ == nullptr)
        common::DebugLog(L"[recent-scans] high-resolution timer unavailable; using WM_TIMER");
    else
        common::DebugLog(L"[recent-scans] animation timer=high_resolution interval_ms="
            + std::to_wstring(kRecentAnimationFrameMilliseconds));

    MSG message{};
    auto previous_scroll_frame = std::chrono::steady_clock::time_point{};
    double scroll_frame_time_ms = 0.0;
    double slowest_scroll_frame_ms = 0.0;
    unsigned scroll_frame_count = 0;
    while (true) {
        if (recent_animation_timer_active_ && recent_animation_uses_waitable_timer_) {
            // 到期的帧先于连续滚轮消息处理，避免快速滚动时动画被输入队列饿死。
            // Give a due frame priority over a wheel-message burst so animation keeps moving.
            const DWORD ready = MsgWaitForMultipleObjectsEx(1, &recent_animation_timer_,
                0, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            if (ready != WAIT_OBJECT_0
                && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                if (message.message == WM_QUIT) return static_cast<int>(message.wParam);
                TranslateMessage(&message);
                DispatchMessageW(&message);
                continue;
            }
            // 动画帧与窗口消息共同等待；普通 WM_TIMER 容易被输入消息延后。
            // Wait for a frame or UI input together; ordinary WM_TIMER can lag behind input.
            const DWORD wait = ready == WAIT_OBJECT_0 ? ready
                : MsgWaitForMultipleObjectsEx(1, &recent_animation_timer_,
                    INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            if (wait == WAIT_OBJECT_0) {
                const auto frame_start = std::chrono::steady_clock::now();
                if (scroll_frame_count != 0) {
                    const double interval = std::chrono::duration<double, std::milli>(
                        frame_start - previous_scroll_frame).count();
                    scroll_frame_time_ms += interval;
                    if (interval > slowest_scroll_frame_ms) slowest_scroll_frame_ms = interval;
                }
                previous_scroll_frame = frame_start;
                ++scroll_frame_count;
                if (!main_ui_->AnimationTick()) {
                    CancelWaitableTimer(recent_animation_timer_);
                    recent_animation_timer_active_ = false;
                    recent_animation_uses_waitable_timer_ = false;
                    if (scroll_frame_count > 1 && scroll_frame_time_ms > 0.0)
                        common::DebugLog(L"[recent-scans] animation frames="
                            + std::to_wstring(scroll_frame_count)
                            + L" average_fps=" + FormatMeasurement(
                                (scroll_frame_count - 1) * 1000.0 / scroll_frame_time_ms)
                            + L" slowest_frame_ms=" + FormatMeasurement(slowest_scroll_frame_ms));
                    scroll_frame_count = 0;
                    scroll_frame_time_ms = 0.0;
                    slowest_scroll_frame_ms = 0.0;
                }
                UpdateWindow(window_);
            } else if (wait == WAIT_FAILED) {
                common::DebugLog(L"[recent-scans] waitable timer wait failed; using WM_TIMER");
                CancelWaitableTimer(recent_animation_timer_);
                recent_animation_uses_waitable_timer_ = false;
                recent_animation_timer_active_ = SetTimer(window_, kRecentAnimationTimerId,
                    kRecentAnimationFrameMilliseconds, nullptr) != 0;
            }
            continue;
        }
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
    if(main_ui_->RecordingShortcut()) {
        (void)main_ui_->KeyDown(preferences_.scanKey,(preferences_.scanModifiers&MOD_CONTROL)!=0);return;
    }
    if (hotkey_id == static_cast<WPARAM>(kCaptureHotkeyId)) {
        // 这里只隐藏 Noven 的覆盖层；若主窗口本身盖住 EFT，它仍可能进入桌面截图。
        // Only Noven overlays are hidden here. A main window covering EFT can
        // still appear in the desktop capture; this shell does not alter capture policy.
        const bool previous_overlay_visible = overlay_window_ != nullptr
            && overlay_window_->Visible();
        if (previous_overlay_visible) overlay_window_->Hide();
        bool debug_visible = false;
        if (kDebugScanVisualization && debug_visualization_window_ != nullptr) {
            debug_visible = debug_visualization_window_->RoiVisible()
                || debug_visualization_window_->SpatialVisible();
            debug_visualization_window_->HideRoi();
            debug_visualization_window_->HideSpatial();
            KillTimer(window_, kDebugMouseTimerId);
        }
        const bool hidden_for_capture = previous_overlay_visible || debug_visible;
        if (hidden_for_capture) DwmFlush();
        common::DebugLog(
            L"[overlay-capture-guard] previousOverlayVisible="
            + std::wstring(previous_overlay_visible ? L"true" : L"false")
            + L" hiddenForCapture="
            + (hidden_for_capture ? L"true" : L"false"));
        scan_in_progress_ = true;
        scan_trigger_->Trigger(hidden_for_capture);
    }
}

void App::UpdateDebugRoi(POINT anchor) {
    if (!kDebugScanVisualization || debug_visualization_window_ == nullptr) {
        return;
    }
    const capture::Point scan_anchor{anchor.x, anchor.y};
    capture::Rect monitor_bounds = VirtualScreenRect();
    MONITORINFO monitor_info{sizeof(MONITORINFO)};
    if (GetMonitorInfoW(MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST),
            &monitor_info)) {
        monitor_bounds = capture::Rect{
            monitor_info.rcMonitor.left, monitor_info.rcMonitor.top,
            monitor_info.rcMonitor.right, monitor_info.rcMonitor.bottom,
        };
    }
    const capture::Rect roi = scan_trigger_ != nullptr
        ? scanner::PredictTooltipPlacement(scan_anchor, monitor_bounds).roi
        : capture::CalculateRoi(scan_anchor, capture::Size{800, 600}, monitor_bounds);
    debug_visualization_window_->ShowRoi(roi);
}

void App::CheckDebugVisualizationCursor() {
    if (!kDebugScanVisualization || debug_visualization_window_ == nullptr
        || !debug_visualization_window_->SpatialVisible()) {
        KillTimer(window_, kDebugMouseTimerId);
        return;
    }
    POINT cursor{};
    if (GetCursorPos(&cursor)
        && debug_visualization_window_->ShouldHideSpatial(cursor)) {
        debug_visualization_window_->HideSpatial();
        KillTimer(window_, kDebugMouseTimerId);
        common::DebugLog(L"[debug-visualization] spatial hidden after cursor movement");
    }
}

void App::OnModeChanged(data::GameMode mode) {
    auto next=preferences_;next.mode=mode;
    if(!next.Save(ExecutableDirectory()/L"data"/L"settings.json"))common::DebugLog(L"[settings] could not save scanner mode");
    preferences_=next;main_ui_->SetPreferences(preferences_);
    scan_trigger_->SetGameMode(mode);
    main_ui_->SetScannerState(ui::ScannerPageState{
        mode, true, item_catalog_->ItemCount()});
    common::DebugLog(L"[economy] active mode=" + std::wstring(data::GameModeName(mode)));
}

bool App::ApplyPreferences(const data::AppSettings& next) {
    if(!next.Valid())return false;
    const bool directoryChanged=next.gameDirectory!=preferences_.gameDirectory;
    const auto root=data::GameLogRoot(next.gameDirectory);
    if(directoryChanged&&!root)return false;
    const bool shortcutChanged=next.scanKey!=preferences_.scanKey||next.scanModifiers!=preferences_.scanModifiers;
    const hotkey::HotkeyDefinition previous{preferences_.scanModifiers|MOD_NOREPEAT,preferences_.scanKey};
    if(shortcutChanged&&!hotkey_->Register(window_,kCaptureHotkeyId,{next.scanModifiers|MOD_NOREPEAT,next.scanKey})){
        hotkey_->Register(window_,kCaptureHotkeyId,previous);return false;
    }
    if(!next.Save(ExecutableDirectory()/L"data"/L"settings.json")){
        if(shortcutChanged)hotkey_->Register(window_,kCaptureHotkeyId,previous);return false;
    }
    preferences_=next;main_ui_->SetPreferences(preferences_);
    if(directoryChanged&&local_raid_service_)local_raid_service_->Start(*root,ExecutableDirectory()/L"data"/L"raid-history.json");
    return true;
}

void App::StartMapAssetUpdate(){
    if(map_asset_worker_.joinable())return;
    const auto assets=ExecutableDirectory()/L"assets";
    const auto cache=ExecutableDirectory()/L"data"/L"map-assets";
    data::MapAssetComposer existing(data::MapAssetStore(cache/L"sources",assets),cache/L"generations");
    if(const auto generation=existing.CurrentGeneration())main_ui_->SetMapAssetGeneration(*generation);
    const auto window=window_;
    // 启动仅检查一次；网络/重拼在后台，UI 线程只接收完整代路径，失败保留旧图。
    // Check once at startup; network/composition run in the worker, UI receives complete generations only and retains old imagery on failure.
    map_asset_worker_=std::jthread([assets,cache,window](std::stop_token stop){
        try{
            data::MapAssetStore sources(cache/L"sources",assets);
            data::MapAssetUpdater updater(sources);
            const auto report=updater.CheckOnce(assets/L"data"/L"map_update_assets.tsv",stop);
            common::DebugLog(L"[map-assets] updated="+std::to_wstring(report.updated)+L" unchanged="
                +std::to_wstring(report.unchanged)+L" failed="+std::to_wstring(report.failed));
            if(!report.error.empty())common::DebugLog(L"[map-assets] source check failed; retaining local imagery");
            data::MapAssetComposer composer(sources,cache/L"generations");
            const auto built=composer.Rebuild(assets/L"data"/L"map_compositions.tsv",report.updatedPaths,stop);
            if(!built.error.empty()){common::DebugLog(L"[map-assets] composition failed; retaining previous generation");return;}
            if(built.cancelled||stop.stop_requested()||built.rebuiltPaths.empty())return;
            auto generation=std::make_unique<std::filesystem::path>(built.generationRoot);
            if(PostMessageW(window,kMapAssetsMessage,0,reinterpret_cast<LPARAM>(generation.get())))generation.release();
        }catch(const std::exception&){common::DebugLog(L"[map-assets] startup check failed; local fallback retained");}
    });
}

void App::EnsureRecentAnimationTimer() {
    if (recent_animation_timer_active_ || !main_ui_->AnimationActive()) return;
    if (recent_animation_timer_ != nullptr) {
        LARGE_INTEGER due{};
        due.QuadPart = -static_cast<LONGLONG>(kRecentAnimationFrameMilliseconds) * 10000;
        recent_animation_uses_waitable_timer_ = SetWaitableTimerEx(
            recent_animation_timer_, &due, kRecentAnimationFrameMilliseconds,
            nullptr, nullptr, nullptr, 0) != FALSE;
        if (!recent_animation_uses_waitable_timer_)
            common::DebugLog(L"[recent-scans] timer arm failed; using WM_TIMER");
    }
    recent_animation_timer_active_ = recent_animation_uses_waitable_timer_
        || SetTimer(window_, kRecentAnimationTimerId,
            kRecentAnimationFrameMilliseconds, nullptr) != 0;
}

void App::PublishEvents() {
    if(event_service_&&main_ui_){
        auto snapshot=event_service_->Events();
        if(plugin_runtime_)plugin_runtime_->PublishEvents(snapshot);
        main_ui_->SetEvents(std::move(snapshot),event_service_->RefreshState(),event_service_->LastSuccessfulRefresh());
    }
}

void App::RefreshPlugins() {
    // 启动一次、手动刷新一次；只把元数据快照交给 UI，不向 PageRegistry 注册清单页面。
    // Startup/manual discovery only; publish metadata snapshots, never register manifest pages in PageRegistry.
    if(plugin_controller_)main_ui_->SetPlugins(plugin_controller_->Refresh());
    else if(plugin_discovery_)main_ui_->SetPlugins(plugin_discovery_->Refresh());
    PublishPluginRuntime();
}
void App::PublishPluginRuntime() {
    if(!plugin_runtime_||!main_ui_)return;
    if(plugin_controller_&&!plugin_controller_->Reconcile()&&!plugin_state_warning_){
        plugin_state_warning_=true;common::DebugLog(L"[plugins] "+ui::Tr("plugins.state_error"));
        (void)ui::ShowMessageDialog(window_,ui::Tr("nav.plugins"),ui::Tr("plugins.state_error"),ui::MessageKind::Warning,ui::Tr("dialog.ok"));
    }
    const auto snapshots=plugin_runtime_->Snapshots();main_ui_->SetPluginRuntime(snapshots);
    for(const auto& session:snapshots)if(!session.lastLog.empty()){
        const auto value=std::pair{session.generation,session.lastLog};
        if(plugin_logs_[session.pluginId]!=value){plugin_logs_[session.pluginId]=value;common::DebugLog(L"[plugin "+Utf8ToWide(session.pluginId)+L"] "+Utf8ToWide(session.lastLog));}
    }
    EnsureRecentAnimationTimer();
}

void App::OnScanCompletionMessage(LPARAM completion_pointer) {
    std::unique_ptr<scanner::ScanCompletion> completion(
        reinterpret_cast<scanner::ScanCompletion*>(completion_pointer));
    if (!completion) {
        return;
    }
    scan_in_progress_ = false;
    if (completion->display_result.has_value() && overlay_window_ != nullptr) {
        POINT cursor{};
        if (GetCursorPos(&cursor)) {
            const overlay::OverlayShowResult overlay_result = overlay_window_->Show(
                *completion->display_result,
                cursor
            );
            completion->validation.overlay_show_succeeded = overlay_result.shown;
            completion->validation.overlay_update_render_ms =
                overlay_result.update_render_ms;
            if (!overlay_result.shown) {
                completion->validation.classification = scanner::ScanClassification::OverlayFailed;
                completion->validation.error = L"OverlayWindow::Show failed";
            }
        } else {
            completion->validation.classification = scanner::ScanClassification::OverlayFailed;
            completion->validation.error = L"GetCursorPos failed before overlay";
        }
    }
    if (kDebugScanVisualization && debug_visualization_window_ != nullptr) {
        const POINT scan_anchor{
            completion->validation.anchor.x,
            completion->validation.anchor.y,
        };
        debug_visualization_window_->ShowRoi(completion->validation.roi);
        if (!completion->spatial_debug_image_path.empty()
            && debug_visualization_window_->ShowSpatial(
                completion->spatial_debug_image_path,
                completion->validation.roi,
                scan_anchor
            )) {
            SetTimer(window_, kDebugMouseTimerId, kDebugMouseCheckMilliseconds, nullptr);
            debug_visualization_window_->ShowRoi(completion->validation.roi);
        }
    }
    if (completion->validation.overlay_show_succeeded) {
        completion->validation.total_hotkey_to_overlay_ms =
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now()
                - completion->validation.hotkey_start
            ).count();
    }
    common::DebugLog(
        L"[scan-output] overlayShow="
        + std::wstring(completion->validation.overlay_show_succeeded
            ? L"true" : L"false")
    );
    common::DebugLog(
        L"[scan:" + std::to_wstring(completion->validation.scan_id)
        + L"][display-chain] finalItemSelected="
        + std::wstring(completion->validation.selected_item_id.empty()
            ? L"false" : L"true")
        + L" economyResolved="
        + (completion->validation.economy_lookup_succeeded ? L"true" : L"false")
        + L" displayResultBuilt="
        + (completion->display_result.has_value() ? L"true" : L"false")
        + L" wmAppPosted=true overlayShow="
        + (completion->validation.overlay_show_succeeded ? L"true" : L"false")
    );
    LogValidation(completion->validation);
    if (completion->validation.profile == scanner::ScanProfileType::Inventory
        && completion->display_result && !completion->display_result->itemId.empty()
        && completion->display_result->matchQuality != overlay::MatchQuality::OcrOnly) {
        const auto& result = *completion->display_result;
        data::RecentScanEntry entry;
        // 扫描器序号每次启动重置；基于已持久化最大 ID 生成跨启动唯一历史 ID。
        // Scanner sequence restarts each launch; offset it by the persisted maximum.
        entry.scanId = recent_scan_id_base_ + completion->validation.scan_id;
        entry.stableItemId = result.itemId;
        entry.canonicalName = result.displayName;
        if (const auto* item = item_catalog_->FindById(result.itemId)) {
            entry.canonicalShortName = !item->shortNameZh.empty()
                ? item->shortNameZh : item->shortNameEn;
        }
        entry.gameMode = result.mode;
        entry.matchMode = result.matchQuality == overlay::MatchQuality::Strict
            ? data::RecentMatchMode::Strict : data::RecentMatchMode::BestEffort;
        entry.ambiguous = result.bestEffortAmbiguous;
        entry.scannedAtUnixMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        entry.fleaPrice = result.fleaPrice;
        if (result.bestTrader) {
            entry.bestTraderPrice = result.bestTrader->priceRoubles;
            entry.bestTraderName = result.bestTrader->traderName;
        }
        entry.valuePerSlot = result.valuePerSlot;
        entry.fleaStatus = result.fleaStatus;
        entry.itemWidth = result.width;
        entry.itemHeight = result.height;
        raid::AssociateScan(entry, local_raid_service_ ? local_raid_service_->ActiveSession() : std::nullopt);
        if (recent_scan_store_->Append(std::move(entry))) {
            const auto scans = recent_scan_store_->Snapshot();
            main_ui_->SetRecentScans(scans);
            if (plugin_runtime_) {
                plugin_runtime_->PublishRecentScans(scans);
                // 同一份已追加结果，只入队通知；不让插件反向触发扫描或阻塞完成路径。
                // Notify only by enqueueing the same admitted result; plugins cannot trigger scans or block completion.
                plugin_runtime_->NotifyScanCompleted(scans.front());
            }
            common::DebugLog(L"[recent-scans] appended scan_id="
                + std::to_wstring(completion->validation.scan_id));
        }
    }
}

void App::OnScanStepMessage(LPARAM roi_pointer) {
    std::unique_ptr<capture::Rect> roi(
        reinterpret_cast<capture::Rect*>(roi_pointer));
    if (!roi || scan_in_progress_ || !kDebugScanVisualization
        || debug_visualization_window_ == nullptr) {
        return;
    }
    debug_visualization_window_->ShowRoi(*roi);
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
        case kPluginRuntimeMessage:
            app->plugin_notification_pending_->store(false);app->PublishPluginRuntime();return 0;
        case kEventsMessage:
            app->PublishEvents();app->EnsureRecentAnimationTimer();return 0;
        case kRaidHistoryMessage:
            if(app->local_raid_service_&&app->main_ui_){
                const auto status=app->local_raid_service_->Status();
                auto completed=app->local_raid_service_->CompletedSessions();
                if(app->plugin_runtime_)app->plugin_runtime_->PublishRaidHistory(completed);
                app->main_ui_->SetRaidSessions(std::move(completed),app->local_raid_service_->ActiveSession(),!status.error.empty());
                app->main_ui_->SetRaidScanStatus(status.manualScanPending,status.manualScans>0,!status.error.empty());
            }
            return 0;
        case kMapAssetsMessage: {
            const std::unique_ptr<std::filesystem::path> generation(reinterpret_cast<std::filesystem::path*>(l_param));
            if(generation&&app->main_ui_)app->main_ui_->SetMapAssetGeneration(*generation);
            return 0;
        }
        case kScanResultMessage:
            app->OnScanCompletionMessage(l_param);
            return 0;
        case kScanStepMessage:
            app->OnScanStepMessage(l_param);
            return 0;
        case WM_PAINT:
            if (app->main_ui_ != nullptr && app->main_ui_->Ready()) {
                app->main_ui_->Paint();
                return 0;
            }
            {
                PAINTSTRUCT paint{};
                BeginPaint(window, &paint);
                EndPaint(window, &paint);
                return 0;
            }
        case WM_ERASEBKGND:
            return 1;
        case WM_SIZE:
            if (app->main_ui_ != nullptr) {
                app->main_ui_->Resize(LOWORD(l_param), HIWORD(l_param));
            }
            return 0;
        case WM_DPICHANGED:
            if (app->main_ui_ != nullptr) {
                app->main_ui_->DpiChanged(HIWORD(w_param));
                const auto* suggested = reinterpret_cast<const RECT*>(l_param);
                SetWindowPos(window, nullptr, suggested->left, suggested->top,
                             suggested->right - suggested->left,
                             suggested->bottom - suggested->top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
            }
            return 0;
        case WM_GETMINMAXINFO: {
            auto* limits = reinterpret_cast<MINMAXINFO*>(l_param);
            const UINT dpi = GetDpiForWindow(window);
            limits->ptMinTrackSize.x = MulDiv(900, dpi, 96);
            limits->ptMinTrackSize.y = MulDiv(750, dpi, 96);
            return 0;
        }
        case ui::ItemImageCache::kReadyMessage:
            if (app->main_ui_ != nullptr) app->main_ui_->ItemImagesReady();
            return 0;
        case data::PriceHistoryService::kReadyMessage:
            if (app->main_ui_ != nullptr) {
                app->main_ui_->PriceHistoryReady();
                app->EnsureRecentAnimationTimer();
            }
            return 0;
        case WM_MOUSEMOVE:
            if (app->main_ui_ != nullptr) {
                app->main_ui_->MouseMove(GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param));
                app->EnsureRecentAnimationTimer();
                if (!app->mouse_tracking_) {
                    TRACKMOUSEEVENT tracking{sizeof(TRACKMOUSEEVENT), TME_LEAVE, window, 0};
                    if (TrackMouseEvent(&tracking)) app->mouse_tracking_ = true;
                }
            }
            return 0;
        case WM_MOUSELEAVE:
            app->mouse_tracking_ = false;
            app->main_ui_->MouseLeave();
            return 0;
        case WM_LBUTTONDOWN:
            app->main_ui_->MouseDown(GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param));
            SetCapture(window);
            return 0;
        case WM_XBUTTONUP:
            if (GET_XBUTTON_WPARAM(w_param)==XBUTTON1 && app->main_ui_->GoBack()) {
                app->EnsureRecentAnimationTimer();
                return TRUE;
            }
            break;
        case WM_LBUTTONUP: {
            const auto previous_locale = ui::UiLocalization().ActiveLocale();
            const auto mode = app->main_ui_->MouseUp(
                GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param));
            if (previous_locale != ui::UiLocalization().ActiveLocale()) {
                app->plugin_runtime_->SetCatalogLocale(ui::UiLocalization().ActiveLocale());
                if(app->overlay_window_->Visible())InvalidateRect(app->overlay_window_->Handle(), nullptr, FALSE);
            }
            if (GetCapture() == window) ReleaseCapture();
            if (mode) app->OnModeChanged(*mode);
            app->EnsureRecentAnimationTimer();
            return 0;
        }
        case WM_CAPTURECHANGED:
            if (app->main_ui_ != nullptr) app->main_ui_->CancelScrollDrag();
            return 0;
        case WM_MOUSEWHEEL: {
            POINT point{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
            ScreenToClient(window, &point);
            if (app->main_ui_->MouseWheel(point.x, point.y,
                    GET_WHEEL_DELTA_WPARAM(w_param), (GET_KEYSTATE_WPARAM(w_param)&MK_CONTROL)!=0)) app->EnsureRecentAnimationTimer();
            return 0;
        }
        case WM_SYSKEYDOWN:
            if(!app->main_ui_->RecordingShortcut())break;
            [[fallthrough]];
        case WM_KEYDOWN:
            if (app->main_ui_ != nullptr && app->main_ui_->KeyDown(
                    w_param, (GetKeyState(VK_CONTROL) & 0x8000) != 0)) {
                app->EnsureRecentAnimationTimer();
                return 0;
            }
            break;
        case WM_IME_STARTCOMPOSITION:
            // 开始组合前先更新自绘搜索光标的位置，候选窗口仍由系统管理。
            // Update the custom search anchor before the system opens its IME windows.
            InvalidateRect(window, nullptr, FALSE);
            UpdateWindow(window);
            break;
        case WM_CHAR:
            if (app->main_ui_ != nullptr && app->main_ui_->Char(static_cast<wchar_t>(w_param))) {
                app->EnsureRecentAnimationTimer();
                return 0;
            }
            break;
        case WM_HOTKEY:
            app->OnHotkey(w_param);
            return 0;
        case WM_TIMER:
            if(w_param==ui::MainWindowUi::EventClockTimerId){app->main_ui_->EventClockTick();app->EnsureRecentAnimationTimer();return 0;}
            if(w_param==ui::MainWindowUi::MapClockTimerId){app->main_ui_->MapClockTick();return 0;}
            if (w_param == kRecentAnimationTimerId) {
                if (!app->main_ui_->AnimationTick()) {
                    KillTimer(window, kRecentAnimationTimerId);
                    app->recent_animation_timer_active_ = false;
                }
                return 0;
            }
            if (w_param == kDebugMouseTimerId) {
                app->CheckDebugVisualizationCursor();
                return 0;
            }
            break;
        case WM_CLOSE:
            // 先收束线程与自有 Job，再销毁通知目标；普通退出保留已授权运行意图。
            // Join owned workers/Jobs before destroying the notification target; normal exit preserves approved intent.
            app->plugin_controller_.reset();app->plugin_runtime_.reset();
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
        if(app){
            app->map_asset_worker_.request_stop();
            if(app->map_asset_worker_.joinable())app->map_asset_worker_.join();
            MSG pending{};
            while(PeekMessageW(&pending,window,kMapAssetsMessage,kMapAssetsMessage,PM_REMOVE))
                delete reinterpret_cast<std::filesystem::path*>(pending.lParam);
        }
        KillTimer(window,ui::MainWindowUi::MapClockTimerId);
        KillTimer(window,ui::MainWindowUi::EventClockTimerId);
        if (app != nullptr && app->main_ui_ != nullptr) {
            app->main_ui_->StopPriceHistory();
            app->main_ui_->StopItemImages();
        }
        KillTimer(window, kDebugMouseTimerId);
        KillTimer(window, kRecentAnimationTimerId);
        if (app != nullptr && app->recent_animation_timer_ != nullptr)
            CancelWaitableTimer(app->recent_animation_timer_);
        if (app != nullptr) app->recent_animation_timer_active_ = false;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, w_param, l_param);
}

} // namespace noven
