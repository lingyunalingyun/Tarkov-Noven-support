#pragma once

// PP-OCR 识别只裁剪检测出的文字框；字典与 CTC 解码均在本地 C++ 完成。
// PP-OCR recognition crops detected boxes only; dictionary and CTC decoding stay in native C++.

#include "capture/CaptureTypes.h"
#include "ocr/OcrTypes.h"
#include "ocr/OnnxRuntimeSession.h"

#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace noven::ocr {

class TextRecognizer final {
public:
    bool Initialize(
        const std::filesystem::path& runtime_dll,
        const std::filesystem::path& model_path,
        const std::filesystem::path& dictionary_path,
        std::wstring& error
    );

    bool WarmUp(std::wstring& error);
    RecognitionResult Recognize(
        const capture::CapturedFrame& frame,
        std::span<const TextBox> boxes
    ) const;

private:
    static constexpr std::int64_t kInputHeight = 48;
    static constexpr std::int64_t kInputWidth = 320;

    bool LoadDictionary(const std::filesystem::path& path, std::wstring& error);
    static bool Preprocess(
        const capture::CapturedFrame& frame,
        const TextBox& box,
        std::vector<float>& input,
        std::wstring& error
    );
    // CTC 解码去掉 blank 和连续重复字符，并保留识别置信度。
    // CTC decoding removes blanks and consecutive repeats while retaining confidence.
    std::string Decode(const OnnxTensor& output, float& confidence) const;

    OnnxRuntimeSession session_;
    std::vector<std::string> dictionary_;
};

} // namespace noven::ocr
