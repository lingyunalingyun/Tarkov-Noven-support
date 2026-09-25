#pragma once

// ONNX Runtime 细节封装于会话中；检测和识别模块只交换张量数据。
// Encapsulate ONNX Runtime details in a session; detector and recognizer exchange tensors.

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace noven::ocr {

struct OnnxTensor final {
    std::vector<float> data;
    std::vector<std::int64_t> shape;
};

class OnnxRuntimeSession final {
public:
    OnnxRuntimeSession();
    ~OnnxRuntimeSession();

    OnnxRuntimeSession(const OnnxRuntimeSession&) = delete;
    OnnxRuntimeSession& operator=(const OnnxRuntimeSession&) = delete;

    bool Initialize(
        const std::filesystem::path& runtime_dll,
        const std::filesystem::path& model_path,
        std::wstring& error
    );

    bool Run(
        std::span<const float> input,
        std::span<const std::int64_t> input_shape,
        OnnxTensor& output,
        std::wstring& error
    ) const;

    [[nodiscard]] bool IsInitialized() const noexcept;

private:
    struct Impl;
    Impl* impl_{};
};

} // namespace noven::ocr
