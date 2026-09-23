#include "scanner/ScanTrigger.h"

#include "capture/Roi.h"
#include "common/DebugLog.h"
#include "scanner/AdaptiveTextExpansion.h"
#include "scanner/DebugImageWriter.h"
#include "ocr/DetectionDebugWriter.h"
#include "scanner/LocalTextGrouping.h"
#include "scanner/ProgressiveScan.h"
#include "scanner/SpatialDebugWriter.h"
#include "scanner/TooltipHeuristic.h"

#include <windows.h>

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

void ScanTrigger::Trigger() {
    const auto total_start = std::chrono::steady_clock::now();
    const ScannerProfile profile = ProfileFor(profile_);

    const auto notify_capture_failure = [&](capture::Point anchor,
                                             capture::Rect roi,
                                             std::wstring error,
                                             double capture_ms) {
        if (!completion_callback_) {
            return;
        }
        ScanValidationRecord validation;
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
    const capture::Rect roi = profile.type == ScanProfileType::Inventory
        ? CalculateDirectionalRoi(
            screen_anchor,
            profile.direction_priority.front(),
            DirectionalScanSizeForDepth(
                InventoryScanSizeForLevel(roi_size_, 0),
                0
            ),
            virtual_screen
        )
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
            && ShouldInitializeDirectionalRoi(
                job.tooltip_expansion_attempt,
                job.adaptive_expansion_count
            )) {
            const ScanDirection direction = profile.direction_priority[job.direction_index];
            job.roi = CalculateDirectionalRoi(
                job.screen_anchor,
                direction,
                DirectionalScanSizeForDepth(
                    InventoryScanSizeForLevel(roi_size_, job.scan_level),
                    job.search_depth
                ),
                job.virtual_screen
            );
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
            }
            jobs_available_.notify_one();
            common::DebugLog(
                L"[scan-step] queued_next level=" + std::to_wstring(next_level)
                + L" direction=" + ScanDirectionName(direction)
                + L" searchDepth=" + std::to_wstring(next_search_depth)
            );
            return true;
        };

        const auto capture_start = std::chrono::steady_clock::now();
        job.capture_result = capture_backend_.Capture(job.roi);
        job.capture_ms = ElapsedMilliseconds(capture_start);
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
            + L" hotkey_to_capture_ready_ms="
            + FormatMeasurement(hotkey_to_capture_ready_ms)
        );
        if (!job.capture_result.Succeeded()) {
            const bool queued_next = enqueue_next_inventory_step();
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
        job.total_captured_pixels += static_cast<std::size_t>(
            job.capture_result.frame.width
        ) * job.capture_result.frame.height;

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

        std::optional<TooltipBoxCandidate> tooltip_box_candidate;
        std::optional<ocr::TextBox> tooltip_region_for_frame;
        if (job.profile == ScanProfileType::Inventory) {
            tooltip_box_candidate = DetectTooltipBox(
                job.capture_result.frame,
                job.anchor
            );
            common::DebugLog(
                std::wstring(L"[tooltip-search] direction=")
                + ScanDirectionName(current_direction)
                + L" searchDepth=" + std::to_wstring(job.search_depth)
                + L" roi=(" + std::to_wstring(job.roi.left)
                + L"," + std::to_wstring(job.roi.top) + L")-("
                + std::to_wstring(job.roi.right) + L"," + std::to_wstring(job.roi.bottom)
                + L") candidate=" + (tooltip_box_candidate.has_value() ? L"true" : L"false")
                + (tooltip_box_candidate.has_value()
                    ? L" border_conf="
                        + FormatMeasurement(tooltip_box_candidate->borderConfidence)
                        + L" proximity="
                        + FormatMeasurement(tooltip_box_candidate->proximityToCursor)
                        + L" rect=" + BoxText(tooltip_box_candidate->rect)
                        + L" full_box="
                        + (tooltip_box_candidate->FullBox() ? L"true" : L"false")
                        + L" has_left_border="
                        + (tooltip_box_candidate->hasLeftBorder ? L"true" : L"false")
                        + L" has_right_border="
                        + (tooltip_box_candidate->hasRightBorder ? L"true" : L"false")
                        + L" has_top_border="
                        + (tooltip_box_candidate->hasTopBorder ? L"true" : L"false")
                        + L" has_bottom_border="
                        + (tooltip_box_candidate->hasBottomBorder ? L"true" : L"false")
                        + L" clipped_left="
                        + (tooltip_box_candidate->clippedLeft ? L"true" : L"false")
                        + L" clipped_right="
                        + (tooltip_box_candidate->clippedRight ? L"true" : L"false")
                        + L" clipped_top="
                        + (tooltip_box_candidate->clippedTop ? L"true" : L"false")
                        + L" clipped_bottom="
                        + (tooltip_box_candidate->clippedBottom ? L"true" : L"false")
                    : L"")
            );
            if (tooltip_box_candidate.has_value()) {
                job.direction_locked = true;
                common::DebugLog(
                    std::wstring(L"[scan-step] direction=")
                    + ScanDirectionName(current_direction)
                    + L" textEvidence=false tooltipEvidence=true directionLocked=true"
                    + L" action=expand_and_recognize"
                );
            }
            if (tooltip_box_candidate.has_value()) {
                if (tooltip_box_candidate->FullBox()) {
                    long crop_left = 0;
                    long crop_top = 0;
                    capture::CapturedFrame cropped = CropFrame(
                        job.capture_result.frame,
                        tooltip_box_candidate->rect,
                        6,
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
                        common::DebugLog(
                            L"[tooltip-search] full_box=true action=ocr_inside"
                            L" padding=6"
                        );
                    }
                } else {
                    common::DebugLog(
                        L"[tooltip-search] action=locked_state_machine"
                    );
                }
            } else {
                common::DebugLog(L"[tooltip-search] action=text_fallback");
            }
        }

        const ocr::DetectionResult detection = text_detector_.Detect(job.capture_result.frame);
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
        if (job.profile == ScanProfileType::Inventory && job.direction_locked) {
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
        const ocr::RecognitionResult recognition = text_recognizer_.Recognize(
            job.capture_result.frame,
            recognition_boxes
        );
        common::DebugLog(
            std::wstring(L"[tooltip] candidate=")
            + (tooltip_region_for_frame.has_value() ? L"true" : L"false")
            + (tooltip_region_for_frame.has_value()
                ? L" bounds=" + BoxText(*tooltip_region_for_frame)
                : L"")
        );
        const double hotkey_to_text_ms = ElapsedMilliseconds(job.hotkey_start);

        ScanValidationRecord validation;
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
            job.locked_stage = LockedScanStage::CatalogMatch;
        }
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

        const auto add_ordered_hypothesis = [&](const std::string& text,
                                                 const ocr::TextBox& box,
                                                 const std::vector<std::size_t>& indices,
                                                 float ocr_confidence,
                                                 const wchar_t* kind) {
            if (text.empty()) {
                return;
            }
            const auto duplicate = std::find_if(
                matched_texts.begin(),
                matched_texts.end(),
                [&](const MatchedText& matched) {
                    return matched.recognized.text == text;
                }
            );
            if (duplicate != matched_texts.end()) {
                return;
            }
            const std::vector<data::ItemMatch> matches = item_catalog_.Match(
                text,
                5,
                profile.catalog_threshold
            );
            if (matches.empty()) {
                return;
            }
            const data::ItemMatch& match = matches.front();
            const float coverage = evidence_coverage(match);
            common::DebugLog(
                L"[catalog-fit] hypothesis=" + std::wstring(kind)
                + L" candidate=\"" + Utf8ToWide(match.matchedAlias)
                + L"\" item_id=" + Utf8ToWide(match.item->id)
                + L" similarity=" + FormatMeasurement(match.score)
                + L" coverage=" + FormatMeasurement(coverage)
                + L" ambiguous=" + (match.ambiguous ? L"true" : L"false")
            );
            if (!item_catalog_.IsConfidentMatch(text, match)) {
                return;
            }
            matched_texts.push_back(MatchedText{
                ocr::RecognizedText{box, text, ocr_confidence},
                match,
                indices,
                1.0F,
                true,
                coverage,
            });
        };
        for (const OrderedTextLine& line : text_assembly.lines) {
            add_ordered_hypothesis(
                line.text,
                line.combinedBox,
                line.boxIndices,
                line.ocrConfidence,
                L"line"
            );
        }
        add_ordered_hypothesis(
            text_assembly.completeText,
            text_assembly.combinedBox,
            text_assembly.orderedBoxIndices,
            text_assembly.ocrConfidence,
            L"complete"
        );
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
                    if (left->match.ambiguous != right->match.ambiguous) {
                        return !left->match.ambiguous;
                    }
                    if (left->evidenceCoverage != right->evidenceCoverage) {
                        return left->evidenceCoverage > right->evidenceCoverage;
                    }
                    if (left->match.score != right->match.score) {
                        return left->match.score > right->match.score;
                    }
                    return left->recognized.confidence
                        > right->recognized.confidence;
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
                    + L" ambiguous="
                    + (fit.match.ambiguous ? L"true" : L"false")
                    + L" rank=" + std::to_wstring(rank + 1)
                );
            }
        }
        const double matching_ms = ElapsedMilliseconds(matching_start);
        validation.catalog_valid_candidate_count = static_cast<std::size_t>(std::count_if(
            matched_texts.begin(),
            matched_texts.end(),
            [](const MatchedText& matched) { return !matched.match.ambiguous; }
        ));
        validation.catalog_matching_ms = matching_ms;

        const auto selection_start = std::chrono::steady_clock::now();
        const ScanResult spatial_result = candidate_selector_.Select(
            job.profile,
            job.anchor,
            matched_texts,
            tooltip_region_for_frame
        );
        const double selection_ms = ElapsedMilliseconds(selection_start);
        validation.spatial_selection_ms = selection_ms;

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
            const std::wstring ranking_reason = profile.type == ScanProfileType::Inventory
                ? L"best_unambiguous_catalog_fit_in_locked_block"
                : L"legacy_ring_direction_order";
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

            const auto economy_start = std::chrono::steady_clock::now();
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
                    L"[economy] mode=" + std::wstring(data::GameModeName(job.game_mode))
                    + L" item_id=" + Utf8ToWide(item_id) + L" unavailable"
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
                    L"[economy] mode=" + std::wstring(data::GameModeName(job.game_mode))
                    + L" item_id=" + Utf8ToWide(item_id)
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
        } else {
            common::DebugLog(L"[scan] selected none");
        }
        if (job.profile == ScanProfileType::Inventory && job.direction_locked) {
            common::DebugLog(
                L"[locked-scan] stage="
                + std::wstring(spatial_result.selected.has_value()
                    ? LockedScanStageName(LockedScanStage::Complete)
                    : LockedScanStageName(LockedScanStage::Failed))
                + L" action="
                + (spatial_result.selected.has_value()
                    ? L"return_item" : L"return_no_result")
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
        } else if (matched_texts.empty()) {
            validation.classification = ScanClassification::NoCatalogMatch;
        } else if (!spatial_result.selected.has_value()) {
            validation.classification = ScanClassification::NoSpatialCandidate;
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
            job.profile == ScanProfileType::Inventory && text_evidence
                ? std::optional<AdaptiveTextAnalysis>{adaptive_analysis}
                : std::nullopt,
            debug_locked_roi_steps,
            capture::Point{job.roi.left, job.roi.top},
            text_assembly
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

        if (!spatial_result.selected.has_value() && job.direction_locked) {
            common::DebugLog(
                L"[scan-step] direction="
                + std::wstring(ScanDirectionName(current_direction))
                + L" directionLocked=true action=return_no_result"
            );
        } else if (!spatial_result.selected.has_value()
            && enqueue_next_inventory_step()) {
            common::DebugLog(
                L"[scan-step] no acceptable item; continuing progressive capture"
            );
            continue;
        }

        common::DebugLog(
            L"[scan-output] finalItemSelected="
            + std::wstring(spatial_result.selected.has_value() ? L"true" : L"false")
            + L" displayResultBuilt="
            + (display_result.has_value() ? L"true" : L"false")
        );
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
