#pragma once

#include "capture/CaptureTypes.h"

namespace noven::capture {

[[nodiscard]] Rect CalculateRoi(Point cursor, Size requested_size, Rect virtual_screen);

} // namespace noven::capture
