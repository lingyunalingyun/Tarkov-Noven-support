#include "ocr/TextRecognizer.h"

#include "common/DebugLog.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <numeric>
#include <sstream>

namespace noven::ocr {

namespace {

double ElapsedMilliseconds(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start
    ).count();
}

long ClampCoordinate(float value, long upper_bound) {
    return std::clamp(static_cast<long>(std::floor(value)), 0L, upper_bound);
}

} // namespace

bool TextRecognizer::Initialize(
    const std::filesystem::path& runtime_dll,
    const std::filesystem::path& model_path,
    const std::filesystem::path& dictionary_path,
    std::wstring& error
) {
    if (!session_.Initialize(runtime_dll, model_path, error)) {
        return false;
    }
    return LoadDictionary(dictionary_path, error);
}

bool TextRecognizer::LoadDictionary(
    const std::filesystem::path& path,
    std::wstring& error
) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = L"Could not open OCR character dictionary: " + path.wstring();
        return false;
    }

    dictionary_.clear();
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        dictionary_.push_back(std::move(line));
    }

    if (dictionary_.empty()) {
        error = L"OCR character dictionary is empty: " + path.wstring();
        return false;
    }
    if (dictionary_.front().size() >= 3
        && static_cast<unsigned char>(dictionary_.front()[0]) == 0xEF
        && static_cast<unsigned char>(dictionary_.front()[1]) == 0xBB
        && static_cast<unsigned char>(dictionary_.front()[2]) == 0xBF) {
        dictionary_.front().erase(0, 3);
    }
    return true;
}

bool TextRecognizer::WarmUp(std::wstring& error) {
    std::vector<float> input(
        static_cast<std::size_t>(kInputHeight * kInputWidth * 3),
        0.0F
    );
    const std::int64_t shape[] = {1, 3, kInputHeight, kInputWidth};
    OnnxTensor output;
    return session_.Run(input, shape, output, error);
}

RecognitionResult TextRecognizer::Recognize(
    const capture::CapturedFrame& frame,
    std::span<const TextBox> boxes
) const {
    RecognitionResult result;
    const auto total_start = std::chrono::steady_clock::now();
    std::vector<float> input;
    const std::int64_t shape[] = {1, 3, kInputHeight, kInputWidth};

    for (const TextBox& box : boxes) {
        const auto crop_start = std::chrono::steady_clock::now();
        if (!Preprocess(frame, box, input, result.error)) {
            return result;
        }
        result.timings.crop_preprocessing_ms += ElapsedMilliseconds(crop_start);

        OnnxTensor output;
        const auto inference_start = std::chrono::steady_clock::now();
        if (!session_.Run(input, shape, output, result.error)) {
            return result;
        }
        result.timings.inference_ms += ElapsedMilliseconds(inference_start);

        if (result.texts.empty()) {
            std::wostringstream output_info;
            output_info << L"[ocr] recognizer_output_shape=";
            for (std::size_t dimension : output.shape) {
                output_info << dimension << L"x";
            }
            output_info << L" values=" << output.data.size();
            if (!output.data.empty()) {
                const auto maximum = std::max_element(output.data.begin(), output.data.end());
                const auto minimum = std::min_element(output.data.begin(), output.data.end());
                output_info << L" min=" << *minimum << L" max=" << *maximum;
            }
            common::DebugLog(output_info.str());
        }

        const auto decode_start = std::chrono::steady_clock::now();
        float confidence = 0.0F;
        std::string text = Decode(output, confidence);
        result.timings.decode_ms += ElapsedMilliseconds(decode_start);
        result.texts.push_back(RecognizedText{box, std::move(text), confidence});
    }

    result.timings.total_ms = ElapsedMilliseconds(total_start);
    return result;
}

bool TextRecognizer::Preprocess(
    const capture::CapturedFrame& frame,
    const TextBox& box,
    std::vector<float>& input,
    std::wstring& error
) {
    const std::size_t frame_size = static_cast<std::size_t>(frame.stride) * frame.height;
    if (frame.width == 0 || frame.height == 0 || frame.stride < frame.width * 4
        || frame.bgra.size() < frame_size) {
        error = L"Captured frame has invalid data for OCR recognition preprocessing";
        return false;
    }

    const long max_x = static_cast<long>(frame.width);
    const long max_y = static_cast<long>(frame.height);
    const long left = ClampCoordinate(box.x1, max_x - 1);
    const long top = ClampCoordinate(box.y1, max_y - 1);
    const long right = std::clamp(
        static_cast<long>(std::ceil(box.x2)), left + 1, max_x
    );
    const long bottom = std::clamp(
        static_cast<long>(std::ceil(box.y2)), top + 1, max_y
    );
    const long crop_width = right - left;
    const long crop_height = bottom - top;
    if (crop_width <= 0 || crop_height <= 0) {
        error = L"Detected OCR box has no pixels";
        return false;
    }

    const std::size_t plane_size = static_cast<std::size_t>(kInputHeight * kInputWidth);
    input.assign(plane_size * 3, 0.0F);
    const auto resized_width = static_cast<std::int64_t>(std::clamp(
        std::ceil(static_cast<double>(crop_width) * kInputHeight / crop_height),
        1.0,
        static_cast<double>(kInputWidth)
    ));

    for (std::int64_t y = 0; y < kInputHeight; ++y) {
        const float source_y = (static_cast<float>(y) + 0.5F)
            * static_cast<float>(crop_height) / static_cast<float>(kInputHeight) - 0.5F;
        const long y0 = std::clamp(static_cast<long>(std::floor(source_y)), 0L, crop_height - 1);
        const long y1 = std::min(y0 + 1, crop_height - 1);
        const float y_fraction = std::clamp(source_y - static_cast<float>(y0), 0.0F, 1.0F);
        for (std::int64_t x = 0; x < resized_width; ++x) {
            const float source_x = (static_cast<float>(x) + 0.5F)
                * static_cast<float>(crop_width) / static_cast<float>(resized_width) - 0.5F;
            const long x0 = std::clamp(static_cast<long>(std::floor(source_x)), 0L, crop_width - 1);
            const long x1 = std::min(x0 + 1, crop_width - 1);
            const float x_fraction = std::clamp(source_x - static_cast<float>(x0), 0.0F, 1.0F);

            const auto sample = [&](long sample_x, long sample_y, int channel) {
                const std::size_t offset = static_cast<std::size_t>(top + sample_y) * frame.stride
                    + static_cast<std::size_t>(left + sample_x) * 4;
                // PaddleOCR's recognition model expects BGR channel order.
                return static_cast<float>(frame.bgra[offset + static_cast<std::size_t>(channel)])
                    / 255.0F;
            };
            const std::size_t destination = static_cast<std::size_t>(y) * kInputWidth
                + static_cast<std::size_t>(x);
            for (int channel = 0; channel < 3; ++channel) {
                const float top_value = sample(x0, y0, channel)
                    + (sample(x1, y0, channel) - sample(x0, y0, channel)) * x_fraction;
                const float bottom_value = sample(x0, y1, channel)
                    + (sample(x1, y1, channel) - sample(x0, y1, channel)) * x_fraction;
                const float value = top_value + (bottom_value - top_value) * y_fraction;
                input[static_cast<std::size_t>(channel) * plane_size + destination]
                    = (value - 0.5F) / 0.5F;
            }
        }
    }
    return true;
}

std::string TextRecognizer::Decode(const OnnxTensor& output, float& confidence) const {
    // 兼容类别轴在前/在后的输出；CTC blank 与连续重复字符不写入结果。
    // Accept either class-axis layout; omit CTC blanks and consecutive repeats.
    confidence = 0.0F;
    if (output.shape.size() < 2 || output.data.empty() || dictionary_.empty()) {
        return {};
    }

    const std::size_t second_last = static_cast<std::size_t>(
        output.shape[output.shape.size() - 2]
    );
    const std::size_t last = static_cast<std::size_t>(output.shape.back());
    // 字典启用 use_space_char 时，CTCLabelDecode 在末尾增加 ASCII 空格类别。
    // CTCLabelDecode appends an ASCII-space class when use_space_char is enabled.
    const std::size_t dictionary_class_count = dictionary_.size() + 1;
    const std::size_t dictionary_with_space_class_count = dictionary_.size() + 2;
    const bool has_appended_space = last == dictionary_with_space_class_count
        || second_last == dictionary_with_space_class_count;
    const std::size_t expected_class_count = has_appended_space
        ? dictionary_with_space_class_count
        : dictionary_class_count;
    const bool class_last = last == expected_class_count;
    const bool class_first = second_last == expected_class_count;
    if ((!class_last && !class_first) || output.data.size() < second_last * last) {
        return {};
    }

    const std::size_t time_steps = class_last ? second_last : last;
    const std::size_t class_count = expected_class_count;
    const auto value_at = [&](std::size_t timestep, std::size_t class_index) {
        return class_last
            ? output.data[timestep * class_count + class_index]
            : output.data[class_index * time_steps + timestep];
    };

    std::string text;
    std::size_t previous_class = 0;
    float confidence_sum = 0.0F;
    std::size_t emitted_count = 0;
    for (std::size_t timestep = 0; timestep < time_steps; ++timestep) {
        std::size_t best_class = 0;
        float best_value = value_at(timestep, 0);
        for (std::size_t class_index = 1; class_index < class_count; ++class_index) {
            if (value_at(timestep, class_index) > best_value) {
                best_class = class_index;
                best_value = value_at(timestep, class_index);
            }
        }

        float row_confidence = best_value;
        if (best_value < 0.0F || best_value > 1.0F) {
            const float maximum = best_value;
            float denominator = 0.0F;
            for (std::size_t class_index = 0; class_index < class_count; ++class_index) {
                denominator += std::exp(value_at(timestep, class_index) - maximum);
            }
            row_confidence = denominator > 0.0F
                ? std::exp(best_value - maximum) / denominator
                : 0.0F;
        }

        if (best_class != 0 && best_class != previous_class) {
            const std::size_t dictionary_index = best_class - 1;
            if (dictionary_index < dictionary_.size()) {
                text += dictionary_[dictionary_index];
                confidence_sum += row_confidence;
                ++emitted_count;
            } else if (has_appended_space && dictionary_index == dictionary_.size()) {
                text.push_back(' ');
                confidence_sum += row_confidence;
                ++emitted_count;
            }
        }
        previous_class = best_class;
    }
    confidence = emitted_count == 0
        ? 0.0F
        : confidence_sum / static_cast<float>(emitted_count);
    return text;
}

} // namespace noven::ocr
