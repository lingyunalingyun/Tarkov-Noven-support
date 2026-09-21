#include "ocr/OnnxRuntimeSession.h"

#include "common/DebugLog.h"

#include <onnxruntime_c_api.h>

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>

namespace noven::ocr {

namespace {

using GetApiBase = const OrtApiBase* (ORT_API_CALL*)();

std::wstring Utf8ToWide(const char* text) {
    if (text == nullptr || *text == '\0') {
        return L"unknown ONNX Runtime error";
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    if (size <= 1) {
        return L"unknown ONNX Runtime error";
    }
    std::wstring result(static_cast<std::size_t>(size - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text, -1, result.data(), size);
    return result;
}

bool CheckStatus(
    const OrtApi* api,
    OrtStatus* status,
    const wchar_t* operation,
    std::wstring& error
) {
    if (status == nullptr) {
        return true;
    }
    error = std::wstring(operation) + L": " + Utf8ToWide(api->GetErrorMessage(status));
    api->ReleaseStatus(status);
    return false;
}

} // namespace

struct OnnxRuntimeSession::Impl final {
    HMODULE runtime_module{};
    const OrtApi* api{};
    OrtEnv* environment{};
    OrtSessionOptions* options{};
    OrtSession* session{};
    OrtMemoryInfo* memory_info{};
    OrtAllocator* allocator{};
    std::string input_name;
    std::string output_name;

    ~Impl() {
        if (api != nullptr) {
            if (memory_info != nullptr) {
                api->ReleaseMemoryInfo(memory_info);
            }
            if (session != nullptr) {
                api->ReleaseSession(session);
            }
            if (options != nullptr) {
                api->ReleaseSessionOptions(options);
            }
            if (environment != nullptr) {
                api->ReleaseEnv(environment);
            }
        }
        if (runtime_module != nullptr) {
            FreeLibrary(runtime_module);
        }
    }
};

OnnxRuntimeSession::OnnxRuntimeSession()
    : impl_(new Impl()) {}

OnnxRuntimeSession::~OnnxRuntimeSession() {
    delete impl_;
}

bool OnnxRuntimeSession::Initialize(
    const std::filesystem::path& runtime_dll,
    const std::filesystem::path& model_path,
    std::wstring& error
) {
    if (impl_->session != nullptr) {
        return true;
    }

    impl_->runtime_module = LoadLibraryW(runtime_dll.c_str());
    if (impl_->runtime_module == nullptr) {
        error = L"Could not load ONNX Runtime DLL: " + runtime_dll.wstring()
            + L" (Win32 error=" + std::to_wstring(GetLastError()) + L")";
        return false;
    }

    const FARPROC api_proc = GetProcAddress(impl_->runtime_module, "OrtGetApiBase");
    GetApiBase get_api_base{};
    static_assert(sizeof(get_api_base) == sizeof(api_proc));
    std::memcpy(&get_api_base, &api_proc, sizeof(get_api_base));
    if (get_api_base == nullptr) {
        error = L"ONNX Runtime DLL does not export OrtGetApiBase";
        return false;
    }

    const OrtApiBase* api_base = get_api_base();
    if (api_base == nullptr || api_base->GetApi(ORT_API_VERSION) == nullptr) {
        error = L"ONNX Runtime API version is unavailable";
        return false;
    }
    impl_->api = api_base->GetApi(ORT_API_VERSION);

    if (!CheckStatus(
        impl_->api,
        impl_->api->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "NovenTarkovSupport", &impl_->environment),
        L"CreateEnv",
        error
    )) {
        return false;
    }
    if (!CheckStatus(
        impl_->api,
        impl_->api->CreateSessionOptions(&impl_->options),
        L"CreateSessionOptions",
        error
    )) {
        return false;
    }
    if (!CheckStatus(
        impl_->api,
        impl_->api->SetIntraOpNumThreads(impl_->options, 1),
        L"SetIntraOpNumThreads",
        error
    )) {
        return false;
    }
    if (!CheckStatus(
        impl_->api,
        impl_->api->SetSessionGraphOptimizationLevel(impl_->options, ORT_ENABLE_ALL),
        L"SetSessionGraphOptimizationLevel",
        error
    )) {
        return false;
    }
    if (!CheckStatus(
        impl_->api,
        impl_->api->CreateSession(
            impl_->environment,
            model_path.c_str(),
            impl_->options,
            &impl_->session
        ),
        L"CreateSession",
        error
    )) {
        return false;
    }
    if (!CheckStatus(
        impl_->api,
        impl_->api->GetAllocatorWithDefaultOptions(&impl_->allocator),
        L"GetAllocatorWithDefaultOptions",
        error
    )) {
        return false;
    }

    char* input_name = nullptr;
    if (!CheckStatus(
        impl_->api,
        impl_->api->SessionGetInputName(impl_->session, 0, impl_->allocator, &input_name),
        L"SessionGetInputName",
        error
    )) {
        return false;
    }
    impl_->input_name = input_name;
    impl_->api->AllocatorFree(impl_->allocator, input_name);

    char* output_name = nullptr;
    if (!CheckStatus(
        impl_->api,
        impl_->api->SessionGetOutputName(impl_->session, 0, impl_->allocator, &output_name),
        L"SessionGetOutputName",
        error
    )) {
        return false;
    }
    impl_->output_name = output_name;
    impl_->api->AllocatorFree(impl_->allocator, output_name);

    if (!CheckStatus(
        impl_->api,
        impl_->api->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &impl_->memory_info),
        L"CreateCpuMemoryInfo",
        error
    )) {
        return false;
    }

    common::DebugLog(L"[ocr] ONNX Runtime CPU session initialized: " + model_path.wstring());
    return true;
}

bool OnnxRuntimeSession::Run(
    std::span<const float> input,
    std::span<const std::int64_t> input_shape,
    OnnxTensor& output,
    std::wstring& error
) const {
    if (!IsInitialized()) {
        error = L"ONNX Runtime session is not initialized";
        return false;
    }

    OrtValue* input_value = nullptr;
    if (!CheckStatus(
        impl_->api,
        impl_->api->CreateTensorWithDataAsOrtValue(
            impl_->memory_info,
            const_cast<float*>(input.data()),
            input.size_bytes(),
            input_shape.data(),
            input_shape.size(),
            ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
            &input_value
        ),
        L"CreateTensorWithDataAsOrtValue",
        error
    )) {
        return false;
    }

    const char* input_names[] = {impl_->input_name.c_str()};
    const char* output_names[] = {impl_->output_name.c_str()};
    OrtValue* output_values[] = {nullptr};
    const OrtValue* input_values[] = {input_value};
    const OrtStatus* run_status = impl_->api->Run(
        impl_->session,
        nullptr,
        input_names,
        input_values,
        1,
        output_names,
        1,
        output_values
    );
    impl_->api->ReleaseValue(input_value);
    if (!CheckStatus(impl_->api, const_cast<OrtStatus*>(run_status), L"Run", error)) {
        return false;
    }

    OrtTensorTypeAndShapeInfo* shape_info = nullptr;
    bool success = CheckStatus(
        impl_->api,
        impl_->api->GetTensorTypeAndShape(output_values[0], &shape_info),
        L"GetTensorTypeAndShape",
        error
    );
    if (success) {
        ONNXTensorElementDataType element_type{};
        success = CheckStatus(
            impl_->api,
            impl_->api->GetTensorElementType(shape_info, &element_type),
            L"GetTensorElementType",
            error
        ) && element_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT;
        if (!success && error.empty()) {
            error = L"ONNX detector output is not float32";
        }
    }
    if (success) {
        size_t dimension_count = 0;
        success = CheckStatus(
            impl_->api,
            impl_->api->GetDimensionsCount(shape_info, &dimension_count),
            L"GetDimensionsCount",
            error
        );
        if (success) {
            output.shape.resize(dimension_count);
            success = CheckStatus(
                impl_->api,
                impl_->api->GetDimensions(shape_info, output.shape.data(), dimension_count),
                L"GetDimensions",
                error
            );
            size_t element_count = 0;
            if (success) {
                success = CheckStatus(
                    impl_->api,
                    impl_->api->GetTensorShapeElementCount(shape_info, &element_count),
                    L"GetTensorShapeElementCount",
                    error
                );
                if (success) {
                    void* data = nullptr;
                    success = CheckStatus(
                        impl_->api,
                        impl_->api->GetTensorMutableData(output_values[0], &data),
                        L"GetTensorMutableData",
                        error
                    );
                    if (success) {
                        output.data.assign(
                            static_cast<const float*>(data),
                            static_cast<const float*>(data) + element_count
                        );
                    }
                }
            }
        }
    }
    if (shape_info != nullptr) {
        impl_->api->ReleaseTensorTypeAndShapeInfo(shape_info);
    }
    impl_->api->ReleaseValue(output_values[0]);
    return success;
}

bool OnnxRuntimeSession::IsInitialized() const noexcept {
    return impl_ != nullptr && impl_->api != nullptr && impl_->session != nullptr;
}

} // namespace noven::ocr
