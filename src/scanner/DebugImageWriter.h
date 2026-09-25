#pragma once

// 仅将单次内存捕获写成验证用 BMP；生产识别不依赖图像文件。
// Write a single in-memory capture as a validation BMP; production recognition
// does not depend on image files.

#include "capture/CaptureTypes.h"

#include <filesystem>

namespace noven::scanner {

bool WriteDebugBmp(
    const std::filesystem::path& path,
    const capture::CapturedFrame& frame,
    std::wstring& error
);

} // namespace noven::scanner
