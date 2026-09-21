#include "ocr/DetectionDebugWriter.h"

#include "scanner/DebugImageWriter.h"

#include <algorithm>

namespace noven::ocr {

bool WriteAnnotatedBmp(
    const std::filesystem::path& path,
    const capture::CapturedFrame& frame,
    const std::vector<TextBox>& boxes,
    std::wstring& error
) {
    capture::CapturedFrame annotated = frame;
    const auto draw_pixel = [&](long x, long y) {
        if (x < 0 || y < 0 || x >= static_cast<long>(annotated.width)
            || y >= static_cast<long>(annotated.height)) {
            return;
        }
        const std::size_t offset = static_cast<std::size_t>(y) * annotated.stride
            + static_cast<std::size_t>(x) * 4;
        annotated.bgra[offset + 0] = 0;
        annotated.bgra[offset + 1] = 0;
        annotated.bgra[offset + 2] = 255;
        annotated.bgra[offset + 3] = 255;
    };
    for (const TextBox& box : boxes) {
        const long left = static_cast<long>(std::clamp(box.x1, 0.0F,
            static_cast<float>(annotated.width - 1)));
        const long top = static_cast<long>(std::clamp(box.y1, 0.0F,
            static_cast<float>(annotated.height - 1)));
        const long right = static_cast<long>(std::clamp(box.x2 - 1.0F, 0.0F,
            static_cast<float>(annotated.width - 1)));
        const long bottom = static_cast<long>(std::clamp(box.y2 - 1.0F, 0.0F,
            static_cast<float>(annotated.height - 1)));
        for (int thickness = 0; thickness < 2; ++thickness) {
            for (long x = left; x <= right; ++x) {
                draw_pixel(x, top + thickness);
                draw_pixel(x, bottom - thickness);
            }
            for (long y = top; y <= bottom; ++y) {
                draw_pixel(left + thickness, y);
                draw_pixel(right - thickness, y);
            }
        }
    }
    return scanner::WriteDebugBmp(path, annotated, error);
}

} // namespace noven::ocr
