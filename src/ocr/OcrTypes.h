#pragma once

// OCR 坐标相对输入的捕获 ROI；每个识别框独立保留以供局部分组。
// OCR coordinates are local to the captured ROI; retain each box for local grouping.

#include <cstdint>
#include <string>
#include <vector>

namespace noven::ocr {

struct TextBox final {
    float x1{};
    float y1{};
    float x2{};
    float y2{};
    float confidence{};
};

struct DetectionTimings final {
    double preprocessing_ms{};
    double inference_ms{};
    double postprocessing_ms{};
    double total_ms{};
};

struct DetectionResult final {
    std::vector<TextBox> boxes;
    DetectionTimings timings;
    std::wstring error;

    [[nodiscard]] bool Succeeded() const noexcept { return error.empty(); }
};

struct RecognizedText final {
    // 不把整个画面的文字合并成一个查询，以免无关 UI 文本混入物品名。
    // Never concatenate all screen text into one query; unrelated UI text must stay separate.
    TextBox box;
    std::string text;
    float confidence{};
};

struct RecognitionTimings final {
    double crop_preprocessing_ms{};
    double inference_ms{};
    double decode_ms{};
    double total_ms{};
};

struct RecognitionResult final {
    std::vector<RecognizedText> texts;
    RecognitionTimings timings;
    std::wstring error;

    [[nodiscard]] bool Succeeded() const noexcept {
        return error.empty();
    }
};

} // namespace noven::ocr
