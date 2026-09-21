#include "data/DataRefreshService.h"

#include "common/DebugLog.h"
#include "data/ItemEconomyStore.h"

#include <windows.h>
#include <winhttp.h>

#include <cstdint>
#include <chrono>
#include <string>
#include <utility>

namespace noven::data {

namespace {

constexpr DWORD kTimeoutMilliseconds = 5000;
constexpr std::size_t kMaximumResponseBytes = 32U * 1024U * 1024U;

class WinHttpHandle final {
public:
    explicit WinHttpHandle(HINTERNET handle = nullptr) : handle_(handle) {}
    ~WinHttpHandle() {
        if (handle_ != nullptr) {
            WinHttpCloseHandle(handle_);
        }
    }

    WinHttpHandle(const WinHttpHandle&) = delete;
    WinHttpHandle& operator=(const WinHttpHandle&) = delete;

    [[nodiscard]] HINTERNET Get() const noexcept { return handle_; }
    [[nodiscard]] explicit operator bool() const noexcept { return handle_ != nullptr; }

private:
    HINTERNET handle_{};
};

std::wstring CachePath(const std::filesystem::path& directory, GameMode mode) {
    return (directory / (std::wstring(UpstreamGameMode(mode)) + L".json")).wstring();
}

} // namespace

DataRefreshService::DataRefreshService(ItemEconomyStore& store) : store_(store) {}

DataRefreshService::~DataRefreshService() {
    Stop();
}

void DataRefreshService::Start(std::filesystem::path cache_directory) {
    if (started_) {
        return;
    }
    cache_directory_ = std::move(cache_directory);
    std::error_code directory_error;
    std::filesystem::create_directories(cache_directory_, directory_error);
    if (directory_error) {
        common::DebugLog(L"[economy] cache directory creation failed");
    }
    const auto cache_load_start = std::chrono::steady_clock::now();
    for (const GameMode mode : AllGameModes()) {
        std::wstring error;
        if (store_.LoadCacheFile(mode, CachePath(cache_directory_, mode), error)) {
            common::DebugLog(
                L"[economy] loaded mode=" + std::wstring(GameModeName(mode))
                + L" items=" + std::to_wstring(store_.ItemCount(mode))
            );
        } else {
            common::DebugLog(
                L"[economy] no usable disk cache mode=" + std::wstring(GameModeName(mode))
                + L" reason=" + error
            );
        }
    }
    common::DebugLog(
        L"[economy] disk cache load ms="
        + std::to_wstring(std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - cache_load_start).count())
    );

    {
        std::lock_guard lock(queue_mutex_);
        stopping_ = false;
        started_ = true;
        for (const GameMode mode : AllGameModes()) {
            queue_.push_back(mode);
        }
    }
    worker_thread_ = std::thread(&DataRefreshService::WorkerLoop, this);
    queue_available_.notify_one();
}

void DataRefreshService::RequestRefresh(GameMode mode) {
    if (!started_) {
        return;
    }
    {
        std::lock_guard lock(queue_mutex_);
        if (stopping_) {
            return;
        }
        queue_.push_back(mode);
    }
    queue_available_.notify_one();
}

void DataRefreshService::Stop() {
    {
        std::lock_guard lock(queue_mutex_);
        if (!started_ && !worker_thread_.joinable()) {
            return;
        }
        stopping_ = true;
    }
    queue_available_.notify_one();
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    started_ = false;
}

void DataRefreshService::WorkerLoop() {
    while (true) {
        GameMode mode{};
        {
            std::unique_lock lock(queue_mutex_);
            queue_available_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (queue_.empty() && stopping_) {
                return;
            }
            mode = queue_.front();
            queue_.pop_front();
        }
        RefreshOne(mode);
    }
}

void DataRefreshService::RefreshOne(GameMode mode) {
    std::string payload;
    std::wstring error;
    if (!DownloadItems(mode, payload, error)) {
        common::DebugLog(
            L"[economy] refresh failed mode=" + std::wstring(GameModeName(mode))
            + L" reason=" + error
        );
        return;
    }
    if (!store_.ReplaceFromUpstreamJson(mode, payload, error)) {
        common::DebugLog(
            L"[economy] rejected upstream payload mode=" + std::wstring(GameModeName(mode))
            + L" reason=" + error
        );
        return;
    }
    if (!store_.SaveCacheFile(mode, CachePath(cache_directory_, mode), error)) {
        common::DebugLog(
            L"[economy] cache save failed mode=" + std::wstring(GameModeName(mode))
            + L" reason=" + error
        );
        return;
    }
    common::DebugLog(
        L"[economy] refreshed mode=" + std::wstring(GameModeName(mode))
        + L" items=" + std::to_wstring(store_.ItemCount(mode))
    );
}

bool DataRefreshService::DownloadItems(
    GameMode mode,
    std::string& payload,
    std::wstring& error
) const {
    WinHttpHandle session(WinHttpOpen(
        L"NovenTarkovSupport/0.1",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    ));
    if (!session) {
        error = L"WinHttpOpen failed";
        return false;
    }
    WinHttpSetTimeouts(
        session.Get(),
        kTimeoutMilliseconds,
        kTimeoutMilliseconds,
        kTimeoutMilliseconds,
        kTimeoutMilliseconds
    );
    WinHttpHandle connection(WinHttpConnect(
        session.Get(), L"json.tarkov.dev", INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!connection) {
        error = L"WinHttpConnect failed";
        return false;
    }
    const std::wstring path = L"/" + std::wstring(UpstreamGameMode(mode)) + L"/items";
    WinHttpHandle request(WinHttpOpenRequest(
        connection.Get(), L"GET", path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (!request) {
        error = L"WinHttpOpenRequest failed";
        return false;
    }
    if (!WinHttpSendRequest(
        request.Get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0
    ) || !WinHttpReceiveResponse(request.Get(), nullptr)) {
        error = L"WinHTTP request failed";
        return false;
    }
    DWORD status_code = 0;
    DWORD status_size = sizeof(status_code);
    if (!WinHttpQueryHeaders(
        request.Get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &status_size,
        WINHTTP_NO_HEADER_INDEX
    ) || status_code != 200) {
        error = L"Economy endpoint returned HTTP " + std::to_wstring(status_code);
        return false;
    }

    payload.clear();
    DWORD available = 0;
    while (WinHttpQueryDataAvailable(request.Get(), &available) && available > 0) {
        if (payload.size() + available > kMaximumResponseBytes) {
            error = L"Economy response exceeds the maximum size";
            return false;
        }
        const std::size_t old_size = payload.size();
        payload.resize(old_size + available);
        DWORD read = 0;
        if (!WinHttpReadData(
            request.Get(), payload.data() + old_size, available, &read)) {
            error = L"WinHttpReadData failed";
            return false;
        }
        payload.resize(old_size + read);
        if (read == 0) {
            break;
        }
    }
    if (payload.empty()) {
        error = L"Economy endpoint returned an empty response";
        return false;
    }
    return true;
}

} // namespace noven::data
