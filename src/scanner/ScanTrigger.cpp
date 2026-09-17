#include "scanner/ScanTrigger.h"

#include "capture/Roi.h"
#include "common/DebugLog.h"
#include "scanner/DebugImageWriter.h"

#include <windows.h>

#include <chrono>
#include <iomanip>
#include <sstream>
#include <string>

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

} // namespace

ScanTrigger::ScanTrigger(capture::ICaptureBackend& capture_backend)
    : capture_backend_(capture_backend),
      output_directory_(std::filesystem::current_path() / L"debug-captures") {}

void ScanTrigger::SetOutputDirectory(std::filesystem::path output_directory) {
    output_directory_ = std::move(output_directory);
}

void ScanTrigger::SetRoiSize(capture::Size roi_size) {
    roi_size_ = roi_size;
}

void ScanTrigger::Trigger() {
    const auto total_start = std::chrono::steady_clock::now();

    POINT cursor_position{};
    if (!GetCursorPos(&cursor_position)) {
        common::DebugLog(
            L"[capture] GetCursorPos failed (Win32 error="
            + std::to_wstring(GetLastError()) + L")"
        );
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
    const capture::Rect roi = capture::CalculateRoi(cursor, roi_size_, virtual_screen);
    if (roi.Empty()) {
        common::DebugLog(L"[capture] calculated ROI is empty");
        return;
    }

    const auto capture_start = std::chrono::steady_clock::now();
    capture::CaptureResult capture_result = capture_backend_.Capture(roi);
    const double hotkey_to_capture_ready_ms = ElapsedMilliseconds(total_start);
    const double capture_call_ms = ElapsedMilliseconds(capture_start);
    common::DebugLog(
        L"[capture] frame result=" + std::to_wstring(capture_result.frame.width)
        + L"x" + std::to_wstring(capture_result.frame.height)
        + L" error=" + capture_result.error
    );

    if (!capture_result.Succeeded()) {
        common::DebugLog(
            L"[capture] failed: " + capture_result.error
            + L" capture_call_ms=" + FormatMeasurement(capture_call_ms)
            + L" hotkey_to_capture_ready_ms="
            + FormatMeasurement(hotkey_to_capture_ready_ms)
            + L" total_ms=" + FormatMeasurement(ElapsedMilliseconds(total_start))
        );
        return;
    }

    ++capture_number_;
    const std::filesystem::path output_path = output_directory_
        / (L"capture_" + std::to_wstring(capture_number_) + L".bmp");
    std::wstring write_error;
    const auto bmp_write_start = std::chrono::steady_clock::now();
    if (!WriteDebugBmp(output_path, capture_result.frame, write_error)) {
        const double bmp_write_ms = ElapsedMilliseconds(bmp_write_start);
        common::DebugLog(
            L"[capture] image save failed: " + write_error
            + L" bmp_write_ms=" + FormatMeasurement(bmp_write_ms)
            + L" capture_to_memory_ms="
            + FormatMeasurement(capture_result.timings.capture_to_memory_ms)
            + L" total_ms=" + FormatMeasurement(ElapsedMilliseconds(total_start))
        );
        return;
    }
    const double bmp_write_ms = ElapsedMilliseconds(bmp_write_start);
    common::DebugLog(L"[capture] debug image written");

    std::wostringstream message;
    message << L"[capture] cursor=(" << cursor.x << L"," << cursor.y << L")"
            << L" roi=(" << roi.left << L"," << roi.top << L")-("
            << roi.right << L"," << roi.bottom << L")"
            << L" size=" << roi.Width() << L"x" << roi.Height()
            << L" backend=" << capture_backend_.Name()
            << L" acquire_ms=" << FormatMeasurement(capture_result.timings.acquire_ms)
            << L" roi_copy_ms=" << FormatMeasurement(capture_result.timings.roi_copy_ms)
            << L" capture_to_memory_ms="
            << FormatMeasurement(capture_result.timings.capture_to_memory_ms)
            << L" capture_call_ms=" << FormatMeasurement(capture_call_ms)
            << L" bmp_write_ms=" << FormatMeasurement(bmp_write_ms)
            << L" hotkey_to_capture_ready_ms="
            << FormatMeasurement(hotkey_to_capture_ready_ms)
            << L" hotkey_to_file_ms=" << FormatMeasurement(ElapsedMilliseconds(total_start))
            << L" file=capture_" << capture_number_ << L".bmp";
    common::DebugLog(message.str());
}

} // namespace noven::scanner
