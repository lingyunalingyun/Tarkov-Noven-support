#include "capture/Roi.h"

#include <algorithm>

namespace noven::capture {

Rect CalculateRoi(Point cursor, Size requested_size, Rect virtual_screen) {
    const long requested_width = std::max(1L, requested_size.width);
    const long requested_height = std::max(1L, requested_size.height);
    const long left = cursor.x - requested_width / 2;
    const long top = cursor.y - requested_height / 2;

    return Rect{
        std::max(left, virtual_screen.left),
        std::max(top, virtual_screen.top),
        std::min(left + requested_width, virtual_screen.right),
        std::min(top + requested_height, virtual_screen.bottom),
    };
}

} // namespace noven::capture
