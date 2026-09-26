#include "ui/ItemImageCache.h"
#include "common/DebugLog.h"

#include <winhttp.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <chrono>
#include <fstream>

namespace noven::ui {
namespace {
constexpr std::size_t kMaxImageBytes = 2 * 1024 * 1024;
struct HttpHandle final {
    HINTERNET value{};
    ~HttpHandle() { if (value) WinHttpCloseHandle(value); }
};

bool Download(const std::string& id, std::vector<unsigned char>& bytes) {
    HttpHandle session{WinHttpOpen(L"NovenTarkovSupport/0.1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.value) return false;
    WinHttpSetTimeouts(session.value, 2000, 2000, 2000, 2000);
    HttpHandle connection{WinHttpConnect(session.value, L"assets.tarkov.dev",
        INTERNET_DEFAULT_HTTPS_PORT, 0)};
    if (!connection.value) return false;
    const std::wstring path = L"/" + std::wstring(id.begin(), id.end()) + L"-icon.webp";
    HttpHandle request{WinHttpOpenRequest(connection.value, L"GET", path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)};
    if (!request.value || !WinHttpSendRequest(request.value, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(request.value, nullptr))
        return false;
    DWORD status{}, size = sizeof(status);
    if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX) || status != 200)
        return false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    bytes.clear();
    unsigned char buffer[16384];
    for (;;) {
        DWORD read{};
        if (std::chrono::steady_clock::now() > deadline
            || !WinHttpReadData(request.value, buffer, sizeof(buffer), &read)) return false;
        if (!read) return !bytes.empty();
        if (bytes.size() + read > kMaxImageBytes) return false;
        bytes.insert(bytes.end(), buffer, buffer + read);
    }
}

std::vector<unsigned char> ReadImage(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) return {};
    const auto size = input.tellg();
    if (size <= 0 || size > static_cast<std::streamoff>(kMaxImageBytes)) return {};
    std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), size)) return {};
    return bytes;
}
} // namespace

bool ItemImageCache::ValidId(const std::string& id) noexcept {
    return id.size() == 24 && std::all_of(id.begin(), id.end(), [](char ch) {
        return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
    });
}

bool ItemImageCache::Decode(const std::vector<unsigned char>& bytes, ItemImage& image) {
    if (bytes.empty() || bytes.size() > kMaxImageBytes) return false;
    using Microsoft::WRL::ComPtr;
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICBitmapScaler> scaler;
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&factory))) || FAILED(factory->CreateStream(&stream))
        || FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(bytes.data()), static_cast<DWORD>(bytes.size())))
        || FAILED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder))
        || FAILED(decoder->GetFrame(0, &frame))) return false;
    UINT width{}, height{};
    if (FAILED(frame->GetSize(&width, &height)) || !width || !height || width > 4096 || height > 4096)
        return false;
    const float scale = (std::min)(1.0F, 192.0F / static_cast<float>((std::max)(width, height)));
    width = (std::max)(1U, static_cast<UINT>(width * scale));
    height = (std::max)(1U, static_cast<UINT>(height * scale));
    if (FAILED(factory->CreateBitmapScaler(&scaler))
        || FAILED(scaler->Initialize(frame.Get(), width, height, WICBitmapInterpolationModeFant))
        || FAILED(factory->CreateFormatConverter(&converter))
        || FAILED(converter->Initialize(scaler.Get(), GUID_WICPixelFormat32bppPBGRA,
            WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) return false;
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height * 4);
    if (FAILED(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data())))
        return false;
    image.width = width;
    image.height = height;
    image.pixels = std::move(pixels);
    return true;
}

ItemImageCache::~ItemImageCache() { Stop(); }

void ItemImageCache::Start(HWND window, std::filesystem::path directory) {
    window_ = window;
    directory_ = std::move(directory);
    worker_ = std::thread([this] { Run(); });
}

void ItemImageCache::Stop() {
    { std::lock_guard lock(mutex_); stopping_ = true; }
    changed_.notify_one();
    if (worker_.joinable()) worker_.join();
}

void ItemImageCache::Request(const std::string& id) {
    if (!ValidId(id)) return;
    std::lock_guard lock(mutex_);
    if (stopping_ || !requested_.insert(id).second) return;
    pending_.push_back(id);
    changed_.notify_one();
}

std::vector<ItemImage> ItemImageCache::TakeReady() {
    std::lock_guard lock(mutex_);
    std::vector<ItemImage> result;
    result.swap(ready_);
    return result;
}

void ItemImageCache::Forget(const std::string& id) {
    std::lock_guard lock(mutex_);
    requested_.erase(id);
}

void ItemImageCache::Run() {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(com)) return;
    std::error_code error;
    std::filesystem::create_directories(directory_, error);
    for (;;) {
        std::string id;
        {
            std::unique_lock lock(mutex_);
            changed_.wait(lock, [this] { return stopping_ || !pending_.empty(); });
            if (stopping_) break;
            id = std::move(pending_.front());
            pending_.pop_front();
        }
        const auto path = directory_ / (id + ".webp");
        auto bytes = ReadImage(path);
        ItemImage image;
        image.id = id;
        bool ok = Decode(bytes, image);
        if (!ok && Download(id, bytes) && Decode(bytes, image)) {
            ok = true;
            const auto temporary = directory_ / (id + ".tmp");
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            output.flush();
            const bool written = output.good();
            output.close();
            if (written) MoveFileExW(temporary.c_str(), path.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        }
        common::DebugLog(L"[item-image] id=" + std::wstring(id.begin(), id.end())
            + (ok ? L" ready=true" : L" ready=false reason=download_or_decode_failed"));
        std::lock_guard lock(mutex_);
        if (stopping_) break;
        if (ok) {
            ready_.push_back(std::move(image));
            PostMessageW(window_, kReadyMessage, 0, 0);
        }
    }
    CoUninitialize();
}

} // namespace noven::ui
