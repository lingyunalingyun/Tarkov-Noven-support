#pragma once

#include "capture/ICaptureBackend.h"
#include "data/ItemCatalog.h"
#include "data/GameMode.h"
#include "data/ItemEconomyStore.h"
#include "ocr/TextDetector.h"
#include "ocr/TextRecognizer.h"
#include "overlay/OverlayTypes.h"
#include "scanner/AdaptiveTextExpansion.h"
#include "scanner/SpatialCandidateSelector.h"
#include "scanner/TooltipHeuristic.h"

#include <chrono>
#include <cstdint>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace noven::scanner {

struct ScanValidationRecord final {
    std::uint64_t scan_id{};
    std::chrono::steady_clock::time_point hotkey_start{};
    data::GameMode game_mode{data::GameMode::Pvp};
    ScanProfileType profile{ScanProfileType::Inventory};
    capture::Point anchor{};
    capture::Rect roi{};
    std::size_t detected_box_count{};
    std::size_t recognized_box_count{};
    std::size_t catalog_valid_candidate_count{};
    std::string selected_item_id;
    std::string selected_display_name;
    std::string selected_ocr_text;
    float selected_ocr_confidence{};
    float selected_match_score{};
    ScanDirection selected_direction{ScanDirection::Up};
    float selected_distance{};
    bool economy_lookup_succeeded{};
    bool overlay_show_succeeded{};
    ScanClassification classification{ScanClassification::CaptureFailed};
    std::wstring error;

    double capture_ms{};
    double detector_preprocess_ms{};
    double detector_inference_ms{};
    double detector_postprocess_ms{};
    double recognition_preprocess_ms{};
    double recognition_inference_ms{};
    double recognition_decode_ms{};
    double catalog_matching_ms{};
    double spatial_selection_ms{};
    double economy_lookup_ms{};
    double overlay_update_render_ms{};
    double total_hotkey_to_overlay_ms{};
};

struct ScanCompletion final {
    std::optional<overlay::ScanDisplayResult> display_result;
    ScanValidationRecord validation;
    std::filesystem::path spatial_debug_image_path;
};

class ScanTrigger final {
public:
    ScanTrigger(
        capture::ICaptureBackend& capture_backend,
        ocr::TextDetector& text_detector,
        ocr::TextRecognizer& text_recognizer,
        data::ItemCatalog& item_catalog,
        data::ItemEconomyStore& economy_store
    );
    ~ScanTrigger();

    ScanTrigger(const ScanTrigger&) = delete;
    ScanTrigger& operator=(const ScanTrigger&) = delete;

    void SetOutputDirectory(std::filesystem::path output_directory);
    void SetRoiSize(capture::Size roi_size);
    void SetProfile(ScanProfileType profile);
    void SetGameMode(data::GameMode mode);
    void SetCompletionCallback(std::function<void(ScanCompletion)> callback);
    void SetScanStepCallback(std::function<void(capture::Rect)> callback);
    void Start();
    void Trigger(bool previous_overlay_visible = false);

private:
    struct ScanJob final {
        capture::CaptureResult capture_result;
        std::filesystem::path raw_output_path;
        std::filesystem::path detected_output_path;
        std::filesystem::path spatial_output_path;
        std::chrono::steady_clock::time_point hotkey_start;
        capture::Point screen_anchor;
        capture::Rect roi;
        double capture_ms{};
        AnchorPoint anchor;
        ScanProfileType profile{};
        data::GameMode game_mode{data::GameMode::Pvp};
        int scan_level{};
        std::size_t direction_index{};
        int search_depth{};
        int tooltip_expansion_attempt{};
        bool direction_locked{};
        capture::Rect virtual_screen{};
        int adaptive_expansion_count{};
        std::size_t total_captured_pixels{};
        LockedScanStage locked_stage{LockedScanStage::HorizontalExpansion};
        std::vector<LockedRoiStep> locked_roi_steps;
        InventoryRecognitionPath inventory_path{
            InventoryRecognitionPath::PrimaryTooltip
        };
        TooltipPlacement tooltip_placement{TooltipPlacement::DefaultRightUpper};
        std::uint64_t scan_id{};
        capture::Rect monitor_bounds{};
        bool capture_guard_pending{};
    };

    void WorkerLoop();

    capture::ICaptureBackend& capture_backend_;
    ocr::TextDetector& text_detector_;
    ocr::TextRecognizer& text_recognizer_;
    data::ItemCatalog& item_catalog_;
    data::ItemEconomyStore& economy_store_;
    std::function<void(ScanCompletion)> completion_callback_;
    std::function<void(capture::Rect)> scan_step_callback_;
    std::filesystem::path output_directory_;
    capture::Size roi_size_{800, 600};
    ScanProfileType profile_{ScanProfileType::Inventory};
    data::GameMode game_mode_{data::GameMode::Pvp};
    SpatialCandidateSelector candidate_selector_;
    unsigned long capture_number_{};
    std::uint64_t next_scan_id_{};
    std::mutex jobs_mutex_;
    std::condition_variable jobs_available_;
    std::deque<ScanJob> jobs_;
    bool stopping_{};
    bool started_{};
    std::thread worker_thread_;
};

} // namespace noven::scanner
