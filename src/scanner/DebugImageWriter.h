#pragma once

#include "capture/CaptureTypes.h"

#include <filesystem>

namespace noven::scanner {

bool WriteDebugBmp(
    const std::filesystem::path& path,
    const capture::CapturedFrame& frame,
    std::wstring& error
);

} // namespace noven::scanner
