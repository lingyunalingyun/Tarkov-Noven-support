#include "scanner/ScanTrigger.h"

#include "capture/Roi.h"
#include "common/DebugLog.h"
#include "scanner/AdaptiveTextExpansion.h"
#include "scanner/DebugImageWriter.h"
#include "scanner/InventoryRecognition.h"
#include "ocr/DetectionDebugWriter.h"
#include "scanner/LocalTextGrouping.h"
#include "scanner/ProgressiveScan.h"
#include "scanner/SpatialDebugWriter.h"
#include "scanner/TooltipHeuristic.h"

#include <windows.h>
#include <dwmapi.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>

namespace noven::scanner {

namespace {

double ElapsedMilliseconds(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start
    ).count();
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

const wchar_t* CaptureSourceName(capture::CaptureSource source) {
    switch (source) {
    case capture::CaptureSource::DxgiNewFrame:
        return L"DXGI_NEW_FRAME";
    case capture::CaptureSource::DxgiCachedFrame:
        return L"DXGI_CACHED_FRAME";
    case capture::CaptureSource::GdiEmergency:
        return L"GDI_EMERGENCY";
    }
    return L"UNKNOWN";
}

bool SameCandidate(const ScanCandidate& left, const ScanCandidate& right) {
    return left.match.item == right.match.item
        && left.recognized.text == right.recognized.text
        && left.recognized.box.x1 == right.recognized.box.x1
        && left.recognized.box.y1 == right.recognized.box.y1
        && left.recognized.box.x2 == right.recognized.box.x2
        && left.recognized.box.y2 == right.recognized.box.y2;
}

std::wstring BoxText(const ocr::TextBox& box) {
    return L"(" + FormatMeasurement(box.x1) + L"," + FormatMeasurement(box.y1)
        + L")-(" + FormatMeasurement(box.x2) + L"," + FormatMeasurement(box.y2) + L")";
}

std::wstring IndexList(std::span<const std::size_t> indices) {
    std::wstring result = L"[";
    for (std::size_t index = 0; index < indices.size(); ++index) {
        if (index != 0) {
            result += L",";
        }
        result += std::to_wstring(indices[index]);
    }
    result += L"]";
    return result;
}

capture::CapturedFrame CropFrame(
    const capture::CapturedFrame& source,
    const ocr::TextBox& bounds,
    long padding,
    long& crop_left,
    long& crop_top
) {
    crop_left = std::max(0L, static_cast<long>(std::floor(bounds.x1)) - padding);
    crop_top = std::max(0L, static_cast<long>(std::floor(bounds.y1)) - padding);
    const long crop_right = std::min(
        static_cast<long>(source.width),
        static_cast<long>(std::ceil(bounds.x2)) + padding
    );
    const long crop_bottom = std::min(
        static_cast<long>(source.height),
        static_cast<long>(std::ceil(bounds.y2)) + padding
    );
    capture::CapturedFrame result;
    if (crop_right <= crop_left || crop_bottom <= crop_top) {
        return result;
    }
    result.width = static_cast<std::uint32_t>(crop_right - crop_left);
    result.height = static_cast<std::uint32_t>(crop_bottom - crop_top);
    result.stride = result.width * 4;
    result.pixel_format = source.pixel_format;
    result.bgra.resize(static_cast<std::size_t>(result.stride) * result.height);
    for (long row = crop_top; row < crop_bottom; ++row) {
        const std::size_t source_offset = static_cast<std::size_t>(row)
            * source.stride + static_cast<std::size_t>(crop_left) * 4;
        const std::size_t destination_offset = static_cast<std::size_t>(row - crop_top)
            * result.stride;
        const std::size_t bytes = static_cast<std::size_t>(result.stride);
        if (source_offset + bytes > source.bgra.size()
            || destination_offset + bytes > result.bgra.size()) {
            result.bgra.clear();
            result.width = 0;
            result.height = 0;
            result.stride = 0;
            return result;
        }
        std::memcpy(
            result.bgra.data() + destination_offset,
            source.bgra.data() + source_offset,
            bytes
        );
    }
    return result;
}

} // namespace

ScanTrigger::ScanTrigger(
    capture::ICaptureBackend& capture_backend,
    ocr::TextDetector& text_detector,
    ocr::TextRecognizer& text_recognizer,
    data::ItemCatalog& item_catalog,
    data::ItemEconomyStore& economy_store
)
    : capture_backend_(capture_backend),
      text_detector_(text_detector),
      text_recognizer_(text_recognizer),
      item_catalog_(item_catalog),
      economy_store_(economy_store),
      output_directory_(std::filesystem::current_path() / L"debug-captures") {}

ScanTrigger::~ScanTrigger() {
    {
        std::lock_guard lock(jobs_mutex_);
        stopping_ = true;
    }
    jobs_available_.notify_one();
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
}

void ScanTrigger::SetOutputDirectory(std::filesystem::path output_directory) {
    output_directory_ = std::move(output_directory);
}

void ScanTrigger::SetRoiSize(capture::Size roi_size) {
    roi_size_ = roi_size;
}

void ScanTrigger::SetProfile(ScanProfileType profile) {
    profile_ = profile;
}

void ScanTrigger::SetGameMode(data::GameMode mode) {
    game_mode_ = mode;
}

void ScanTrigger::SetCompletionCallback(
    std::function<void(ScanCompletion)> callback
) {
    completion_callback_ = std::move(callback);
}

void ScanTrigger::SetScanStepCallback(std::function<void(capture::Rect)> callback) {
    scan_step_callback_ = std::move(callback);
}

void ScanTrigger::Start() {
    if (started_) {
        return;
    }
    started_ = true;
    worker_thread_ = std::thread(&ScanTrigger::WorkerLoop, this);
}

void ScanTrigger::Trigger(bool previous_overlay_visible) {
    const auto total_start = std::chrono::steady_clock::now();
    const std::uint64_t scan_id = ++next_scan_id_;
    const ScannerProfile profile = ProfileFor(profile_);

    const auto notify_capture_failure = [&](capture::Point anchor,
                                             capture::Rect roi,
                                             std::wstring error,
                                             double capture_ms) {
        if (!completion_callback_) {
            return;
        }
        ScanValidationRecord validation;
        validation.scan_id = scan_id;
        validation.hotkey_start = total_start;
        validation.game_mode = game_mode_;
        validation.profile = profile.type;
        validation.anchor = anchor;
        validation.roi = roi;
        validation.classification = ScanClassification::CaptureFailed;
        validation.error = std::move(error);
        validation.capture_ms = capture_ms;
         completion_callback_(ScanCompletion{std::nullopt, std::move(validation), {}});
    };

    POINT cursor_position{};
    if (!GetCursorPos(&cursor_position)) {
        const std::wstring error = L"GetCursorPos failed (Win32 error="
            + std::to_wstring(GetLastError()) + L")";
        common::DebugLog(
            L"[capture] " + error
        );
        notify_capture_failure(capture::Point{}, capture::Rect{}, error, 0.0);
        return;
    }

    const capture::Point cursor{
        static_cast<long>(cursor_position.x),
        static_cast<long>(cursor_position.y),
    };
    const capture::Rect virtual_screen{
        GetSystemMetrics(SM_XVIRTUALSCREEN),
        GetSystemMetrics(SM_YVIRTUALSCREEN),
        GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN),
        GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN),
    };
    const capture::Point screen_anchor = profile.type == ScanProfileType::RaidPickup
        ? capture::Point{
            virtual_screen.left + virtual_screen.Width() / 2,
            virtual_screen.top + virtual_screen.Height() / 2,
        }
        : cursor;
    capture::Rect monitor_bounds = virtual_screen;
    MONITORINFO monitor_info{sizeof(MONITORINFO)};
    if (profile.type == ScanProfileType::Inventory
        && GetMonitorInfoW(MonitorFromPoint(cursor_position, MONITOR_DEFAULTTONEAREST),
            &monitor_info)) {
        monitor_bounds = capture::Rect{
            monitor_info.rcMonitor.left, monitor_info.rcMonitor.top,
            monitor_info.rcMonitor.right, monitor_info.rcMonitor.bottom,
        };
    }
    const TooltipPlacementPrediction placement = PredictTooltipPlacement(
        screen_anchor, monitor_bounds);
    const capture::Rect roi = profile.type == ScanProfileType::Inventory
        ? placement.roi
        : capture::CalculateRoi(screen_anchor, roi_size_, virtual_screen);
    if (roi.Empty()) {
        common::DebugLog(L"[capture] calculated ROI is empty");
        notify_capture_failure(screen_anchor, roi, L"calculated ROI is empty", 0.0);
        return;
    }

    if (!started_) {
        common::DebugLog(L"[ocr] detector worker is not started");
        return;
    }
    {
        std::lock_guard lock(jobs_mutex_);
        jobs_.push_back(ScanJob{
            {},
            {},
            {},
            {},
            total_start,
            screen_anchor,
            roi,
            0.0,
            AnchorPoint{
                static_cast<float>(screen_anchor.x - roi.left),
                static_cast<float>(screen_anchor.y - roi.top),
            },
            profile.type,
            game_mode_,
            0,
            0,
            0,
            0,
            false,
            virtual_screen,
        });
        jobs_.back().scan_id = scan_id;
        jobs_.back().monitor_bounds = monitor_bounds;
        jobs_.back().tooltip_placement = placement.placement;
        jobs_.back().capture_guard_pending = previous_overlay_visible;
    }
    jobs_available_.notify_one();
}

void ScanTrigger::WorkerLoop() {
    while (true) {
        ScanJob job;
        {
            std::unique_lock lock(jobs_mutex_);
            jobs_available_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
            if (jobs_.empty() && stopping_) {
                return;
            }
            job = std::move(jobs_.front());
            jobs_.pop_front();
        }

        const ScannerProfile profile = ProfileFor(job.profile);
        if (job.profile == ScanProfileType::Inventory
            && job.inventory_path == InventoryRecognitionPath::PrimaryTooltip
            && ShouldInitializeDirectionalRoi(
                job.tooltip_expansion_attempt,
                job.adaptive_expansion_count
            )) {
            const ScanDirection direction = profile.direction_priority[job.direction_index];
            const bool initial_primary_tooltip = job.scan_level == 0
                && job.direction_index == 0 && job.search_depth == 0;
            if (initial_primary_tooltip) {
                const auto prediction = PredictTooltipPlacement(
                    job.screen_anchor,
                    job.monitor_bounds.Empty()
                        ? job.virtual_screen : job.monitor_bounds);
                job.roi = prediction.roi;
                job.tooltip_placement = prediction.placement;
            } else {
                job.roi = CalculateDirectionalRoi(
                    job.screen_anchor,
                    direction,
                    DirectionalScanSizeForDepth(
                        InventoryScanSizeForLevel(roi_size_, job.scan_level),
                        job.search_depth
                    ),
                    job.virtual_screen
                );
            }
            job.anchor = AnchorPoint{
                static_cast<float>(job.screen_anchor.x - job.roi.left),
                static_cast<float>(job.screen_anchor.y - job.roi.top),
            };
        }

        if (scan_step_callback_) {
            scan_step_callback_(job.roi);
        }

        const auto enqueue_next_inventory_step = [&]() {
            if (job.profile != ScanProfileType::Inventory) {
                return false;
            }
            const auto next_position = NextInventoryScanPosition(
                ProgressiveScanPosition{
                    job.scan_level,
                    job.direction_index,
                    job.search_depth,
                },
                profile.direction_priority.size(),
                job.direction_locked
            );
            if (!next_position.has_value()) {
                if (job.direction_locked) {
                    common::DebugLog(
                        L"[scan-step] direction="
                        + std::wstring(ScanDirectionName(
                            profile.direction_priority[job.direction_index]
                        ))
                        + L" directionLocked=true action=return_no_result"
                    );
                }
                return false;
            }
            const int next_level = next_position->level;
            const std::size_t next_direction = next_position->direction_index;
            const int next_search_depth = next_position->search_depth;
            const ScanDirection direction = profile.direction_priority[next_direction];
            const capture::Rect next_roi = CalculateDirectionalRoi(
                job.screen_anchor,
                direction,
                InventoryScanSizeForLevel(roi_size_, next_level),
                job.virtual_screen
            );
            if (next_roi.Empty()) {
                return false;
            }
            {
                std::lock_guard lock(jobs_mutex_);
                jobs_.push_back(ScanJob{
                    {},
                    {},
                    {},
                    {},
                    job.hotkey_start,
                    job.screen_anchor,
                    next_roi,
                    0.0,
                    AnchorPoint{
                        static_cast<float>(job.screen_anchor.x - next_roi.left),
                        static_cast<float>(job.screen_anchor.y - next_roi.top),
                    },
                    job.profile,
                    job.game_mode,
                    next_level,
                    next_direction,
                    next_search_depth,
                    0,
                    job.direction_locked,
                    job.virtual_screen,
                    job.adaptive_expansion_count,
                    job.total_captured_pixels,
                });
                jobs_.back().scan_id = job.scan_id;
            }
            jobs_available_.notify_one();
            common::DebugLog(
                L"[scan-step] queued_next level=" + std::to_wstring(next_level)
                + L" direction=" + ScanDirectionName(direction)
                + L" searchDepth=" + std::to_wstring(next_search_depth)
            );
            return true;
        };

        const bool initial_tooltip_probe = job.profile == ScanProfileType::Inventory
            && job.inventory_path == InventoryRecognitionPath::PrimaryTooltip
            && job.scan_level == 0 && job.direction_index == 0
            && job.search_depth == 0 && job.tooltip_expansion_attempt == 0;
        std::optional<TooltipBoxCandidate> tooltip_box_candidate;
        bool tooltip_probe_preselected = false;
        std::size_t pixels_captured_this_step = 0;
        if (initial_tooltip_probe) {
            const auto capture_start = std::chrono::steady_clock::now();
            job.capture_result = capture_backend_.Capture(job.roi);
            job.capture_ms = ElapsedMilliseconds(capture_start);
        } else {
            const auto capture_start = std::chrono::steady_clock::now();
            job.capture_result = capture_backend_.Capture(job.roi);
            job.capture_ms = ElapsedMilliseconds(capture_start);
        }
        if (job.capture_guard_pending) {
            const auto guard_start = std::chrono::steady_clock::now();
            for (int attempt = 0; attempt < 3
                && job.capture_result.Succeeded()
                && !capture::FrameSafeAfterOverlayHide(
                    true, job.capture_result.source); ++attempt) {
                DwmFlush();
                job.capture_result = capture_backend_.Capture(job.roi);
            }
            const bool fresh = job.capture_result.Succeeded()
                && capture::FrameSafeAfterOverlayHide(
                    true, job.capture_result.source);
            job.capture_ms += ElapsedMilliseconds(guard_start);
            common::DebugLog(
                L"[overlay-capture-guard] previousOverlayVisible=true"
                L" hiddenForCapture=true captureFrameAfterHide="
                + std::wstring(fresh ? L"true" : L"false")
                + L" source=" + CaptureSourceName(job.capture_result.source));
            if (!fresh && job.capture_result.Succeeded()) {
                job.capture_result.error = L"No fresh frame after hiding Noven overlay";
            }
        }
        if (initial_tooltip_probe && job.capture_result.Succeeded()) {
            const auto panel_start = std::chrono::steady_clock::now();
            tooltip_box_candidate = DetectTooltipBox(
                job.capture_result.frame, job.anchor, job.tooltip_placement);
            tooltip_probe_preselected = true;
            common::DebugLog(
                L"[tooltip-probe] placement="
                + std::wstring(TooltipPlacementName(job.tooltip_placement))
                + L" panel="
                + (tooltip_box_candidate.has_value() ? L"true" : L"false")
                + L" panel_search_ms="
                + FormatMeasurement(ElapsedMilliseconds(panel_start)));
        }
        if (pixels_captured_this_step == 0 && job.capture_result.Succeeded()) {
            pixels_captured_this_step = static_cast<std::size_t>(
                job.capture_result.frame.width
            ) * job.capture_result.frame.height;
        }
        const double hotkey_to_capture_ready_ms = ElapsedMilliseconds(job.hotkey_start);
        const ScanDirection current_direction = job.profile == ScanProfileType::Inventory
            ? profile.direction_priority[job.direction_index]
            : profile.direction_priority.front();
        common::DebugLog(
            L"[scan-step] level=" + std::to_wstring(job.scan_level)
            + L" direction=" + ScanDirectionName(current_direction)
            + L" searchDepth=" + std::to_wstring(job.search_depth)
            + L" roi=(" + std::to_wstring(job.roi.left) + L","
            + std::to_wstring(job.roi.top) + L")-("
            + std::to_wstring(job.roi.right) + L"," + std::to_wstring(job.roi.bottom)
            + L") size=" + std::to_wstring(job.roi.Width()) + L"x"
            + std::to_wstring(job.roi.Height())
            + L" capture_ms=" + FormatMeasurement(job.capture_ms)
            + L" tooltip_placement="
            + std::wstring(TooltipPlacementName(job.tooltip_placement))
            + L" hotkey_to_capture_ready_ms="
            + FormatMeasurement(hotkey_to_capture_ready_ms)
        );
        if (!job.capture_result.Succeeded()) {
            const bool queued_next = job.capture_guard_pending
                ? false : enqueue_next_inventory_step();
            common::DebugLog(
                L"[scan-step] capture_failed error=" + job.capture_result.error
                + L" action="
                + (queued_next ? L"continue" : L"stop")
            );
            if (queued_next) {
                continue;
            }
            if (completion_callback_) {
                ScanValidationRecord validation;
                validation.scan_id = job.scan_id;
                validation.hotkey_start = job.hotkey_start;
                validation.game_mode = job.game_mode;
                validation.profile = job.profile;
                validation.anchor = job.screen_anchor;
                validation.roi = job.roi;
                validation.capture_ms = job.capture_ms;
                validation.classification = ScanClassification::CaptureFailed;
                validation.error = job.capture_result.error;
                completion_callback_(ScanCompletion{
                    std::nullopt,
                    std::move(validation),
                    {},
                });
            }
            continue;
        }
        job.total_captured_pixels += pixels_captured_this_step;

        ++capture_number_;
        const std::wstring capture_prefix =
            L"capture_" + std::to_wstring(capture_number_)
            + L"_l" + std::to_wstring(job.scan_level)
            + L"_d" + std::to_wstring(job.search_depth)
            + L"_" + ScanDirectionName(current_direction);
        job.raw_output_path = output_directory_ / (capture_prefix + L".bmp");
        job.detected_output_path = output_directory_ / (capture_prefix + L"_detected.bmp");
        job.spatial_output_path = output_directory_ / (capture_prefix + L"_spatial.bmp");

        common::DebugLog(
            L"[capture] frame result=" + std::to_wstring(job.capture_result.frame.width)
            + L"x" + std::to_wstring(job.capture_result.frame.height)
            + L" source=" + CaptureSourceName(job.capture_result.source)
            + L" error=" + job.capture_result.error
        );

        std::optional<ocr::TextBox> tooltip_region_for_frame;
        const capture::Rect primary_source_roi = job.roi;
        bool primary_tooltip_crop = false;
        float tooltip_geometry_confidence = 0.0F;
        float crop_confidence = 0.0F;
        const auto enqueue_adaptive_fallback = [&]() {
            ScanJob fallback_job;
            fallback_job.hotkey_start = job.hotkey_start;
            fallback_job.scan_id = job.scan_id;
            fallback_job.screen_anchor = job.screen_anchor;
            fallback_job.roi = primary_source_roi;
            fallback_job.anchor = AnchorPoint{
                static_cast<float>(job.screen_anchor.x - primary_source_roi.left),
                static_cast<float>(job.screen_anchor.y - primary_source_roi.top),
            };
            fallback_job.profile = job.profile;
            fallback_job.game_mode = job.game_mode;
            fallback_job.scan_level = job.scan_level;
            fallback_job.direction_index = job.direction_index;
            fallback_job.search_depth = job.search_depth;
            fallback_job.tooltip_expansion_attempt = job.tooltip_expansion_attempt;
            fallback_job.direction_locked = true;
            fallback_job.virtual_screen = job.virtual_screen;
            fallback_job.monitor_bounds = job.monitor_bounds;
            fallback_job.total_captured_pixels = job.total_captured_pixels;
            fallback_job.inventory_path = InventoryRecognitionPath::AdaptiveFallback;
            fallback_job.tooltip_placement = job.tooltip_placement;
            {
                std::lock_guard lock(jobs_mutex_);
                jobs_.push_back(std::move(fallback_job));
            }
            jobs_available_.notify_one();
        };
        if (job.profile == ScanProfileType::Inventory) {
            const TooltipPrimaryProfile tooltip_profile =
                DefaultTooltipPrimaryProfile();
            if (job.inventory_path == InventoryRecognitionPath::PrimaryTooltip) {
                const auto tooltip_start = std::chrono::steady_clock::now();
                if (!tooltip_probe_preselected) {
                    tooltip_box_candidate = DetectTooltipBox(
                        job.capture_result.frame,
                        job.anchor,
                        job.tooltip_placement
                    );
                }
                if (tooltip_box_candidate.has_value()) {
                    const ocr::TextBox& panel = tooltip_box_candidate->rect;
                    const long panel_left = job.roi.left
                        + static_cast<long>(std::floor(panel.x1));
                    const long panel_top = job.roi.top
                        + static_cast<long>(std::floor(panel.y1));
                    const long panel_right = job.roi.left
                        + static_cast<long>(std::ceil(panel.x2));
                    const long panel_bottom = job.roi.top
                        + static_cast<long>(std::ceil(panel.y2));
                    tooltip_geometry_confidence =
                        tooltip_box_candidate->geometryConfidence;
                    common::DebugLog(
                        L"[tooltip-placement] mode="
                        + std::wstring(TooltipPlacementName(job.tooltip_placement))
                        + L" cursor=(" + std::to_wstring(job.screen_anchor.x)
                        + L"," + std::to_wstring(job.screen_anchor.y) + L")"
                        + L" panel=(" + std::to_wstring(panel_left) + L","
                        + std::to_wstring(panel_top) + L","
                        + std::to_wstring(panel_right) + L","
                        + std::to_wstring(panel_bottom) + L")"
                        + L" relative=(" + std::to_wstring(panel_left - job.screen_anchor.x)
                        + L"," + std::to_wstring(panel_top - job.screen_anchor.y)
                        + L"," + std::to_wstring(panel_right - job.screen_anchor.x)
                        + L"," + std::to_wstring(panel_bottom - job.screen_anchor.y)
                        + L") monitor_right=" + std::to_wstring(job.monitor_bounds.right)
                        + L" geometryConfidence="
                        + FormatMeasurement(tooltip_geometry_confidence)
                        + L" panelConfidence="
                        + FormatMeasurement(tooltip_box_candidate->panelConfidence));
                    if (tooltip_geometry_confidence < 0.45F) {
                        common::DebugLog(
                            L"[tooltip-placement] action=reject_crop"
                            L" reason=implausible_cursor_relationship");
                        tooltip_box_candidate.reset();
                    }
                }
                const TooltipPrimaryDecision primary_decision =
                    DecideTooltipPrimaryPath(
                        tooltip_box_candidate,
                        job.roi,
                        job.monitor_bounds.Empty() ? job.virtual_screen : job.monitor_bounds,
                        job.tooltip_expansion_attempt,
                        tooltip_profile
                    );
                common::DebugLog(
                    L"[tooltip-primary] initial_roi=("
                    + std::to_wstring(job.roi.left) + L","
                    + std::to_wstring(job.roi.top) + L")-("
                    + std::to_wstring(job.roi.right) + L","
                    + std::to_wstring(job.roi.bottom) + L") panel_found="
                    + (tooltip_box_candidate.has_value() ? L"true" : L"false")
                    + L" panel_rect="
                    + (tooltip_box_candidate.has_value()
                        ? BoxText(tooltip_box_candidate->rect) : L"none")
                    + L" panel_confidence="
                    + (tooltip_box_candidate.has_value()
                        ? FormatMeasurement(tooltip_box_candidate->panelConfidence)
                        : L"0.00")
                    + L" placement="
                    + std::wstring(TooltipPlacementName(job.tooltip_placement))
                    + L" text_confidence="
                    + (tooltip_box_candidate.has_value()
                        ? FormatMeasurement(tooltip_box_candidate->textConfidence)
                        : L"0.00")
                    + L" background_confidence="
                    + (tooltip_box_candidate.has_value()
                        ? FormatMeasurement(tooltip_box_candidate->backgroundConfidence)
                        : L"0.00")
                    + L" partial="
                    + (tooltip_box_candidate.has_value()
                        && !tooltip_box_candidate->FullBox() ? L"true" : L"false")
                    + L" action="
                    + TooltipPrimaryActionName(primary_decision.action)
                    + L" reason=" + primary_decision.reason
                    + L" detection_ms="
                    + FormatMeasurement(ElapsedMilliseconds(tooltip_start))
                );
                if (tooltip_box_candidate.has_value()) {
                    common::DebugLog(
                        L"[tooltip-primary-border] left="
                        + std::wstring(tooltip_box_candidate->hasLeftBorder ? L"true" : L"false")
                        + L" right="
                        + (tooltip_box_candidate->hasRightBorder ? L"true" : L"false")
                        + L" top="
                        + (tooltip_box_candidate->hasTopBorder ? L"true" : L"false")
                        + L" bottom="
                        + (tooltip_box_candidate->hasBottomBorder ? L"true" : L"false")
                    );
                    ScanResult panel_debug_result;
                    panel_debug_result.profile = job.profile;
                    panel_debug_result.tooltipRegion = tooltip_box_candidate->rect;
                    std::wstring panel_debug_error;
                    const std::filesystem::path panel_debug_path =
                        output_directory_ / (capture_prefix + L"_panel.bmp");
                    if (!WriteSpatialAnnotatedBmp(
                            panel_debug_path,
                            job.capture_result.frame,
                            job.anchor,
                            profile,
                            std::span<const ocr::RecognizedText>{},
                            panel_debug_result,
                            panel_debug_error,
                            tooltip_box_candidate->rect,
                            std::nullopt,
                            {},
                            capture::Point{job.roi.left, job.roi.top},
                            std::nullopt,
                            PredictTooltipPlacement(job.screen_anchor,
                                job.monitor_bounds).roi,
                            job.monitor_bounds.right)) {
                        common::DebugLog(
                            L"[tooltip-primary] panel_debug_write_failed="
                            + panel_debug_error
                        );
                    }
                }

                if (primary_decision.action == TooltipPrimaryAction::RecoverPanel) {
                    job.direction_locked = true;
                    ScanJob recovery_job;
                    recovery_job.hotkey_start = job.hotkey_start;
                    recovery_job.screen_anchor = job.screen_anchor;
                    recovery_job.roi = primary_decision.nextRoi;
                    recovery_job.anchor = AnchorPoint{
                        static_cast<float>(job.screen_anchor.x
                            - primary_decision.nextRoi.left),
                        static_cast<float>(job.screen_anchor.y
                            - primary_decision.nextRoi.top),
                    };
                    recovery_job.profile = job.profile;
                    recovery_job.scan_id = job.scan_id;
                    recovery_job.game_mode = job.game_mode;
                    recovery_job.scan_level = job.scan_level;
                    recovery_job.direction_index = job.direction_index;
                    recovery_job.search_depth = job.search_depth;
                    recovery_job.tooltip_expansion_attempt =
                        job.tooltip_expansion_attempt + 1;
                    recovery_job.direction_locked = true;
                    recovery_job.virtual_screen = job.virtual_screen;
                    recovery_job.monitor_bounds = job.monitor_bounds;
                    recovery_job.total_captured_pixels = job.total_captured_pixels;
                    recovery_job.inventory_path =
                        InventoryRecognitionPath::PrimaryTooltip;
                    recovery_job.tooltip_placement = job.tooltip_placement;
                    {
                        std::lock_guard lock(jobs_mutex_);
                        jobs_.push_back(std::move(recovery_job));
                    }
                    jobs_available_.notify_one();
                    common::DebugLog(
                        L"[tooltip-primary] action=recover_panel next_roi=("
                        + std::to_wstring(primary_decision.nextRoi.left) + L","
                        + std::to_wstring(primary_decision.nextRoi.top) + L")-("
                        + std::to_wstring(primary_decision.nextRoi.right) + L","
                        + std::to_wstring(primary_decision.nextRoi.bottom) + L")"
                    );
                    continue;
                }

                if (primary_decision.action == TooltipPrimaryAction::PreciseCrop
                    && tooltip_box_candidate.has_value()) {
                    job.direction_locked = true;
                    long crop_left = 0;
                    long crop_top = 0;
                    const auto crop_start = std::chrono::steady_clock::now();
                    capture::CapturedFrame cropped = CropFrame(
                        job.capture_result.frame,
                        tooltip_box_candidate->rect,
                        tooltip_profile.cropPadding,
                        crop_left,
                        crop_top
                    );
                    if (cropped.width > 0 && cropped.height > 0) {
                        job.capture_result.frame = std::move(cropped);
                        job.roi.left += crop_left;
                        job.roi.top += crop_top;
                        job.roi.right = job.roi.left
                            + static_cast<long>(job.capture_result.frame.width);
                        job.roi.bottom = job.roi.top
                            + static_cast<long>(job.capture_result.frame.height);
                        job.anchor.x -= static_cast<float>(crop_left);
                        job.anchor.y -= static_cast<float>(crop_top);
                        tooltip_region_for_frame = ocr::TextBox{
                            tooltip_box_candidate->rect.x1
                                - static_cast<float>(crop_left),
                            tooltip_box_candidate->rect.y1
                                - static_cast<float>(crop_top),
                            tooltip_box_candidate->rect.x2
                                - static_cast<float>(crop_left),
                            tooltip_box_candidate->rect.y2
                                - static_cast<float>(crop_top),
                            1.0F,
                        };
                        if (scan_step_callback_) {
                            scan_step_callback_(job.roi);
                        }
                        primary_tooltip_crop = true;
                        crop_confidence = std::min(1.0F,
                            tooltip_box_candidate->panelConfidence)
                            * tooltip_geometry_confidence;
                        common::DebugLog(
                            L"[tooltip-primary] action=precise_crop final_roi=("
                            + std::to_wstring(job.roi.left) + L","
                            + std::to_wstring(job.roi.top) + L")-("
                            + std::to_wstring(job.roi.right) + L","
                            + std::to_wstring(job.roi.bottom) + L") crop_ms="
                            + FormatMeasurement(ElapsedMilliseconds(crop_start))
                        );
                    } else {
                        job.inventory_path = InventoryRecognitionPath::AdaptiveFallback;
                        common::DebugLog(
                            L"[fallback] reason=precise_crop_failed "
                            L"method=AdaptiveTextExpansion"
                        );
                    }
                } else {
                    job.inventory_path = InventoryRecognitionPath::AdaptiveFallback;
                    common::DebugLog(
                        L"[fallback] reason=" + std::wstring(primary_decision.reason)
                        + L" method=AdaptiveTextExpansion"
                    );
                }
            } else {
                common::DebugLog(
                    L"[fallback] reason=queued_fallback "
                    L"method=AdaptiveTextExpansion"
                );
            }
        }

        ocr::DetectionResult detection = text_detector_.Detect(job.capture_result.frame);
        if (primary_tooltip_crop && !detection.boxes.empty()) {
            auto title_band = DeriveTooltipTitleBand(
                detection.boxes,
                capture::Size{
                    static_cast<long>(job.capture_result.frame.width),
                    static_cast<long>(job.capture_result.frame.height),
                }
            );
            if (title_band.has_value() && tooltip_region_for_frame.has_value()) {
                title_band = ClampTooltipTitleCrop(
                    *title_band, *tooltip_region_for_frame, 4.0F);
            }
            if (title_band.has_value()) {
                long crop_left = 0;
                long crop_top = 0;
                capture::CapturedFrame title_crop = CropFrame(
                    job.capture_result.frame,
                    *title_band,
                    0,
                    crop_left,
                    crop_top
                );
                if (title_crop.width > 0 && title_crop.height > 0) {
                    std::vector<ocr::TextBox> title_boxes;
                    for (ocr::TextBox box : detection.boxes) {
                        if (box.x2 < title_band->x1 || box.x1 > title_band->x2
                            || box.y2 < title_band->y1 || box.y1 > title_band->y2) {
                            continue;
                        }
                        box.x1 -= static_cast<float>(crop_left);
                        box.x2 -= static_cast<float>(crop_left);
                        box.y1 -= static_cast<float>(crop_top);
                        box.y2 -= static_cast<float>(crop_top);
                        title_boxes.push_back(box);
                    }
                    job.capture_result.frame = std::move(title_crop);
                    job.roi.left += crop_left;
                    job.roi.top += crop_top;
                    job.roi.right = job.roi.left
                        + static_cast<long>(job.capture_result.frame.width);
                    job.roi.bottom = job.roi.top
                        + static_cast<long>(job.capture_result.frame.height);
                    job.anchor.x -= static_cast<float>(crop_left);
                    job.anchor.y -= static_cast<float>(crop_top);
                    detection.boxes = std::move(title_boxes);
                    tooltip_region_for_frame = ocr::TextBox{
                        0.0F,
                        0.0F,
                        static_cast<float>(job.capture_result.frame.width),
                        static_cast<float>(job.capture_result.frame.height),
                        1.0F,
                    };
                    if (scan_step_callback_) {
                        scan_step_callback_(job.roi);
                    }
                    common::DebugLog(
                        L"[tooltip-title] panel_boxes="
                        + std::to_wstring(detection.boxes.size())
                        + L" band=" + BoxText(*title_band)
                        + L" crop_roi=(" + std::to_wstring(job.roi.left) + L","
                        + std::to_wstring(job.roi.top) + L")-("
                        + std::to_wstring(job.roi.right) + L","
                        + std::to_wstring(job.roi.bottom) + L")"
                    );
                }
            } else {
                common::DebugLog(L"[tooltip-title] band=unavailable action=ocr_panel");
            }
        }
        const bool text_evidence = !detection.boxes.empty();
        if (job.profile == ScanProfileType::Inventory && text_evidence) {
            job.direction_locked = true;
        }
        common::DebugLog(
            std::wstring(L"[scan-step] direction=")
            + ScanDirectionName(current_direction)
            + L" textEvidence=" + (text_evidence ? L"true" : L"false")
            + L" tooltipEvidence="
            + (tooltip_box_candidate.has_value() ? L"true" : L"false")
            + L" directionLocked=" + (job.direction_locked ? L"true" : L"false")
            + L" action="
            + (job.direction_locked ? L"recognize_locked" : L"recognize_or_continue")
        );

        const AdaptiveTextExpansionProfile adaptive_profile =
            DefaultAdaptiveTextExpansionProfile();
        AdaptiveTextAnalysis adaptive_analysis;
        std::vector<ocr::TextBox> recognition_boxes = detection.boxes;
        if (job.profile == ScanProfileType::Inventory
            && job.direction_locked
            && job.inventory_path == InventoryRecognitionPath::AdaptiveFallback) {
            adaptive_analysis = AnalyzeTextContinuity(
                detection.boxes,
                job.anchor,
                capture::Size{
                    static_cast<long>(job.capture_result.frame.width),
                    static_cast<long>(job.capture_result.frame.height),
                },
                current_direction,
                tooltip_region_for_frame,
                adaptive_profile
            );
            if (detection.boxes.empty() && job.adaptive_expansion_count > 0) {
                adaptive_analysis.noContinuationAfterExpansion = true;
                adaptive_analysis.horizontalComplete = true;
                adaptive_analysis.verticalComplete = true;
            }
            const bool expansion_count_limit_reached =
                job.adaptive_expansion_count
                    >= adaptive_profile.maximumExpansionCount;
            if (expansion_count_limit_reached) {
                adaptive_analysis.horizontalComplete = true;
                adaptive_analysis.verticalComplete = true;
                adaptive_analysis.needsExpandRight = false;
                adaptive_analysis.needsExpandTop = false;
                adaptive_analysis.needsExpandBottom = false;
                adaptive_analysis.widthCompletedBySafetyLimit = true;
                adaptive_analysis.heightCompletedBySafetyLimit = true;
            }
            if (job.roi.Width() >= adaptive_profile.maximumWidth) {
                adaptive_analysis.horizontalComplete = true;
                adaptive_analysis.needsExpandRight = false;
                adaptive_analysis.widthCompletedBySafetyLimit = true;
            }
            if (job.roi.Height() >= adaptive_profile.maximumHeight) {
                adaptive_analysis.verticalComplete = true;
                adaptive_analysis.needsExpandTop = false;
                adaptive_analysis.needsExpandBottom = false;
                adaptive_analysis.heightCompletedBySafetyLimit = true;
            }
            const TooltipExpansionEvidence tooltip_evidence{
                tooltip_box_candidate.has_value(),
                tooltip_box_candidate.has_value()
                    && tooltip_box_candidate->FullBox(),
                tooltip_box_candidate.has_value()
                    && tooltip_box_candidate->hasLeftBorder,
                tooltip_box_candidate.has_value()
                    && tooltip_box_candidate->hasRightBorder,
                tooltip_box_candidate.has_value()
                    && tooltip_box_candidate->hasTopBorder,
                tooltip_box_candidate.has_value()
                    && tooltip_box_candidate->hasBottomBorder,
                tooltip_box_candidate.has_value()
                    && tooltip_box_candidate->clippedRight,
                tooltip_box_candidate.has_value()
                    && tooltip_box_candidate->clippedTop,
                tooltip_box_candidate.has_value()
                    && tooltip_box_candidate->clippedBottom,
            };
            adaptive_analysis.trustedTooltipRightBorder =
                tooltip_evidence.hasRightBorder;
            adaptive_analysis.tooltipRightBorderX = tooltip_box_candidate.has_value()
                ? tooltip_box_candidate->rect.x2
                : 0.0F;
            LockedScanDecision locked_decision = DecideLockedScanStep(
                job.locked_stage,
                adaptive_analysis,
                tooltip_evidence
            );
            for (std::size_t box_index = 0;
                 box_index < detection.boxes.size();
                 ++box_index) {
                common::DebugLog(
                    L"[locked-state-box] index=" + std::to_wstring(box_index)
                    + L" rect=" + BoxText(detection.boxes[box_index])
                );
            }
            common::DebugLog(
                L"[locked-state] stage="
                + std::wstring(LockedScanStageName(job.locked_stage))
                + L" direction=" + ScanDirectionName(current_direction)
                + L" expansionCount="
                + std::to_wstring(job.adaptive_expansion_count)
                + L" roi=(" + std::to_wstring(job.roi.left) + L","
                + std::to_wstring(job.roi.top) + L")-("
                + std::to_wstring(job.roi.right) + L","
                + std::to_wstring(job.roi.bottom) + L") size="
                + std::to_wstring(job.roi.Width()) + L"x"
                + std::to_wstring(job.roi.Height()) + L" boxes="
                + std::to_wstring(detection.boxes.size())
                + L" localBoxes="
                + std::to_wstring(adaptive_analysis.localBoxIndices.size())
                + L" localMedianHeight="
                + FormatMeasurement(adaptive_analysis.localMedianHeight)
                + L" medianLocalGap="
                + FormatMeasurement(adaptive_analysis.medianLocalGap)
                + L" nearestGap="
                + (std::isfinite(adaptive_analysis.nearestOutsideGap)
                    ? FormatMeasurement(adaptive_analysis.nearestOutsideGap)
                    : L"none")
                + L" stopThreshold="
                + FormatMeasurement(adaptive_analysis.stopThreshold)
                + L" rightmostText="
                + FormatMeasurement(adaptive_analysis.rightmostTextX)
                + L" rightTextMargin="
                + FormatMeasurement(adaptive_analysis.rightTextMargin)
                + L" topTextMargin="
                + FormatMeasurement(adaptive_analysis.topTextMargin)
                + L" bottomTextMargin="
                + FormatMeasurement(adaptive_analysis.bottomTextMargin)
                + L" tooltipRect="
                + (tooltip_box_candidate.has_value()
                    ? BoxText(tooltip_box_candidate->rect)
                    : L"none")
                + L" tooltipComplete="
                + (tooltip_evidence.complete ? L"true" : L"false")
                + L" hasRightBorder="
                + (tooltip_evidence.hasRightBorder ? L"true" : L"false")
                + L" widthComplete="
                + (adaptive_analysis.horizontalComplete ? L"true" : L"false")
                + L" needsExpandLeft="
                + (adaptive_analysis.needsExpandLeft ? L"true" : L"false")
                + L" needsExpandRight="
                + (adaptive_analysis.needsExpandRight ? L"true" : L"false")
                + L" needsExpandTop="
                + (adaptive_analysis.needsExpandTop ? L"true" : L"false")
                + L" needsExpandBottom="
                + (adaptive_analysis.needsExpandBottom ? L"true" : L"false")
                + L" transition=" + locked_decision.reason
            );
            if (job.locked_stage == LockedScanStage::HorizontalExpansion
                && !(locked_decision.shouldExpand
                    && locked_decision.expansionSide == ExpansionSide::Right)) {
                common::DebugLog(
                    L"[locked-scan] stage=horizontal action=width_complete"
                );
            }
            if (locked_decision.nextStage == LockedScanStage::TextAssembly) {
                common::DebugLog(
                    L"[locked-scan] stage=vertical action=height_complete"
                );
            }

            if (locked_decision.shouldExpand
                && job.adaptive_expansion_count
                    < adaptive_profile.maximumExpansionCount) {
                const capture::Rect expanded_roi = ExpandAdaptiveTextRoi(
                    job.roi,
                    locked_decision.expansionSide,
                    adaptive_analysis.localMedianHeight,
                    job.virtual_screen,
                    adaptive_profile
                );
                const std::size_t expanded_pixels = expanded_roi.Empty()
                    ? 0
                    : static_cast<std::size_t>(expanded_roi.Width())
                        * static_cast<std::size_t>(expanded_roi.Height());
                const bool changed = expanded_roi.left != job.roi.left
                    || expanded_roi.top != job.roi.top
                    || expanded_roi.right != job.roi.right
                    || expanded_roi.bottom != job.roi.bottom;
                adaptive_analysis.nextRoi = expanded_roi;
                if (changed && expanded_pixels > 0
                    && job.total_captured_pixels + expanded_pixels
                        <= adaptive_profile.maximumTotalCapturedPixels) {
                    std::vector<LockedRoiStep> locked_roi_steps =
                        job.locked_roi_steps;
                    locked_roi_steps.push_back(LockedRoiStep{
                        job.roi,
                        job.locked_stage,
                    });
                    std::vector<ocr::RecognizedText> detector_debug_boxes;
                    detector_debug_boxes.reserve(detection.boxes.size());
                    for (const ocr::TextBox& box : detection.boxes) {
                        detector_debug_boxes.push_back(ocr::RecognizedText{
                            box,
                            {},
                            box.confidence,
                        });
                    }
                    ScanResult locked_debug_result;
                    locked_debug_result.profile = job.profile;
                    std::wstring locked_debug_error;
                    static_cast<void>(WriteSpatialAnnotatedBmp(
                        job.spatial_output_path,
                        job.capture_result.frame,
                        job.anchor,
                        profile,
                        detector_debug_boxes,
                        locked_debug_result,
                        locked_debug_error,
                        std::nullopt,
                        adaptive_analysis,
                        locked_roi_steps,
                        capture::Point{job.roi.left, job.roi.top}
                    ));
                    {
                        std::lock_guard lock(jobs_mutex_);
                        jobs_.push_back(ScanJob{
                            {}, {}, {}, {},
                            job.hotkey_start,
                            job.screen_anchor,
                            expanded_roi,
                            0.0,
                            AnchorPoint{
                                static_cast<float>(job.screen_anchor.x - expanded_roi.left),
                                static_cast<float>(job.screen_anchor.y - expanded_roi.top),
                            },
                            job.profile,
                            job.game_mode,
                            job.scan_level,
                            job.direction_index,
                            job.search_depth,
                            job.tooltip_expansion_attempt,
                            true,
                            job.virtual_screen,
                            job.adaptive_expansion_count + 1,
                            job.total_captured_pixels,
                            locked_decision.nextStage,
                            std::move(locked_roi_steps),
                        });
                        jobs_.back().scan_id = job.scan_id;
                    }
                    jobs_available_.notify_one();
                    common::DebugLog(
                        L"[locked-scan] stage="
                        + std::wstring(LockedScanStageName(job.locked_stage))
                        + L" action=expand_"
                        + std::wstring(ExpansionSideName(
                            locked_decision.expansionSide
                        ))
                        + L" next_roi=(" + std::to_wstring(expanded_roi.left)
                        + L"," + std::to_wstring(expanded_roi.top) + L")-("
                        + std::to_wstring(expanded_roi.right) + L","
                        + std::to_wstring(expanded_roi.bottom) + L")"
                        + L" reason=" + locked_decision.reason
                    );
                    continue;
                }
                common::DebugLog(
                    L"[locked-scan] stage="
                    + std::wstring(LockedScanStageName(job.locked_stage))
                    + L" action=final_assembly_at_safety_limit"
                );
                adaptive_analysis.horizontalComplete = true;
                adaptive_analysis.verticalComplete = true;
                locked_decision = LockedScanDecision{
                    LockedScanStage::TextAssembly,
                    ExpansionSide::None,
                    false,
                    L"safety_limit_final_assembly",
                };
            } else if (locked_decision.shouldExpand) {
                common::DebugLog(
                    L"[locked-scan] stage="
                    + std::wstring(LockedScanStageName(job.locked_stage))
                    + L" action=final_assembly_at_expansion_limit"
                );
                adaptive_analysis.horizontalComplete = true;
                adaptive_analysis.verticalComplete = true;
                locked_decision = LockedScanDecision{
                    LockedScanStage::TextAssembly,
                    ExpansionSide::None,
                    false,
                    L"expansion_limit_final_assembly",
                };
            } else if (adaptive_analysis.stoppedByLargeGap) {
                common::DebugLog(
                    L"[locked-scan] stage="
                    + std::wstring(LockedScanStageName(job.locked_stage))
                    + L" gap="
                    + FormatMeasurement(adaptive_analysis.nearestOutsideGap)
                    + L" stopThreshold="
                    + FormatMeasurement(adaptive_analysis.stopThreshold)
                    + L" action=stop_large_gap"
                );
            } else {
                common::DebugLog(
                    L"[locked-scan] stage="
                    + std::wstring(LockedScanStageName(job.locked_stage))
                    + L" action="
                    + (locked_decision.nextStage == LockedScanStage::TextAssembly
                        ? L"coverage_complete"
                        : L"stop_no_continuation")
                );
            }

            job.locked_stage = locked_decision.nextStage;

            if (!adaptive_analysis.localBoxIndices.empty()) {
                recognition_boxes.clear();
                recognition_boxes.reserve(adaptive_analysis.localBoxIndices.size());
                for (const std::size_t index : adaptive_analysis.localBoxIndices) {
                    recognition_boxes.push_back(detection.boxes[index]);
                }
            }
        }
        ocr::RecognitionResult recognition = text_recognizer_.Recognize(
            job.capture_result.frame,
            recognition_boxes
        );
        if (primary_tooltip_crop) {
            const std::vector<ocr::RecognizedText> original_texts = recognition.texts;
            recognition.texts = FilterTooltipNameFragments(
                job.capture_result.frame,
                original_texts
            );
            for (std::size_t index = 0; index < original_texts.size(); ++index) {
                const ocr::RecognizedText& text = original_texts[index];
                const float background_confidence = TooltipTextBackgroundConfidence(
                    job.capture_result.frame,
                    text.box
                );
                const bool retained = std::any_of(
                    recognition.texts.begin(),
                    recognition.texts.end(),
                    [&](const ocr::RecognizedText& candidate) {
                        return candidate.box.x1 == text.box.x1
                            && candidate.box.y1 == text.box.y1
                            && candidate.text == text.text;
                    }
                );
                common::DebugLog(
                    L"[tooltip-ocr-fragment] index=" + std::to_wstring(index)
                    + L" text=\"" + Utf8ToWide(text.text) + L"\" background="
                    + FormatMeasurement(background_confidence)
                    + L" retained=" + (retained ? L"true" : L"false")
                );
            }
            common::DebugLog(
                L"[tooltip-ocr] boxes="
                + std::to_wstring(recognition.texts.size())
                + L" filtered="
                + std::to_wstring(original_texts.size() - recognition.texts.size())
            );
        }
        common::DebugLog(
            L"[scan:" + std::to_wstring(job.scan_id)
            + L"][tooltip] candidate="
            + (tooltip_region_for_frame.has_value() ? L"true" : L"false")
            + (tooltip_region_for_frame.has_value()
                ? L" bounds=" + BoxText(*tooltip_region_for_frame)
                : L"")
        );
        const double hotkey_to_text_ms = ElapsedMilliseconds(job.hotkey_start);

        ScanValidationRecord validation;
        validation.scan_id = job.scan_id;
        validation.hotkey_start = job.hotkey_start;
        validation.game_mode = job.game_mode;
        validation.profile = job.profile;
        validation.anchor = job.screen_anchor;
        validation.roi = job.roi;
        validation.capture_ms = job.capture_ms;
        validation.detected_box_count = detection.boxes.size();
        validation.recognized_box_count = recognition.texts.size();
        validation.detector_preprocess_ms = detection.timings.preprocessing_ms;
        validation.detector_inference_ms = detection.timings.inference_ms;
        validation.detector_postprocess_ms = detection.timings.postprocessing_ms;
        validation.recognition_preprocess_ms = recognition.timings.crop_preprocessing_ms;
        validation.recognition_inference_ms = recognition.timings.inference_ms;
        validation.recognition_decode_ms = recognition.timings.decode_ms;

        std::vector<MatchedText> matched_texts;
        matched_texts.reserve(recognition.texts.size());

        std::wstring raw_error;
        const auto raw_write_start = std::chrono::steady_clock::now();
        const bool raw_written = WriteDebugBmp(
            job.raw_output_path,
            job.capture_result.frame,
            raw_error
        );
        const double raw_write_ms = ElapsedMilliseconds(raw_write_start);

        std::wstring detected_error;
        const auto detected_write_start = std::chrono::steady_clock::now();
        const bool detected_written = ocr::WriteAnnotatedBmp(
            job.detected_output_path,
            job.capture_result.frame,
            detection.boxes,
            detected_error
        );
        const double detected_write_ms = ElapsedMilliseconds(detected_write_start);

        const auto matching_start = std::chrono::steady_clock::now();
        const LocalTextGroupingProfile grouping_profile =
            DefaultLocalTextGroupingProfile();
        const OrderedTextAssembly text_assembly =
            AssembleLocalTextInReadingOrder(recognition.texts);
        std::optional<InventoryRecognitionResult> inventory_recognition;
        if (job.profile == ScanProfileType::Inventory && job.direction_locked) {
            job.locked_stage = LockedScanStage::TextAssembly;
            for (std::size_t line_index = 0;
                 line_index < text_assembly.lines.size();
                 ++line_index) {
                const OrderedTextLine& line = text_assembly.lines[line_index];
                common::DebugLog(
                    L"[text-assembly] line=" + std::to_wstring(line_index)
                    + L" boxes=" + IndexList(line.boxIndices)
                    + L" text=\"" + Utf8ToWide(line.text) + L"\""
                );
            }
            common::DebugLog(
                L"[text-assembly] complete=\""
                + Utf8ToWide(text_assembly.completeText) + L"\""
            );
            if (primary_tooltip_crop) {
                common::DebugLog(
                    L"[tooltip-ocr] assembled=\""
                    + Utf8ToWide(text_assembly.completeText) + L"\""
                );
            }
            job.locked_stage = LockedScanStage::CatalogMatch;
        }
        if (job.profile == ScanProfileType::RaidPickup) {
        const auto evidence_coverage = [&](const data::ItemMatch& match) {
            if (job.profile != ScanProfileType::Inventory
                || !job.direction_locked
                || text_assembly.completeText.empty()) {
                return 1.0F;
            }
            return CatalogEvidenceCoverage(
                text_assembly.completeText,
                match.matchedAlias
            );
        };
        const auto log_match_diagnostics = [&](const wchar_t* tag,
                                                std::size_t box_index,
                                                const std::string& text,
                                                const std::vector<data::ItemMatch>& matches) {
            const std::string normalized = data::NormalizeForMatching(text);
            const bool short_ascii_token = normalized.size() <= 4
                && normalized.find(' ') == std::string::npos
                && std::all_of(
                    normalized.begin(),
                    normalized.end(),
                    [](unsigned char character) {
                        return (character >= 'a' && character <= 'z')
                            || (character >= '0' && character <= '9');
                    }
                );
            for (std::size_t candidate_index = 0;
                 candidate_index < matches.size();
                 ++candidate_index) {
                const data::ItemMatch& candidate = matches[candidate_index];
                const wchar_t* reason = L"none";
                if (candidate.ambiguous) {
                    reason = short_ascii_token
                        ? L"short_common_token"
                        : (candidate.scoreGap <= 0.08F
                            ? L"insufficient_score_gap"
                            : L"multiple_competitive_matches");
                }
                common::DebugLog(
                    std::wstring(tag) + L" box=" + std::to_wstring(box_index)
                    + L" rank=" + std::to_wstring(candidate_index + 1)
                    + L" OCR=\"" + Utf8ToWide(text) + L"\" normalized=\""
                    + Utf8ToWide(normalized) + L"\" alias=\""
                    + Utf8ToWide(candidate.matchedAlias) + L"\" item_id="
                    + Utf8ToWide(candidate.item->id)
                    + L" score=" + FormatMeasurement(candidate.score)
                    + L" bestScore=" + FormatMeasurement(candidate.bestScore)
                    + L" secondBestScore=" + FormatMeasurement(candidate.secondBestScore)
                    + L" scoreGap=" + FormatMeasurement(candidate.scoreGap)
                    + L" competitive="
                    + std::to_wstring(candidate.competitiveCandidateCount)
                    + L" ambiguous=" + (candidate.ambiguous ? L"true" : L"false")
                    + L" reason=" + reason
                    + L" match_type="
                    + Utf8ToWide(data::MatchTypeName(candidate.matchType))
                );
            }
        };
        for (std::size_t index = 0; index < recognition.texts.size(); ++index) {
            const auto& recognized = recognition.texts[index];
            const std::string normalized = data::NormalizeForMatching(recognized.text);
            const std::vector<data::ItemMatch> matches = item_catalog_.Match(
                recognized.text,
                5,
                profile.catalog_threshold
            );
            common::DebugLog(
                L"[ocr] [" + std::to_wstring(index) + L"] \""
                + Utf8ToWide(recognized.text) + L"\" conf="
                + FormatMeasurement(recognized.confidence)
                + L" normalized=\"" + Utf8ToWide(normalized) + L"\""
                + L" box=" + BoxText(recognized.box)
            );
            const std::vector<TextNeighborDiagnostic> neighbors = FindNearestTextNeighbors(
                recognition.texts,
                index,
                grouping_profile,
                3
            );
            for (const TextNeighborDiagnostic& neighbor : neighbors) {
                common::DebugLog(
                    L"[ocr-neighbor] box=" + std::to_wstring(index)
                    + L" neighbor=" + std::to_wstring(neighbor.boxIndex)
                    + L" horizontal_gap=" + FormatMeasurement(neighbor.horizontalGap)
                    + L" vertical_gap=" + FormatMeasurement(neighbor.verticalGap)
                    + L" same_line=" + (neighbor.sameLine ? L"true" : L"false")
                    + L" stacked=" + (neighbor.stacked ? L"true" : L"false")
                );
            }
            if (!matches.empty()
                && item_catalog_.IsConfidentMatch(recognized.text, matches.front())) {
                const data::ItemMatch& match = matches.front();
                matched_texts.push_back(MatchedText{
                    recognized,
                    match,
                    {index},
                    1.0F,
                    false,
                    evidence_coverage(match),
                });
                common::DebugLog(
                    L"[match] [" + std::to_wstring(index) + L"] alias=\""
                    + Utf8ToWide(match.matchedAlias) + L"\" item_id="
                    + Utf8ToWide(match.item->id) + L" match_score="
                    + FormatMeasurement(match.score) + L" match_type="
                    + Utf8ToWide(data::MatchTypeName(match.matchType))
                );
            } else if (!matches.empty()) {
                matched_texts.push_back(MatchedText{
                    recognized,
                    matches.front(),
                    {index},
                    1.0F,
                    false,
                    evidence_coverage(matches.front()),
                });
                common::DebugLog(
                    L"[match-ambiguous] box=" + std::to_wstring(index)
                    + L" OCR=\"" + Utf8ToWide(recognized.text)
                    + L"\" selected=false"
                );
                log_match_diagnostics(
                    L"[match-ambiguous-diagnostic]",
                    index,
                    recognized.text,
                    item_catalog_.MatchDiagnostics(recognized.text, 10)
                );
            } else {
                common::DebugLog(L"[match] [" + std::to_wstring(index) + L"] no_candidate");
                log_match_diagnostics(
                    L"[match-diagnostic]",
                    index,
                    recognized.text,
                    item_catalog_.MatchDiagnostics(recognized.text, 10)
                );
            }
        }

        const std::vector<LocalTextGroup> local_groups = BuildLocalTextGroups(
            recognition.texts,
            grouping_profile
        );
        std::vector<TextGroupMatch> group_matches;
        group_matches.reserve(local_groups.size());
        for (const LocalTextGroup& group : local_groups) {
            common::DebugLog(
                L"[ocr-group] boxes=" + IndexList(group.boxIndices)
                + L" text=\"" + Utf8ToWide(group.combinedText)
                + L"\" rect=" + BoxText(group.combinedBox)
                + L" grouping_conf=" + FormatMeasurement(group.groupingConfidence)
                + L" ocr_conf=" + FormatMeasurement(group.ocrConfidence)
            );
            const std::vector<data::ItemMatch> matches = item_catalog_.Match(
                group.combinedText,
                5,
                profile.catalog_threshold
            );
            if (!matches.empty()
                && item_catalog_.IsConfidentMatch(group.combinedText, matches.front())) {
                group_matches.push_back(TextGroupMatch{
                    group.boxIndices,
                    group.combinedBox,
                    group.combinedText,
                    matches.front(),
                    group.groupingConfidence,
                    group.ocrConfidence,
                });
                common::DebugLog(
                    L"[match-group] boxes=" + IndexList(group.boxIndices)
                    + L" alias=\"" + Utf8ToWide(matches.front().matchedAlias)
                    + L"\" item_id=" + Utf8ToWide(matches.front().item->id)
                    + L" match_score=" + FormatMeasurement(matches.front().score)
                );
            } else if (!matches.empty()) {
                common::DebugLog(
                    L"[match-group-ambiguous] boxes=" + IndexList(group.boxIndices)
                    + L" text=\"" + Utf8ToWide(group.combinedText)
                    + L"\" selected=false"
                );
                log_match_diagnostics(
                    L"[match-group-ambiguous-diagnostic]",
                    group.boxIndices.empty() ? 0 : group.boxIndices.front(),
                    group.combinedText,
                    item_catalog_.MatchDiagnostics(group.combinedText, 10)
                );
            } else {
                log_match_diagnostics(
                    L"[match-group-diagnostic]",
                    group.boxIndices.empty() ? 0 : group.boxIndices.front(),
                    group.combinedText,
                    item_catalog_.MatchDiagnostics(group.combinedText, 10)
                );
            }
        }

        for (const TextGroupMatch& group_match : group_matches) {
            const auto single_match = std::find_if(
                matched_texts.begin(),
                matched_texts.end(),
                [&](const MatchedText& matched) {
                    return !matched.grouped
                        && !matched.match.ambiguous
                        && matched.match.item != nullptr
                        && matched.match.item->id == group_match.match.item->id;
                }
            );
            if (single_match != matched_texts.end()
                && !GroupedMatchClearlyBetter(
                    group_match.match.score,
                    single_match->match.score
                )) {
                common::DebugLog(
                    L"[match-group] skipped=single_preferred boxes="
                    + IndexList(group_match.boxIndices)
                    + L" item_id=" + Utf8ToWide(group_match.match.item->id)
                );
                continue;
            }
            const auto grouped_match = std::find_if(
                matched_texts.begin(),
                matched_texts.end(),
                [&](const MatchedText& matched) {
                    return matched.grouped
                        && matched.match.item != nullptr
                        && matched.match.item->id == group_match.match.item->id;
                }
            );
            if (grouped_match != matched_texts.end()) {
                if (group_match.match.score <= grouped_match->match.score) {
                    continue;
                }
                *grouped_match = MatchedText{
                    ocr::RecognizedText{
                        group_match.combinedBox,
                        group_match.combinedText,
                        group_match.ocrConfidence,
                    },
                    group_match.match,
                    group_match.boxIndices,
                    group_match.groupingConfidence,
                    true,
                    evidence_coverage(group_match.match),
                };
                continue;
            }
            matched_texts.push_back(MatchedText{
                ocr::RecognizedText{
                    group_match.combinedBox,
                    group_match.combinedText,
                    group_match.ocrConfidence,
                },
                group_match.match,
                group_match.boxIndices,
                group_match.groupingConfidence,
                true,
                evidence_coverage(group_match.match),
            });
        }

        if (!text_assembly.completeText.empty()) {
            common::DebugLog(
                L"[catalog-input] assembled=\""
                + Utf8ToWide(text_assembly.completeText)
                + L"\" normalized=\""
                + Utf8ToWide(data::NormalizeForMatching(text_assembly.completeText))
                + L"\" line_count=" + std::to_wstring(text_assembly.lines.size())
                + L" box_count="
                + std::to_wstring(text_assembly.orderedBoxIndices.size())
            );
        }
        const auto add_ordered_hypothesis = [&](const std::string& text,
                                                 const ocr::TextBox& box,
                                                 const std::vector<std::size_t>& indices,
                                                 float ocr_confidence,
                                                 const wchar_t* kind,
                                                 bool complete_hypothesis) {
            if (text.empty()) {
                return;
            }
            data::CatalogNarrowingStats narrowing_stats;
            const std::optional<data::ItemDimensions> reliable_item_size;
            const std::vector<data::ItemMatch> candidates =
                item_catalog_.MatchDiagnosticsConstrained(
                text,
                10,
                reliable_item_size,
                complete_hypothesis ? &narrowing_stats : nullptr
            );
            if (complete_hypothesis) {
                common::DebugLog(
                    L"[catalog-narrow] all_items="
                    + std::to_wstring(narrowing_stats.allItems)
                    + L" after_size_filter="
                    + std::to_wstring(narrowing_stats.afterSizeFilter)
                    + L" after_alias_filter="
                    + std::to_wstring(narrowing_stats.afterAliasFilter)
                    + L" size_constraint=unknown"
                );
            }
            if (candidates.empty()) {
                common::DebugLog(
                    L"[catalog-rank] hypothesis=" + std::wstring(kind)
                    + L" no_candidate=true"
                );
                return;
            }
            const auto exact_full_alias = [](data::MatchType type) {
                return type == data::MatchType::ExactName
                    || type == data::MatchType::CanonicalName;
            };
            bool accepted_any = false;
            const float minimum_similarity = complete_hypothesis
                ? std::min(profile.catalog_threshold, 0.58F)
                : profile.catalog_threshold;
            for (std::size_t rank = 0; rank < candidates.size(); ++rank) {
                const data::ItemMatch& candidate = candidates[rank];
                const float coverage = CatalogEvidenceCoverage(
                    text,
                    candidate.matchedAlias
                );
                const float ocr_ambiguity_gap = candidate.scoreGap;
                const bool full_name_match = exact_full_alias(candidate.matchType);
                const bool strong_fuzzy_full_title = complete_hypothesis
                    && candidate.matchType == data::MatchType::FuzzyName
                    && candidate.score >= 0.78F
                    && coverage >= 0.68F
                    && ocr_ambiguity_gap >= 0.08F
                    && candidate.competitiveCandidateCount <= 1;
                const bool accepted = candidate.item != nullptr
                    && !candidate.ambiguous
                    && candidate.score >= minimum_similarity
                    && (full_name_match || !complete_hypothesis
                        || strong_fuzzy_full_title);
                const wchar_t* rejection = L"none";
                if (candidate.ambiguous) {
                    rejection = L"ambiguous";
                } else if (candidate.score < minimum_similarity) {
                    rejection = L"low_similarity";
                } else if (complete_hypothesis && !full_name_match
                    && !strong_fuzzy_full_title) {
                    rejection = candidate.matchType == data::MatchType::FuzzyShortName
                        ? L"short_alias_not_sufficient"
                        : (coverage < 0.68F ? L"low_coverage"
                            : (ocr_ambiguity_gap < 0.08F
                                || candidate.competitiveCandidateCount > 1
                                ? L"insufficient_score_gap" : L"weak_fuzzy_match"));
                }
                common::DebugLog(
                    L"[catalog-rank] hypothesis=" + std::wstring(kind)
                    + L" rank=" + std::to_wstring(rank + 1)
                    + L" candidate_id=" + Utf8ToWide(candidate.item->id)
                    + L" alias=\"" + Utf8ToWide(candidate.matchedAlias) + L"\""
                    + L" similarity=" + FormatMeasurement(candidate.score)
                    + L" coverage=" + FormatMeasurement(coverage)
                    + L" ambiguity_gap=" + FormatMeasurement(ocr_ambiguity_gap)
                    + L" competitive="
                    + std::to_wstring(candidate.competitiveCandidateCount)
                    + L" ambiguous=" + (candidate.ambiguous ? L"true" : L"false")
                    + L" accepted=" + (accepted ? L"true" : L"false")
                    + L" rejection=" + rejection
                );
                if (!accepted) {
                    continue;
                }
                accepted_any = true;
                const auto duplicate = std::find_if(
                    matched_texts.begin(),
                    matched_texts.end(),
                    [&](const MatchedText& matched) {
                        return matched.recognized.text == text
                            && matched.match.item != nullptr
                            && matched.match.item->id == candidate.item->id;
                    }
                );
                const MatchedText hypothesis{
                    ocr::RecognizedText{box, text, ocr_confidence},
                    candidate,
                    indices,
                    1.0F,
                    true,
                    coverage,
                };
                if (duplicate == matched_texts.end()) {
                    matched_texts.push_back(hypothesis);
                } else if (coverage > duplicate->evidenceCoverage
                    || (coverage == duplicate->evidenceCoverage
                        && candidate.score > duplicate->match.score)) {
                    *duplicate = hypothesis;
                }
            }
            if (!accepted_any) {
                const data::ItemMatch& best = candidates.front();
                const float coverage = CatalogEvidenceCoverage(text, best.matchedAlias);
                const wchar_t* reason = best.ambiguous ? L"ambiguous"
                    : (best.score < minimum_similarity ? L"low_similarity"
                        : (complete_hypothesis && coverage < 0.68F
                            ? L"low_coverage" : L"quality_gate"));
                common::DebugLog(
                    L"[catalog-reject] hypothesis=" + std::wstring(kind)
                    + L" reason=" + reason
                    + L" best_id=" + Utf8ToWide(best.item->id)
                    + L" score=" + FormatMeasurement(best.score)
                    + L" coverage=" + FormatMeasurement(coverage)
                    + L" ambiguity_gap=" + FormatMeasurement(best.scoreGap)
                );
            }
        };
        for (const OrderedTextLine& line : text_assembly.lines) {
            add_ordered_hypothesis(
                line.text,
                line.combinedBox,
                line.boxIndices,
                line.ocrConfidence,
                L"line",
                false
            );
        }
        add_ordered_hypothesis(
            text_assembly.completeText,
            text_assembly.combinedBox,
            text_assembly.orderedBoxIndices,
            text_assembly.ocrConfidence,
            L"complete",
            true
        );
        } else {
            inventory_recognition = BuildInventoryRecognition(
                job.scan_id,
                job.inventory_path,
                tooltip_region_for_frame,
                recognition.texts
            );
            inventory_recognition->tooltipGeometryConfidence =
                tooltip_geometry_confidence;
            inventory_recognition->cropConfidence = crop_confidence;
            ResolveInventoryCatalog(
                *inventory_recognition,
                item_catalog_,
                profile.catalog_threshold
            );
            common::DebugLog(
                L"[scan:" + std::to_wstring(job.scan_id)
                + L"][catalog-strict] selected="
                + (inventory_recognition->selectedCandidate.has_value()
                    ? L"true" : L"false")
                + L" reason=" + Utf8ToWide(inventory_recognition->rejectionReason)
            );
            if (!inventory_recognition->selectedCandidate.has_value()
                && !inventory_recognition->normalizedText.empty()) {
                const auto best_effort_start = std::chrono::steady_clock::now();
                BestEffortResolve(*inventory_recognition, item_catalog_);
                for (std::size_t rank = 0;
                     rank < inventory_recognition->bestEffortTop.size(); ++rank) {
                    const BestEffortCandidate& candidate =
                        inventory_recognition->bestEffortTop[rank];
                    common::DebugLog(
                        L"[scan:" + std::to_wstring(job.scan_id)
                        + L"][best-effort-candidate] rank=" + std::to_wstring(rank + 1)
                        + L" item_id=" + Utf8ToWide(candidate.match.item->id)
                        + L" textSimilarity=" + FormatMeasurement(candidate.similarity)
                        + L" similarityContribution="
                        + FormatMeasurement(0.33F * candidate.similarity)
                        + L" coverage=" + FormatMeasurement(candidate.coverage)
                        + L" coverageContribution="
                        + FormatMeasurement(0.15F * candidate.coverage)
                        + L" tokenOverlap=" + FormatMeasurement(candidate.tokenEvidence)
                        + L" tokenContribution="
                        + FormatMeasurement(0.10F * candidate.tokenEvidence)
                        + L" numericEvidence=" + FormatMeasurement(candidate.numericEvidence)
                        + L" numericContribution="
                        + FormatMeasurement(0.03F * candidate.numericEvidence)
                        + L" caliberEvidence=" + FormatMeasurement(candidate.caliberEvidence)
                        + L" caliberContribution="
                        + FormatMeasurement(0.17F * candidate.caliberEvidence)
                        + L" modelEvidence=" + FormatMeasurement(candidate.modelEvidence)
                        + L" modelContribution="
                        + FormatMeasurement(0.18F * candidate.modelEvidence)
                        + L" categoryEvidence=" + FormatMeasurement(candidate.categoryEvidence)
                        + L" categoryContribution="
                        + FormatMeasurement(0.12F * candidate.categoryEvidence)
                        + L" OCRConfidence=" + FormatMeasurement(candidate.ocrConfidence)
                        + L" ocrContribution="
                        + FormatMeasurement(0.02F * candidate.ocrConfidence)
                        + L" ocr_caliber=" + Utf8ToWide(candidate.ocrCaliber)
                        + L" candidate_caliber=" + Utf8ToWide(candidate.candidateCaliber)
                        + L" caliber_match="
                        + (candidate.caliberEvidence > 0.0F ? L"true" : L"false")
                        + L" caliber_conflict="
                        + (candidate.caliberConflict ? L"true" : L"false")
                        + L" category_ocr=" + Utf8ToWide(candidate.ocrCategory)
                        + L" category_candidate=" + Utf8ToWide(candidate.candidateCategory)
                        + L" category_match="
                        + (candidate.categoryEvidence > 0.0F ? L"true" : L"false")
                        + L" category_conflict="
                        + (candidate.categoryConflict ? L"true" : L"false")
                        + L" model_match="
                        + (candidate.modelEvidence > 0.0F ? L"true" : L"false")
                        + L" model_conflict="
                        + (candidate.modelConflict ? L"true" : L"false")
                        + L" contradiction_penalty="
                        + FormatMeasurement(candidate.contradictionPenalty)
                        + L" final_score=" + FormatMeasurement(candidate.score)
                        + L" alias=\"" + Utf8ToWide(candidate.match.matchedAlias) + L"\""
                    );
                }
                common::DebugLog(
                    L"[scan:" + std::to_wstring(job.scan_id)
                    + L"][best-effort-decision] mode="
                    + (inventory_recognition->matchMode == InventoryMatchMode::OcrOnly
                        ? L"OCR_ONLY" : L"BEST_EFFORT")
                    + L" item_id=" + Utf8ToWide(inventory_recognition->selectedItemId)
                    + L" reason=" + Utf8ToWide(inventory_recognition->rejectionReason)
                    + L" score_gap=" + FormatMeasurement(inventory_recognition->ambiguityGap)
                    + L" ambiguous="
                    + (inventory_recognition->bestEffortAmbiguous ? L"true" : L"false")
                );
                common::DebugLog(
                    L"[scan:" + std::to_wstring(job.scan_id)
                    + L"][best-effort] ocr=\""
                    + Utf8ToWide(inventory_recognition->assembledText)
                    + L"\" ambiguous="
                    + (inventory_recognition->bestEffortAmbiguous ? L"true" : L"false")
                    + L" mode="
                    + (inventory_recognition->matchMode == InventoryMatchMode::OcrOnly
                        ? L"OCR_ONLY" : L"BEST_EFFORT")
                    + L" elapsed_ms=" + FormatMeasurement(
                        ElapsedMilliseconds(best_effort_start))
                );
            }
            common::DebugLog(
                L"[scan:" + std::to_wstring(job.scan_id)
                + L"][ocr] path=" + InventoryRecognitionPathName(job.inventory_path)
                + L" assembled=\"" + Utf8ToWide(inventory_recognition->assembledText)
                + L"\" normalized=\""
                + Utf8ToWide(inventory_recognition->normalizedText)
                + L"\" box_count="
                + std::to_wstring(inventory_recognition->rawFragments.size())
            );
            for (std::size_t index = 0;
                 index < inventory_recognition->candidates.size() && index < 20;
                 ++index) {
                const InventoryCatalogCandidate& candidate =
                    inventory_recognition->candidates[index];
                common::DebugLog(
                    L"[scan:" + std::to_wstring(job.scan_id)
                    + L"][catalog] stage=" + Utf8ToWide(candidate.stage)
                    + L" item_id=" + Utf8ToWide(candidate.text.match.item->id)
                    + L" alias=\"" + Utf8ToWide(candidate.text.match.matchedAlias)
                    + L"\" similarity=" + FormatMeasurement(candidate.text.match.score)
                    + L" coverage=" + FormatMeasurement(candidate.text.evidenceCoverage)
                    + L" token_evidence=" + FormatMeasurement(candidate.tokenEvidence)
                    + L" ambiguity_gap=" + FormatMeasurement(candidate.text.match.scoreGap)
                    + L" accepted=" + (candidate.accepted ? L"true" : L"false")
                    + L" rejection=" + Utf8ToWide(candidate.rejectionReason)
                );
            }
            if (inventory_recognition->selectedCandidate.has_value()) {
                matched_texts.push_back(inventory_recognition->candidates[
                    *inventory_recognition->selectedCandidate].text);
            }
            common::DebugLog(
                L"[scan:" + std::to_wstring(job.scan_id)
                + L"][catalog] selected="
                + (inventory_recognition->selectedCandidate.has_value()
                    ? L"true" : L"false")
                + L" item_id=" + Utf8ToWide(inventory_recognition->selectedItemId)
                + L" reason=" + Utf8ToWide(inventory_recognition->rejectionReason)
            );
            if (!inventory_recognition->normalizedText.empty()) {
                std::wstring global = L"[scan:" + std::to_wstring(job.scan_id)
                    + L"][catalog-global] catalog_available="
                    + (item_catalog_.AliasCount() != 0 ? L"true" : L"false")
                    + L" aliases_checked=" + std::to_wstring(
                        inventory_recognition->matchMode == InventoryMatchMode::Strict
                            ? 0 : item_catalog_.AliasCount())
                    + L" top1_id=" + Utf8ToWide(inventory_recognition->selectedItemId)
                    + L" gap=" + FormatMeasurement(inventory_recognition->ambiguityGap);
                if (inventory_recognition->selectedCandidate.has_value()) {
                    const auto& top = inventory_recognition->candidates[
                        *inventory_recognition->selectedCandidate].text.match;
                    global += L" top1_name=" + Utf8ToWide(
                        overlay::DisplayNameForItem(*top.item))
                        + L" top1_score=" + FormatMeasurement(
                            inventory_recognition->matchMode == InventoryMatchMode::BestEffort
                                ? inventory_recognition->bestEffortTop.front().score
                                : top.score);
                }
                if (inventory_recognition->bestEffortTop.size() > 1) {
                    const auto& second = inventory_recognition->bestEffortTop[1];
                    global += L" top2_name=" + Utf8ToWide(
                        overlay::DisplayNameForItem(*second.match.item))
                        + L" top2_score=" + FormatMeasurement(second.score);
                }
                common::DebugLog(global);
            }
        }
        if (job.profile == ScanProfileType::Inventory && job.direction_locked) {
            std::vector<const MatchedText*> catalog_fit_ranking;
            catalog_fit_ranking.reserve(matched_texts.size());
            for (const MatchedText& matched : matched_texts) {
                catalog_fit_ranking.push_back(&matched);
            }
            std::stable_sort(
                catalog_fit_ranking.begin(),
                catalog_fit_ranking.end(),
                [](const MatchedText* left, const MatchedText* right) {
                    const auto exact_token_priority = [](data::MatchType type) {
                        if (type == data::MatchType::ExactName) return 3;
                        if (type == data::MatchType::CanonicalName) return 2;
                        if (type == data::MatchType::ExactShortName
                            || type == data::MatchType::CanonicalShortName) return 1;
                        return 0;
                    };
                    if (left->match.ambiguous != right->match.ambiguous) {
                        return !left->match.ambiguous;
                    }
                    if (left->evidenceCoverage != right->evidenceCoverage) {
                        return left->evidenceCoverage > right->evidenceCoverage;
                    }
                    if (left->match.score != right->match.score) {
                        return left->match.score > right->match.score;
                    }
                    if (exact_token_priority(left->match.matchType)
                        != exact_token_priority(right->match.matchType)) {
                        return exact_token_priority(left->match.matchType)
                            > exact_token_priority(right->match.matchType);
                    }
                    if (left->recognized.confidence != right->recognized.confidence) {
                        return left->recognized.confidence
                            > right->recognized.confidence;
                    }
                    return left->match.scoreGap > right->match.scoreGap;
                }
            );
            for (std::size_t rank = 0; rank < catalog_fit_ranking.size(); ++rank) {
                const MatchedText& fit = *catalog_fit_ranking[rank];
                common::DebugLog(
                    L"[catalog-fit] candidate=\""
                    + Utf8ToWide(fit.match.matchedAlias)
                    + L"\" similarity=" + FormatMeasurement(fit.match.score)
                    + L" coverage=" + FormatMeasurement(fit.evidenceCoverage)
                    + L" ocr=" + FormatMeasurement(fit.recognized.confidence)
                    + L" ambiguity_gap=" + FormatMeasurement(fit.match.scoreGap)
                    + L" match_type="
                    + Utf8ToWide(data::MatchTypeName(fit.match.matchType))
                    + L" ambiguous="
                    + (fit.match.ambiguous ? L"true" : L"false")
                    + L" rank=" + std::to_wstring(rank + 1)
                );
            }
        }
        const double matching_ms = ElapsedMilliseconds(matching_start);
        validation.catalog_valid_candidate_count = inventory_recognition.has_value()
            ? (inventory_recognition->matchMode == InventoryMatchMode::Strict
                && inventory_recognition->selectedCandidate.has_value() ? 1U : 0U)
            : static_cast<std::size_t>(std::count_if(
                matched_texts.begin(),
                matched_texts.end(),
                [](const MatchedText& matched) { return !matched.match.ambiguous; }
            ));
        validation.catalog_matching_ms = matching_ms;

        const auto selection_start = std::chrono::steady_clock::now();
        ScanResult spatial_result;
        if (job.profile == ScanProfileType::RaidPickup) {
            spatial_result = candidate_selector_.Select(
                job.profile, job.anchor, matched_texts, tooltip_region_for_frame
            );
        } else {
            spatial_result.profile = job.profile;
            spatial_result.tooltipRegion = tooltip_region_for_frame;
            if (inventory_recognition->selectedCandidate.has_value()) {
                const MatchedText& selected = inventory_recognition->candidates[
                    *inventory_recognition->selectedCandidate].text;
                const float center_x = (selected.recognized.box.x1
                    + selected.recognized.box.x2) * 0.5F;
                const float center_y = (selected.recognized.box.y1
                    + selected.recognized.box.y2) * 0.5F;
                const float edge_dx = std::max({
                    selected.recognized.box.x1 - job.anchor.x,
                    0.0F,
                    job.anchor.x - selected.recognized.box.x2,
                });
                const float edge_dy = std::max({
                    selected.recognized.box.y1 - job.anchor.y,
                    0.0F,
                    job.anchor.y - selected.recognized.box.y2,
                });
                ScanCandidate candidate;
                candidate.recognized = selected.recognized;
                candidate.match = selected.match;
                candidate.direction = ClassifyDirection(
                    center_x - job.anchor.x, center_y - job.anchor.y
                );
                candidate.distanceToAnchor = std::hypot(edge_dx, edge_dy);
                candidate.centerDistanceToAnchor = std::hypot(
                    center_x - job.anchor.x, center_y - job.anchor.y
                );
                candidate.sourceBoxIndices = selected.sourceBoxIndices;
                candidate.grouped = selected.grouped;
                candidate.evidenceCoverage = selected.evidenceCoverage;
                candidate.participatingInSearch = true;
                spatial_result.selected = candidate;
                spatial_result.nearestValid = candidate;
                spatial_result.considered.push_back(std::move(candidate));
                spatial_result.found = true;
            }
        }
        const double selection_ms = ElapsedMilliseconds(selection_start);
        validation.spatial_selection_ms = selection_ms;
        if (!spatial_result.selected.has_value()) {
            const bool has_unambiguous_match = std::any_of(
                matched_texts.begin(),
                matched_texts.end(),
                [](const MatchedText& matched) {
                    return matched.match.item != nullptr && !matched.match.ambiguous;
                }
            );
            const bool has_ambiguous_match = std::any_of(
                matched_texts.begin(),
                matched_texts.end(),
                [](const MatchedText& matched) {
                    return matched.match.item != nullptr && matched.match.ambiguous;
                }
            );
            const wchar_t* reason = inventory_recognition.has_value()
                    && inventory_recognition->matchMode == InventoryMatchMode::OcrOnly
                ? L"ocr_only_catalog_unavailable"
                : (recognition.texts.empty() ? L"ocr_empty"
                    : (matched_texts.empty() ? L"no_catalog_candidate"
                        : (!has_unambiguous_match && has_ambiguous_match ? L"ambiguous"
                            : L"no_spatial_candidate")));
            common::DebugLog(
                L"[final-match] selected=false reason=" + std::wstring(reason)
            );
        }

        common::DebugLog(
            L"[scan-step] level=" + std::to_wstring(job.scan_level)
            + L" direction=" + ScanDirectionName(current_direction)
            + L" detected_boxes=" + std::to_wstring(detection.boxes.size())
            + L" recognized_boxes=" + std::to_wstring(recognition.texts.size())
            + L" catalog_candidates=" + std::to_wstring(matched_texts.size())
            + L" accepted=" + std::to_wstring(validation.catalog_valid_candidate_count)
            + L" tooltipCandidate="
            + (tooltip_region_for_frame.has_value() ? L"true" : L"false")
            + L" action=" + (spatial_result.selected.has_value() ? L"stop" : L"continue")
            + L" path=" + InventoryRecognitionPathName(job.inventory_path)
        );

        common::DebugLog(
            L"[scan] anchor=(" + FormatMeasurement(job.anchor.x) + L","
            + FormatMeasurement(job.anchor.y) + L") profile="
            + ScanProfileName(job.profile)
            + L" recognized_boxes=" + std::to_wstring(recognition.texts.size())
            + L" catalog_valid_candidates=" + std::to_wstring(matched_texts.size())
            + L" spatial_candidates=" + std::to_wstring(spatial_result.considered.size())
        );
        for (const SpatialSearchStep& step : spatial_result.searchSteps) {
            common::DebugLog(
                L"[spatial-search] scanLevel=" + std::to_wstring(job.scan_level)
                + L" searchDepth=" + std::to_wstring(job.search_depth)
                + L" ring=" + std::to_wstring(step.ring)
                + L" direction=" + ScanDirectionName(step.direction)
                + L" boxes=" + std::to_wstring(step.boxCount)
                + L" valid=" + std::to_wstring(step.validCandidateCount)
                + L" ambiguous=" + std::to_wstring(step.ambiguousCandidateCount)
                + L" accepted=" + std::to_wstring(step.acceptedCandidateCount)
                + L" action=" + SpatialSearchActionName(step.action)
            );
        }
        for (std::size_t index = 0; index < spatial_result.considered.size(); ++index) {
            const ScanCandidate& candidate = spatial_result.considered[index];
            common::DebugLog(
                L"[scan] candidate #" + std::to_wstring(index + 1)
                + L" OCR=\"" + Utf8ToWide(candidate.recognized.text)
                + L"\" match=\"" + Utf8ToWide(candidate.match.matchedAlias)
                + L"\" item_id=" + Utf8ToWide(candidate.match.item->id)
                + L" direction=" + ScanDirectionName(candidate.direction)
                + L" direction_rank=" + std::to_wstring(candidate.directionRank)
                + L" ring=" + std::to_wstring(candidate.ringIndex)
                + L" distance=" + FormatMeasurement(candidate.distanceToAnchor)
                + L" rectDistance=" + FormatMeasurement(candidate.distanceToAnchor)
                + L" centerDistance=" + FormatMeasurement(candidate.centerDistanceToAnchor)
                + L" horizontal_distance=" + FormatMeasurement(candidate.horizontalDistance)
                + L" vertical_distance=" + FormatMeasurement(candidate.verticalDistance)
                + L" box=(" + FormatMeasurement(candidate.recognized.box.x1) + L","
                + FormatMeasurement(candidate.recognized.box.y1) + L")-("
                + FormatMeasurement(candidate.recognized.box.x2) + L","
                + FormatMeasurement(candidate.recognized.box.y2) + L")"
                + L" center=(" + FormatMeasurement(
                    (candidate.recognized.box.x1 + candidate.recognized.box.x2) / 2.0F)
                + L"," + FormatMeasurement(
                    (candidate.recognized.box.y1 + candidate.recognized.box.y2) / 2.0F)
                + L") anchor_roi=(" + FormatMeasurement(job.anchor.x) + L","
                + FormatMeasurement(job.anchor.y) + L")"
                + L" overlaps_row=" + (candidate.overlapsAnchorRow ? L"true" : L"false")
                + L" overlaps_column="
                + (candidate.overlapsAnchorColumn ? L"true" : L"false")
                + L" box_size=" + FormatMeasurement(candidate.boxWidth) + L"x"
                + FormatMeasurement(candidate.boxHeight)
                + L" angle=" + FormatMeasurement(candidate.centerAngleDegrees)
                + L" row_alignment=" + FormatMeasurement(candidate.rowAlignmentScore)
                + L" distance_score=" + FormatMeasurement(candidate.distanceScore)
                 + L" OCRConf=" + FormatMeasurement(candidate.recognized.confidence)
                 + L" MatchScore=" + FormatMeasurement(candidate.match.score)
                 + L" grouped=" + (candidate.grouped ? L"true" : L"false")
                 + L" source_boxes=" + IndexList(candidate.sourceBoxIndices)
                + L" grouping_confidence="
                 + FormatMeasurement(candidate.groupingConfidence)
                + L" evidence_coverage="
                + FormatMeasurement(candidate.evidenceCoverage)
                + L" ambiguous="
                + (candidate.match.ambiguous ? L"true" : L"false")
                + L" tooltipCandidate="
                + (spatial_result.tooltipRegion.has_value() ? L"true" : L"false")
                + L" insideTooltip="
                + (candidate.insideTooltip ? L"true" : L"false")
                + L" participating="
                + (candidate.participatingInSearch ? L"true" : L"false")
                 + L" spatial_score=" + FormatMeasurement(candidate.spatialScore)
                + L" ranking_score=" + FormatMeasurement(candidate.rankingScore)
            );
        }
        std::optional<overlay::ScanDisplayResult> display_result;
        if (spatial_result.selected.has_value()) {
            common::DebugLog(
                L"[scan] selected item_id="
                + Utf8ToWide(spatial_result.selected->match.item->id)
                + L" alias=\""
                + Utf8ToWide(spatial_result.selected->match.matchedAlias) + L"\""
            );
            const bool selected_is_nearest = spatial_result.nearestValid.has_value()
                && SameCandidate(
                    *spatial_result.selected,
                    *spatial_result.nearestValid
                );
            const std::wstring ranking_reason = profile.type == ScanProfileType::RaidPickup
                ? L"legacy_ring_direction_order"
                : (inventory_recognition->matchMode == InventoryMatchMode::BestEffort
                    ? L"best_effort_catalog_candidate"
                    : L"strict_catalog_fit_in_locked_block");
            common::DebugLog(
                L"[scan] selector_result=selected_rank=1 reason="
                + ranking_reason
                + L" nearest_valid=" + (selected_is_nearest ? L"same" : L"different")
            );
            if (profile.type == ScanProfileType::Inventory) {
                common::DebugLog(
                    L"[adaptive-select] rectDistance="
                    + FormatMeasurement(spatial_result.selected->distanceToAnchor)
                    + L" selectedText=\""
                    + Utf8ToWide(spatial_result.selected->recognized.text) + L"\""
                );
            }
            const std::string& item_id = spatial_result.selected->match.item->id;
            validation.selected_item_id = item_id;
            validation.selected_display_name = overlay::DisplayNameForItem(
                *spatial_result.selected->match.item
            );
            validation.selected_ocr_text = spatial_result.selected->recognized.text;
            validation.selected_ocr_confidence =
                spatial_result.selected->recognized.confidence;
            validation.selected_match_score = spatial_result.selected->match.score;
            validation.selected_direction = spatial_result.selected->direction;
            validation.selected_distance = spatial_result.selected->distanceToAnchor;
            common::DebugLog(
                L"[final-match] selected=true item_id=" + Utf8ToWide(item_id)
                + L" display_name=\"" + Utf8ToWide(validation.selected_display_name)
                + L"\" matched_alias=\""
                + Utf8ToWide(spatial_result.selected->match.matchedAlias)
                + L"\" similarity="
                + FormatMeasurement(spatial_result.selected->match.score)
                + L" coverage="
                + FormatMeasurement(spatial_result.selected->evidenceCoverage)
                + L" ambiguity_gap="
                + FormatMeasurement(spatial_result.selected->match.scoreGap)
            );
            if (inventory_recognition.has_value()) {
                common::DebugLog(
                    L"[scan:" + std::to_wstring(job.scan_id)
                    + L"][final-match] mode="
                    + (inventory_recognition->matchMode == InventoryMatchMode::BestEffort
                        ? L"BEST_EFFORT" : L"STRICT")
                    + L" item_id=" + Utf8ToWide(item_id)
                    + L" best_effort_ambiguous="
                    + (inventory_recognition->bestEffortAmbiguous ? L"true" : L"false")
                );
            }

            const auto economy_start = std::chrono::steady_clock::now();
            if (job.profile == ScanProfileType::Inventory) {
                const InventoryEconomyResolution economy = ResolveInventoryEconomy(
                    *inventory_recognition, economy_store_, job.game_mode
                );
                validation.economy_lookup_ms = ElapsedMilliseconds(economy_start);
                validation.economy_lookup_succeeded = economy.found;
                display_result = BuildInventoryDisplayResult(
                    *inventory_recognition, economy, job.game_mode
                );
                inventory_recognition->status = economy.found
                    ? InventoryOutcomeStatus::Success
                    : InventoryOutcomeStatus::EconomyMissing;
                common::DebugLog(
                    L"[scan:" + std::to_wstring(job.scan_id)
                    + L"][economy] item_id=" + Utf8ToWide(item_id)
                    + L" found=" + (economy.found ? L"true" : L"false")
                );
                common::DebugLog(
                    L"[scan:" + std::to_wstring(job.scan_id)
                    + L"][display] built="
                    + (display_result.has_value() ? L"true" : L"false")
                );
            } else {
            const data::ItemEconomyInfo* economy = economy_store_.Lookup(
                job.game_mode,
                item_id
            );
            validation.economy_lookup_ms = ElapsedMilliseconds(economy_start);
            overlay::ScanDisplayResult result;
            result.itemId = item_id;
            result.displayName = validation.selected_display_name;
            result.mode = job.game_mode;
            if (economy == nullptr) {
                common::DebugLog(
                    L"[economy] item_id=" + Utf8ToWide(item_id)
                    + L" found=false mode="
                    + std::wstring(data::GameModeName(job.game_mode))
                );
            } else {
                validation.economy_lookup_succeeded = true;
                result.fleaPrice = economy->fleaPrice;
                result.bestTrader = economy->bestTrader;
                result.valuePerSlot = economy->valuePerSlot;
                result.fleaStatus = economy->fleaStatus;
                result.width = economy->width;
                result.height = economy->height;
                std::wstring economy_message =
                    L"[economy] item_id=" + Utf8ToWide(item_id)
                    + L" found=true mode="
                    + std::wstring(data::GameModeName(job.game_mode))
                    + L" size=" + std::to_wstring(economy->width)
                    + L"x" + std::to_wstring(economy->height)
                    + L" flea_status=" + data::FleaStatusName(economy->fleaStatus);
                if (economy->fleaPrice.has_value()) {
                    economy_message += L" flea=" + std::to_wstring(*economy->fleaPrice);
                } else {
                    economy_message += L" flea=unknown";
                }
                if (economy->bestTrader.has_value()) {
                    economy_message += L" best_trader="
                        + Utf8ToWide(economy->bestTrader->traderId)
                        + L" trader_value="
                        + std::to_wstring(economy->bestTrader->priceRoubles);
                } else {
                    economy_message += L" best_trader=unknown";
                }
                if (economy->bestValue.has_value()) {
                    economy_message += L" best_value=" + std::to_wstring(*economy->bestValue);
                } else {
                    economy_message += L" best_value=unknown";
                }
                if (economy->valuePerSlot.has_value()) {
                    economy_message += L" value_per_slot="
                        + FormatMeasurement(*economy->valuePerSlot);
                } else {
                    economy_message += L" value_per_slot=unknown";
                }
                common::DebugLog(economy_message);
            }
            display_result = std::move(result);
            }
        } else {
            common::DebugLog(L"[scan] selected none");
            if (inventory_recognition.has_value()
                && inventory_recognition->matchMode == InventoryMatchMode::OcrOnly) {
                display_result = BuildInventoryDisplayResult(
                    *inventory_recognition, {}, job.game_mode
                );
                validation.selected_ocr_text = inventory_recognition->assembledText;
                validation.selected_display_name = inventory_recognition->assembledText;
                common::DebugLog(
                    L"[scan:" + std::to_wstring(job.scan_id)
                    + L"][display] mode=OCR_ONLY built="
                    + (display_result.has_value() ? L"true" : L"false")
                );
            }
        }
        if (job.profile == ScanProfileType::Inventory && job.direction_locked) {
            common::DebugLog(
                L"[locked-scan] stage="
                + std::wstring(display_result.has_value()
                    ? LockedScanStageName(LockedScanStage::Complete)
                    : LockedScanStageName(LockedScanStage::Failed))
                + L" action="
                + (display_result.has_value()
                    ? L"return_display" : L"return_no_result")
            );
        }
        if (spatial_result.nearestValid.has_value()) {
            const ScanCandidate& nearest = *spatial_result.nearestValid;
            common::DebugLog(
                L"[scan] nearest_valid OCR=\"" + Utf8ToWide(nearest.recognized.text)
                + L"\" match=\"" + Utf8ToWide(nearest.match.matchedAlias)
                + L"\" item_id=" + Utf8ToWide(nearest.match.item->id)
                + L" distance=" + FormatMeasurement(nearest.distanceToAnchor)
                + L" direction=" + ScanDirectionName(nearest.direction)
            );
        }

        if (!detection.Succeeded() || !recognition.Succeeded()) {
            validation.classification = ScanClassification::OcrFailed;
            validation.error = !detection.Succeeded() ? detection.error : recognition.error;
        } else if (recognition.texts.empty()) {
            validation.classification = ScanClassification::NoText;
        } else if (inventory_recognition.has_value()
            && inventory_recognition->matchMode == InventoryMatchMode::OcrOnly
            && display_result.has_value()) {
            validation.classification = ScanClassification::OcrOnly;
            validation.error = Utf8ToWide(inventory_recognition->rejectionReason);
        } else if (inventory_recognition.has_value()
            && inventory_recognition->matchMode == InventoryMatchMode::BestEffort
            && display_result.has_value()) {
            validation.classification = ScanClassification::BestEffortMatch;
            validation.error = Utf8ToWide(inventory_recognition->rejectionReason);
        } else if (inventory_recognition.has_value()
            && inventory_recognition->status == InventoryOutcomeStatus::CatalogAmbiguous) {
            validation.classification = ScanClassification::CatalogAmbiguous;
            validation.error = Utf8ToWide(inventory_recognition->rejectionReason);
        } else if (matched_texts.empty()) {
            validation.classification = ScanClassification::NoCatalogMatch;
            if (inventory_recognition.has_value()) {
                validation.error = Utf8ToWide(inventory_recognition->rejectionReason);
            }
        } else if (!spatial_result.selected.has_value()) {
            validation.classification = ScanClassification::NoSpatialCandidate;
        } else if (!display_result.has_value()) {
            validation.classification = ScanClassification::DisplayFailed;
            validation.error = L"Catalog selected but display result was not built";
        } else if (!validation.economy_lookup_succeeded) {
            validation.classification = ScanClassification::EconomyMissing;
        } else {
            validation.classification = ScanClassification::Success;
        }

        std::wstring spatial_error;
        const auto spatial_write_start = std::chrono::steady_clock::now();
        std::vector<LockedRoiStep> debug_locked_roi_steps = job.locked_roi_steps;
        if (job.profile == ScanProfileType::Inventory && job.direction_locked) {
            debug_locked_roi_steps.push_back(LockedRoiStep{
                job.roi,
                job.locked_stage,
            });
        }
        const bool spatial_written = WriteSpatialAnnotatedBmp(
            job.spatial_output_path,
            job.capture_result.frame,
            job.anchor,
            profile,
            recognition.texts,
            spatial_result,
            spatial_error,
            spatial_result.tooltipRegion,
            job.profile == ScanProfileType::Inventory
                && job.inventory_path == InventoryRecognitionPath::AdaptiveFallback
                && text_evidence
                ? std::optional<AdaptiveTextAnalysis>{adaptive_analysis}
                : std::nullopt,
            debug_locked_roi_steps,
            capture::Point{job.roi.left, job.roi.top},
            text_assembly,
            job.profile == ScanProfileType::Inventory
                ? std::optional<capture::Rect>{PredictTooltipPlacement(
                    job.screen_anchor, job.monitor_bounds).roi}
                : std::nullopt,
            job.profile == ScanProfileType::Inventory
                ? std::optional<long>{job.monitor_bounds.right}
                : std::nullopt
        );
        const double spatial_write_ms = ElapsedMilliseconds(spatial_write_start);

        if (!raw_written) {
            common::DebugLog(L"[capture] debug image save failed: " + raw_error);
        }
        if (!detected_written) {
            common::DebugLog(L"[ocr] detected image save failed: " + detected_error);
        }
        if (!spatial_written) {
            common::DebugLog(L"[ocr] spatial image save failed: " + spatial_error);
        }

        common::DebugLog(
            L"[ocr] boxes=" + std::to_wstring(detection.boxes.size())
            + L" preprocessing_ms=" + FormatMeasurement(detection.timings.preprocessing_ms)
            + L" inference_ms=" + FormatMeasurement(detection.timings.inference_ms)
            + L" postprocessing_ms=" + FormatMeasurement(detection.timings.postprocessing_ms)
            + L" recognized_boxes=" + std::to_wstring(recognition.texts.size())
            + L" recognition_crop_preprocess_ms="
            + FormatMeasurement(recognition.timings.crop_preprocessing_ms)
            + L" recognition_inference_ms="
            + FormatMeasurement(recognition.timings.inference_ms)
            + L" recognition_decode_ms="
            + FormatMeasurement(recognition.timings.decode_ms)
            + L" matching_ms=" + FormatMeasurement(matching_ms)
            + L" selection_ms=" + FormatMeasurement(selection_ms)
            + L" hotkey_to_text_ms=" + FormatMeasurement(hotkey_to_text_ms)
        );
        common::DebugLog(
            L"[ocr] capture_to_memory_ms="
            + FormatMeasurement(job.capture_result.timings.capture_to_memory_ms)
            + L" raw_bmp_write_ms=" + FormatMeasurement(raw_write_ms)
            + L" detected_bmp_write_ms=" + FormatMeasurement(detected_write_ms)
            + L" spatial_bmp_write_ms=" + FormatMeasurement(spatial_write_ms)
            + L" debug_images_written="
            + (raw_written && detected_written && spatial_written ? L"true" : L"false")
        );

        if (primary_tooltip_crop
            && !spatial_result.selected.has_value()
            && validation.catalog_valid_candidate_count == 0
            && (!inventory_recognition.has_value()
                || inventory_recognition->normalizedText.empty())) {
            common::DebugLog(
                L"[fallback] reason=primary_crop_no_usable_catalog_match "
                L"method=AdaptiveTextExpansion"
            );
            enqueue_adaptive_fallback();
            continue;
        }

        if (!spatial_result.selected.has_value()
            && !display_result.has_value() && job.direction_locked) {
            common::DebugLog(
                L"[scan-step] direction="
                + std::wstring(ScanDirectionName(current_direction))
                + L" directionLocked=true action=return_no_result"
            );
        } else if (!spatial_result.selected.has_value()
            && !display_result.has_value()
            && enqueue_next_inventory_step()) {
            common::DebugLog(
                L"[scan-step] no acceptable item; continuing progressive capture"
            );
            continue;
        }

        common::DebugLog(
            L"[scan:" + std::to_wstring(job.scan_id)
            + L"][scan-output] finalItemSelected="
            + std::wstring(spatial_result.selected.has_value() ? L"true" : L"false")
            + L" displayResultBuilt="
            + (display_result.has_value() ? L"true" : L"false")
            + L" status=" + ScanClassificationName(validation.classification)
            + L" reason=" + validation.error
        );
        if (job.profile == ScanProfileType::Inventory && display_result.has_value()) {
            common::DebugLog(
                L"[scan:" + std::to_wstring(job.scan_id)
                + L"][display] title_source="
                + (display_result->matchQuality == overlay::MatchQuality::OcrOnly
                    ? L"OCR_ONLY" : L"CANONICAL_ITEM_NAME")
                + L" item_id=" + Utf8ToWide(display_result->itemId)
                + L" shown_title=\"" + Utf8ToWide(display_result->displayName) + L"\""
            );
        }
        if (completion_callback_) {
            completion_callback_(ScanCompletion{
                std::move(display_result),
                std::move(validation),
                spatial_written ? job.spatial_output_path : std::filesystem::path{},
            });
        }
    }
}

} // namespace noven::scanner
