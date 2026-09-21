#pragma once

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
