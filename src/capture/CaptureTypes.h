#pragma once

// 单次触发的内存捕获契约：虚拟桌面矩形输入，BGRA8 像素输出。
// Single-trigger in-memory capture contract: virtual-desktop rectangle in, BGRA8 pixels out.

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
    // 屏幕坐标允许负值，边界采用左上闭合、右下排他的矩形约定。
    // Screen coordinates may be negative; bounds use a left/top-inclusive,
    // right/bottom-exclusive rectangle.
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

[[nodiscard]] inline bool FrameSafeAfterOverlayHide(
    bool overlay_was_visible,
    CaptureSource source
) noexcept {
    // 先前 Noven 窗口可见时，隐藏前缓存的 DXGI 帧不能作为下一次 OCR 输入。
    // If a previous Noven window was visible, a pre-hide cached DXGI frame
    // must not feed the next OCR scan.
    return !overlay_was_visible || source != CaptureSource::DxgiCachedFrame;
}

struct CapturedFrame final {
    std::uint32_t width{};
    std::uint32_t height{};
    // 每行字节数可能包含填充，不必等于宽度乘四。
    // Row stride may include padding and need not equal width times four.
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
