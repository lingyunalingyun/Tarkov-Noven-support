#include "app/App.h"

#include "capture/DxgiDesktopDuplicationBackend.h"
#include "capture/Roi.h"
#include "common/DebugLog.h"
#include "data/DataRefreshService.h"
#include "data/ItemCatalog.h"
#include "data/ItemEconomyStore.h"
#include "hotkey/GlobalHotkey.h"
#include "ocr/TextDetector.h"
#include "ocr/TextRecognizer.h"
#include "overlay/DebugVisualizationWindow.h"
#include "overlay/OverlayTypes.h"
#include "overlay/OverlayWindow.h"
#include "scanner/ProgressiveScan.h"
#include "scanner/ScanTrigger.h"

#include <commctrl.h>
#include <shellscalingapi.h>

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
          *item_economy_store_)) {}

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

    if (!CreateModeSelector()) {
        common::DebugLog(L"[app] mode selector creation failed");
        DestroyWindow(window_);
        window_ = nullptr;
        return 1;
    }

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
        DestroyWindow(window_);
        window_ = nullptr;
        return 1;
    }
    common::DebugLog(
        L"[catalog] loaded items=" + std::to_wstring(item_catalog_->ItemCount())
        + L" aliases=" + std::to_wstring(item_catalog_->AliasCount())
    );

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

    if (!capture_backend_->Initialize()) {
        common::DebugLog(L"[app] capture backend initialization failed");
        DestroyWindow(window_);
        window_ = nullptr;
        return 1;
    }

    scan_trigger_->SetCompletionCallback([this](scanner::ScanCompletion completion) {
        auto* completion_pointer = new scanner::ScanCompletion(std::move(completion));
        const bool posted = PostMessageW(
            window_,
            kScanResultMessage,
            0,
            reinterpret_cast<LPARAM>(completion_pointer)
        ) != FALSE;
        common::DebugLog(
            L"[scan-output] wmAppPosted="
            + std::wstring(posted ? L"true" : L"false")
        );
        if (!posted) {
            delete completion_pointer;
        }
    });
    scan_trigger_->SetScanStepCallback([this](capture::Rect roi) {
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
        if (kDebugScanVisualization && debug_visualization_window_ != nullptr) {
            POINT cursor{};
            if (GetCursorPos(&cursor)) {
                UpdateDebugRoi(cursor);
            }
            debug_visualization_window_->HideSpatial();
            KillTimer(window_, kDebugMouseTimerId);
        }
        scan_trigger_->Trigger();
    }
}

void App::UpdateDebugRoi(POINT anchor) {
    if (!kDebugScanVisualization || debug_visualization_window_ == nullptr) {
        return;
    }
    const capture::Point scan_anchor{anchor.x, anchor.y};
    const capture::Rect virtual_screen = VirtualScreenRect();
    const capture::Rect roi = scan_trigger_ != nullptr
        ? scanner::CalculateDirectionalRoi(
            scan_anchor,
            scanner::InventoryProfile().direction_priority.front(),
            scanner::DirectionalScanSizeForDepth(
                scanner::InventoryScanSizeForLevel(capture::Size{800, 600}, 0),
                0
            ),
            virtual_screen
        )
        : capture::CalculateRoi(scan_anchor, capture::Size{800, 600}, virtual_screen);
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

bool App::CreateModeSelector() {
    mode_selector_ = CreateWindowExW(
        0,
        WC_COMBOBOXW,
        L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
        20,
        20,
        180,
        180,
        window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kModeSelectorId)),
        instance_,
        nullptr
    );
    if (mode_selector_ == nullptr) {
        return false;
    }
    SendMessageW(mode_selector_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"PvP"));
    SendMessageW(mode_selector_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"PvE"));
    SendMessageW(mode_selector_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Seasonal"));
    SendMessageW(mode_selector_, CB_SETCURSEL, 0, 0);
    return true;
}

void App::OnModeChanged() {
    const LRESULT selection = SendMessageW(mode_selector_, CB_GETCURSEL, 0, 0);
    data::GameMode mode = data::GameMode::Pvp;
    if (selection == 1) {
        mode = data::GameMode::Pve;
    } else if (selection == 2) {
        mode = data::GameMode::Seasonal;
    }
    scan_trigger_->SetGameMode(mode);
    common::DebugLog(L"[economy] active mode=" + std::wstring(data::GameModeName(mode)));
}

void App::OnScanCompletionMessage(LPARAM completion_pointer) {
    std::unique_ptr<scanner::ScanCompletion> completion(
        reinterpret_cast<scanner::ScanCompletion*>(completion_pointer));
    if (!completion) {
        return;
    }
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
    LogValidation(completion->validation);
}

void App::OnScanStepMessage(LPARAM roi_pointer) {
    std::unique_ptr<capture::Rect> roi(
        reinterpret_cast<capture::Rect*>(roi_pointer));
    if (!roi || !kDebugScanVisualization || debug_visualization_window_ == nullptr) {
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
        case kScanResultMessage:
            app->OnScanCompletionMessage(l_param);
            return 0;
        case kScanStepMessage:
            app->OnScanStepMessage(l_param);
            return 0;
        case WM_COMMAND:
            if (LOWORD(w_param) == kModeSelectorId
                && HIWORD(w_param) == CBN_SELCHANGE) {
                app->OnModeChanged();
                return 0;
            }
            break;
        case WM_HOTKEY:
            app->OnHotkey(w_param);
            return 0;
        case WM_TIMER:
            if (w_param == kDebugMouseTimerId) {
                app->CheckDebugVisualizationCursor();
                return 0;
            }
            break;
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
        KillTimer(window, kDebugMouseTimerId);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, w_param, l_param);
}

} // namespace noven
