#pragma once

// 将鼠标周围的请求区域裁到虚拟桌面边界；返回值仍为屏幕坐标。
// Clip a cursor-relative request to virtual-desktop bounds; the result remains in screen coordinates.

#include "capture/CaptureTypes.h"

namespace noven::capture {

[[nodiscard]] Rect CalculateRoi(Point cursor, Size requested_size, Rect virtual_screen);

} // namespace noven::capture
