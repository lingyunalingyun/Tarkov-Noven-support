#pragma once

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
