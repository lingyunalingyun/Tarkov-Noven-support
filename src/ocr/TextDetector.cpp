#include "ocr/TextDetector.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <utility>

namespace noven::ocr {

namespace {

double ElapsedMilliseconds(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start
    ).count();
}

std::size_t PixelOffset(
    std::uint32_t width,
    std::uint32_t height,
    const std::vector<float>& output
) {
    if (width == 0 || height == 0) {
        return std::numeric_limits<std::size_t>::max();
    }
    const std::size_t expected = static_cast<std::size_t>(width) * height;
    if (output.size() < expected) {
        return std::numeric_limits<std::size_t>::max();
    }
    return output.size() - expected;
}

} // namespace

TextDetector::TextDetector(TextDetectorConfig config)
    : config_(config) {}

bool TextDetector::Initialize(
    const std::filesystem::path& runtime_dll,
    const std::filesystem::path& model_path,
    std::wstring& error
) {
    return session_.Initialize(runtime_dll, model_path, error);
}

bool TextDetector::WarmUp(std::wstring& error) {
    std::vector<float> input(
        static_cast<std::size_t>(kInputWidth * kInputHeight * 3),
        0.0F
    );
    OnnxTensor output;
    const std::int64_t shape[] = {1, 3, kInputHeight, kInputWidth};
    return session_.Run(input, shape, output, error);
}

DetectionResult TextDetector::Detect(const capture::CapturedFrame& frame) const {
    DetectionResult result;
    const auto total_start = std::chrono::steady_clock::now();

    std::vector<float> input;
    const auto preprocessing_start = std::chrono::steady_clock::now();
    if (!Preprocess(frame, input, result.error)) {
        return result;
    }
    result.timings.preprocessing_ms = ElapsedMilliseconds(preprocessing_start);

    OnnxTensor output;
    const std::int64_t shape[] = {1, 3, kInputHeight, kInputWidth};
    const auto inference_start = std::chrono::steady_clock::now();
    if (!session_.Run(input, shape, output, result.error)) {
        return result;
    }
    result.timings.inference_ms = ElapsedMilliseconds(inference_start);

    const auto postprocessing_start = std::chrono::steady_clock::now();
    result.boxes = Postprocess(output, frame.width, frame.height, config_);
    result.timings.postprocessing_ms = ElapsedMilliseconds(postprocessing_start);
    result.timings.total_ms = ElapsedMilliseconds(total_start);
    return result;
}

bool TextDetector::Preprocess(
    const capture::CapturedFrame& frame,
    std::vector<float>& input,
    std::wstring& error
) {
    const std::size_t source_size = static_cast<std::size_t>(frame.stride) * frame.height;
    if (frame.width == 0 || frame.height == 0 || frame.stride < frame.width * 4
        || frame.bgra.size() < source_size) {
        error = L"Captured frame has invalid data for OCR preprocessing";
        return false;
    }

    const std::size_t plane_size = static_cast<std::size_t>(kInputWidth * kInputHeight);
    input.resize(plane_size * 3);
    for (std::int64_t y = 0; y < kInputHeight; ++y) {
        const float source_y = (static_cast<float>(y) + 0.5F)
            * static_cast<float>(frame.height) / static_cast<float>(kInputHeight) - 0.5F;
        const auto y0 = static_cast<long>(std::clamp(std::floor(source_y), 0.0F,
            static_cast<float>(frame.height - 1)));
        const auto y1 = std::min(y0 + 1, static_cast<long>(frame.height - 1));
        const float y_fraction = std::clamp(source_y - static_cast<float>(y0), 0.0F, 1.0F);
        for (std::int64_t x = 0; x < kInputWidth; ++x) {
            const float source_x = (static_cast<float>(x) + 0.5F)
                * static_cast<float>(frame.width) / static_cast<float>(kInputWidth) - 0.5F;
            const auto x0 = static_cast<long>(std::clamp(std::floor(source_x), 0.0F,
                static_cast<float>(frame.width - 1)));
            const auto x1 = std::min(x0 + 1, static_cast<long>(frame.width - 1));
            const float x_fraction = std::clamp(source_x - static_cast<float>(x0), 0.0F, 1.0F);

            const auto sample = [&](long sample_x, long sample_y, int channel) {
                const std::size_t offset = static_cast<std::size_t>(sample_y) * frame.stride
                    + static_cast<std::size_t>(sample_x) * 4;
                // The detector uses RGB input; the capture buffer is BGRA.
                const std::size_t channel_offset = static_cast<std::size_t>(2 - channel);
                return static_cast<float>(frame.bgra[offset + channel_offset]) / 255.0F;
            };
            const std::size_t destination = static_cast<std::size_t>(y) * kInputWidth + x;
            for (int channel = 0; channel < 3; ++channel) {
                const float top = sample(x0, y0, channel)
                    + (sample(x1, y0, channel) - sample(x0, y0, channel)) * x_fraction;
                const float bottom = sample(x0, y1, channel)
                    + (sample(x1, y1, channel) - sample(x0, y1, channel)) * x_fraction;
                const float rgb = top + (bottom - top) * y_fraction;
                input[static_cast<std::size_t>(channel) * plane_size + destination]
                    = (rgb - 0.5F) / 0.5F;
            }
        }
    }
    return true;
}

std::vector<TextBox> TextDetector::Postprocess(
    const OnnxTensor& output,
    std::uint32_t source_width,
    std::uint32_t source_height,
    const TextDetectorConfig& config
) {
    if (output.shape.size() < 3 || output.data.empty() || source_width == 0
        || source_height == 0) {
        return {};
    }

    const auto height = static_cast<std::size_t>(output.shape[output.shape.size() - 2]);
    const auto width = static_cast<std::size_t>(output.shape.back());
    if (width == 0 || height == 0 || width > 4096 || height > 4096) {
        return {};
    }
    const std::size_t offset = PixelOffset(
        static_cast<std::uint32_t>(width),
        static_cast<std::uint32_t>(height),
        output.data
    );
    if (offset == std::numeric_limits<std::size_t>::max()) {
        return {};
    }

    std::vector<std::uint8_t> visited(width * height, 0);
    std::vector<TextBox> boxes;
    const int neighbor_offsets[] = {-1, 0, 1};
    for (std::size_t y = 0; y < height; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            const std::size_t start = y * width + x;
            if (visited[start] != 0 || output.data[offset + start] < config.pixel_threshold) {
                continue;
            }

            std::deque<std::pair<std::size_t, std::size_t>> queue;
            queue.emplace_back(x, y);
            visited[start] = 1;
            std::size_t min_x = x;
            std::size_t max_x = x;
            std::size_t min_y = y;
            std::size_t max_y = y;
            float confidence_sum = 0.0F;
            std::size_t area = 0;
            while (!queue.empty()) {
                const auto [current_x, current_y] = queue.front();
                queue.pop_front();
                const std::size_t current = current_y * width + current_x;
                confidence_sum += output.data[offset + current];
                ++area;
                min_x = std::min(min_x, current_x);
                max_x = std::max(max_x, current_x);
                min_y = std::min(min_y, current_y);
                max_y = std::max(max_y, current_y);

                for (const int delta_y : neighbor_offsets) {
                    for (const int delta_x : neighbor_offsets) {
                        if (delta_x == 0 && delta_y == 0) {
                            continue;
                        }
                        const auto next_x = static_cast<long>(current_x) + delta_x;
                        const auto next_y = static_cast<long>(current_y) + delta_y;
                        if (next_x < 0 || next_y < 0
                            || next_x >= static_cast<long>(width)
                            || next_y >= static_cast<long>(height)) {
                            continue;
                        }
                        const std::size_t next = static_cast<std::size_t>(next_y) * width
                            + static_cast<std::size_t>(next_x);
                        if (visited[next] == 0 && output.data[offset + next] >= config.pixel_threshold) {
                            visited[next] = 1;
                            queue.emplace_back(
                                static_cast<std::size_t>(next_x),
                                static_cast<std::size_t>(next_y)
                            );
                        }
                    }
                }
            }

            const float confidence = confidence_sum / static_cast<float>(area);
            if (area < 4 || confidence < config.box_threshold) {
                continue;
            }
            boxes.push_back(TextBox{
                static_cast<float>(min_x) * static_cast<float>(source_width) / width,
                static_cast<float>(min_y) * static_cast<float>(source_height) / height,
                static_cast<float>(max_x + 1) * static_cast<float>(source_width) / width,
                static_cast<float>(max_y + 1) * static_cast<float>(source_height) / height,
                confidence,
            });
        }
    }
    return boxes;
}

} // namespace noven::ocr
