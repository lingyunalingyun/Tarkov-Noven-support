#pragma once

// PP-OCR 检测阶段把内存像素预处理为模型输入，再还原 ROI 局部文字框。
// PP-OCR detection preprocesses in-memory pixels and maps output boxes back to ROI coordinates.

#include "capture/CaptureTypes.h"
#include "ocr/OcrTypes.h"
#include "ocr/OnnxRuntimeSession.h"

#include <filesystem>

namespace noven::ocr {

struct TextDetectorConfig final {
    float pixel_threshold{0.30F};
    float box_threshold{0.60F};
};

class TextDetector final {
public:
    explicit TextDetector(TextDetectorConfig config = {});

    bool Initialize(
        const std::filesystem::path& runtime_dll,
        const std::filesystem::path& model_path,
        std::wstring& error
    );

    bool WarmUp(std::wstring& error);
    DetectionResult Detect(const capture::CapturedFrame& frame) const;

private:
    static constexpr std::int64_t kInputWidth = 640;
    static constexpr std::int64_t kInputHeight = 640;

    static bool Preprocess(
        const capture::CapturedFrame& frame,
        std::vector<float>& input,
        std::wstring& error
    );

    // 模型概率图经阈值/框过滤后输出文字区域，不执行文字识别。
    // Threshold and filter the model probability map into text regions; no recognition here.
    static std::vector<TextBox> Postprocess(
        const OnnxTensor& output,
        std::uint32_t source_width,
        std::uint32_t source_height,
        const TextDetectorConfig& config
    );

    TextDetectorConfig config_;
    OnnxRuntimeSession session_;
};

} // namespace noven::ocr
