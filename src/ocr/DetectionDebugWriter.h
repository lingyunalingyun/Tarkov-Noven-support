#pragma once

#include "capture/CaptureTypes.h"
#include "ocr/OcrTypes.h"

#include <filesystem>
#include <vector>

namespace noven::ocr {

bool WriteAnnotatedBmp(
    const std::filesystem::path& path,
    const capture::CapturedFrame& frame,
    const std::vector<TextBox>& boxes,
    std::wstring& error
);

} // namespace noven::ocr
