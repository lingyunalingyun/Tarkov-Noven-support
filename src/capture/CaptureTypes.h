#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace noven::capture {

struct Point final {
    long x{};
    long y{};
};

struct Size final {
    long width{};
    long height{};
};

struct Rect final {
    long left{};
    long top{};
    long right{};
    long bottom{};

    [[nodiscard]] long Width() const noexcept { return right - left; }
    [[nodiscard]] long Height() const noexcept { return bottom - top; }
    [[nodiscard]] bool Empty() const noexcept {
        return Width() <= 0 || Height() <= 0;
    }
};

enum class PixelFormat {
    Bgra8,
};

struct CaptureTimings final {
    double acquire_ms{};
    double roi_copy_ms{};
    double format_conversion_ms{};
    double capture_to_memory_ms{};
};

enum class CaptureSource {
    DxgiNewFrame,
    DxgiCachedFrame,
    GdiEmergency,
};

struct CapturedFrame final {
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t stride{};
    PixelFormat pixel_format{PixelFormat::Bgra8};
    std::vector<std::uint8_t> bgra;
};

struct CaptureResult final {
    CapturedFrame frame;
    CaptureTimings timings;
    CaptureSource source{CaptureSource::GdiEmergency};
    std::uint32_t dxgi_new_frame_count{};
    std::uint32_t dxgi_cached_frame_count{};
    std::uint32_t gdi_emergency_count{};
    std::wstring error;

    [[nodiscard]] bool Succeeded() const noexcept {
        return error.empty() && frame.width > 0 && frame.height > 0;
    }
};

} // namespace noven::capture
