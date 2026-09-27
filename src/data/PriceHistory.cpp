#include "data/PriceHistory.h"
#include "common/DebugLog.h"
#include <winhttp.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <map>

namespace noven::data {
namespace {
constexpr std::size_t kMaxBytes = 16 * 1024 * 1024;
std::int64_t Now() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}
struct InternetHandle {
    HINTERNET value{};
    ~InternetHandle() { if (value) WinHttpCloseHandle(value); }
};
bool Download(const std::string& id, GameMode mode, std::string& result) {
    InternetHandle session{WinHttpOpen(L"NovenTarkovSupport/0.1",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, 0)};
    if (!session.value) return false;
    WinHttpSetTimeouts(session.value, 5000, 5000, 5000, 5000);
    InternetHandle connection{WinHttpConnect(session.value, L"json.tarkov.dev",
        INTERNET_DEFAULT_HTTPS_PORT, 0)};
    if (!connection.value) return false;
    const std::wstring path = L"/" + std::wstring(UpstreamGameMode(mode))
        + L"/prices/" + std::wstring(id.begin(), id.end());
    InternetHandle request{WinHttpOpenRequest(connection.value, L"GET", path.c_str(),
        nullptr, nullptr, nullptr, WINHTTP_FLAG_SECURE)};
    if (!request.value || !WinHttpSendRequest(request.value, nullptr, 0, nullptr, 0, 0, 0)
        || !WinHttpReceiveResponse(request.value, nullptr)) return false;
    DWORD status{}, size = sizeof(status);
    if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        nullptr, &status, &size, nullptr) || status != 200) return false;
    result.clear();
    const auto started = std::chrono::steady_clock::now();
    while (true) {
        if (std::chrono::steady_clock::now() - started > std::chrono::seconds(20)) return false;
        char buffer[16384];
        DWORD read{};
        if (!WinHttpReadData(request.value, buffer, sizeof(buffer), &read)) return false;
        if (!read) break;
        if (result.size() + read > kMaxBytes) return false;
        result.append(buffer, read);
    }
    return !result.empty();
}
std::filesystem::path CacheFile(const std::filesystem::path& directory,
    const std::string& id, GameMode mode) {
    return directory / (std::string(UpstreamGameModeCode(mode)) + "-" + id + ".json");
}
bool OwnedFile(const std::filesystem::path& file) {
    const auto utf8 = file.filename().u8string();
    const std::string name(utf8.begin(), utf8.end());
    for (const auto mode : AllGameModes()) {
        const auto prefix = std::string(UpstreamGameModeCode(mode)) + "-";
        if (name.starts_with(prefix) && name.ends_with(".json")
            && ValidHistoryId(name.substr(prefix.size(), name.size() - prefix.size() - 5))) return true;
    }
    return false;
}
void PruneDirectory(const std::filesystem::path& directory) {
    std::error_code error;
    for (std::filesystem::directory_iterator it(directory, error), end; !error && it != end; it.increment(error)) {
        if (!it->is_regular_file(error) || !OwnedFile(it->path())) continue;
        std::vector<HistoryPoint> points;
        if (!LoadHistory(it->path(), points)) continue;
        auto recent = HistoryRange(points, Now(), kHistoryRetentionDays);
        if (recent.size() != points.size()) SaveHistory(it->path(), recent, Now());
    }
}
}

bool ValidHistoryId(const std::string& id) noexcept {
    return id.size() == 24 && std::all_of(id.begin(), id.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}

bool ParseHistory(std::string_view json, std::vector<HistoryPoint>& points) {
    using namespace winrt::Windows::Data::Json;
    points.clear();
    try {
        const auto root = JsonObject::Parse(winrt::to_hstring(json));
        if (root.HasKey(L"schemaVersion") && root.GetNamedNumber(L"schemaVersion") != 1) return false;
        const auto rows = root.GetNamedArray(L"data");
        if (rows.Size() > 100000) return false;
        std::map<std::int64_t, std::int64_t> ordered;
        for (const auto& value : rows) {
            const auto row = value.GetObject();
            const double time = row.GetNamedNumber(L"timestamp");
            const double price = row.GetNamedNumber(L"price");
            // 零价/负价/非整数不是有效采样；缺失数据不能当作零绘图。
            // Zero/negative/fractional values are not valid samples; missing data is never plotted as zero.
            if (!std::isfinite(time) || !std::isfinite(price) || time <= 0 || price <= 0
                || time > 9007199254740991.0 || price > 9007199254740991.0
                || std::floor(time) != time || std::floor(price) != price) continue;
            ordered[static_cast<std::int64_t>(time)] = static_cast<std::int64_t>(price);
        }
        for (const auto& [time, price] : ordered) points.push_back({time, price});
        return true;
    } catch (...) { return false; }
}

std::vector<HistoryPoint> HistoryRange(const std::vector<HistoryPoint>& points, std::int64_t now, int days) {
    std::vector<HistoryPoint> result;
    const auto cutoff = now - static_cast<std::int64_t>(days) * kHistoryDayMs;
    for (const auto& point : points)
        if (point.timeMs >= cutoff && point.timeMs <= now && point.price > 0) result.push_back(point);
    return result;
}

bool LoadHistory(const std::filesystem::path& file, std::vector<HistoryPoint>& points) {
    std::error_code error;
    const auto size = std::filesystem::file_size(file, error);
    if (error || size > kMaxBytes) return false;
    std::ifstream stream(file, std::ios::binary);
    const std::string json{std::istreambuf_iterator<char>(stream), {}};
    return stream.good() && ParseHistory(json, points);
}

bool SaveHistory(const std::filesystem::path& file, const std::vector<HistoryPoint>& points,
    std::int64_t now) {
    const auto recent = HistoryRange(points, now, kHistoryRetentionDays);
    std::string json = "{\"schemaVersion\":1,\"data\":[";
    for (std::size_t i = 0; i < recent.size(); ++i) {
        if (i) json += ',';
        json += "{\"timestamp\":" + std::to_string(recent[i].timeMs)
            + ",\"price\":" + std::to_string(recent[i].price) + "}";
    }
    json += "]}";
    auto temp = file; temp += L".tmp";
    const HANDLE handle = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    DWORD written{};
    const bool ok = WriteFile(handle, json.data(), static_cast<DWORD>(json.size()), &written, nullptr)
        && written == json.size() && FlushFileBuffers(handle);
    CloseHandle(handle);
    if (!ok) { DeleteFileW(temp.c_str()); return false; }
    if (MoveFileExW(temp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
    DeleteFileW(temp.c_str());
    return false;
}

void PriceHistoryService::Start(HWND window, std::filesystem::path directory) {
    if (worker_.joinable()) return;
    window_ = window; directory_ = std::move(directory); stopping_ = false;
    worker_ = std::thread([this] { Run(); });
}
void PriceHistoryService::Stop() {
    { std::lock_guard lock(mutex_); stopping_ = true; pending_.reset(); ready_.reset(); }
    wake_.notify_one();
    if (worker_.joinable()) worker_.join();
}
void PriceHistoryService::Request(std::string id, GameMode mode, int days) {
    if (!ValidHistoryId(id) || (days != 7 && days != 30 && days != 90 && days != 365)) return;
    {
        std::lock_guard lock(mutex_);
        if (stopping_ || !worker_.joinable()) return;
        pending_ = RequestInfo{std::move(id), mode, days, ++generation_};
        ready_.reset();
    }
    wake_.notify_one();
}
std::optional<HistorySnapshot> PriceHistoryService::TakeReady() {
    std::lock_guard lock(mutex_);
    auto result = std::move(ready_); ready_.reset(); return result;
}
void PriceHistoryService::Publish(const RequestInfo& request, HistorySnapshot snapshot) {
    {
        std::lock_guard lock(mutex_);
        if (stopping_ || request.generation != generation_) return;
        ready_ = std::move(snapshot);
    }
    PostMessageW(window_, kReadyMessage, 0, 0);
}
void PriceHistoryService::Run() {
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        struct Apartment { ~Apartment() { winrt::uninit_apartment(); } } apartment;
        std::error_code error;
        std::filesystem::create_directories(directory_, error);
        PruneDirectory(directory_);
        auto lastPrune = std::chrono::steady_clock::now();
        std::string memoryId;
        GameMode memoryMode{};
        std::int64_t memoryTime{};
        std::vector<HistoryPoint> memoryPoints;
        for (;;) {
            RequestInfo request;
            {
                std::unique_lock lock(mutex_);
                wake_.wait_for(lock, std::chrono::hours(1), [this] { return stopping_ || pending_.has_value(); });
                if (stopping_) return;
                if (!pending_) { lock.unlock(); PruneDirectory(directory_); continue; }
                request = std::move(*pending_); pending_.reset();
            }
            if (std::chrono::steady_clock::now() - lastPrune >= std::chrono::hours(1)) {
                PruneDirectory(directory_); lastPrune = std::chrono::steady_clock::now();
            }
            const auto file = CacheFile(directory_, request.id, request.mode);
            HistorySnapshot snapshot{request.id, request.mode, request.days, Now()};
            // 同一卡片切换区间复用短期内存，既不重复下载，也不把旧数据写入磁盘。
            // Range switches reuse a short-lived in-memory response without redownloading or persisting old data.
            if (memoryId == request.id && memoryMode == request.mode
                && snapshot.asOfMs - memoryTime < 5 * 60 * 1000) {
                snapshot.points = HistoryRange(memoryPoints, snapshot.asOfMs, request.days);
                snapshot.loading = false;
                Publish(request, std::move(snapshot));
                continue;
            }
            memoryPoints.clear(); memoryId.clear();
            std::vector<HistoryPoint> cached;
            if (LoadHistory(file, cached)) {
                cached = HistoryRange(cached, snapshot.asOfMs, kHistoryRetentionDays);
                SaveHistory(file, cached, snapshot.asOfMs);
                snapshot.points = HistoryRange(cached, snapshot.asOfMs, request.days);
                snapshot.cached = !snapshot.points.empty();
                Publish(request, snapshot);
            }
            std::string payload;
            std::vector<HistoryPoint> received;
            const bool downloaded = downloader_ ? downloader_(request.id, request.mode, payload)
                : Download(request.id, request.mode, payload);
            if (downloaded && ParseHistory(payload, received)) {
                snapshot.asOfMs = Now();
                // 仅新接口采样更新同时间点；断网缓存不会被空响应丢弃。
                // Fresh samples replace identical timestamps; empty replies do not discard cached history.
                std::map<std::int64_t, std::int64_t> merged;
                for (const auto& p : cached) merged[p.timeMs] = p.price;
                for (const auto& p : received) merged[p.timeMs] = p.price;
                received.clear();
                for (const auto& [time, price] : merged) received.push_back({time, price});
                snapshot.points = HistoryRange(received, snapshot.asOfMs, request.days);
                snapshot.cached = false;
                if (!SaveHistory(file, received, snapshot.asOfMs))
                    common::DebugLog(L"[price-history] cache write failed");
                memoryId = request.id; memoryMode = request.mode; memoryTime = snapshot.asOfMs;
                memoryPoints = std::move(received);
            } else {
                snapshot.failed = true;
                common::DebugLog(L"[price-history] download or parse failed");
            }
            snapshot.loading = false;
            Publish(request, std::move(snapshot));
        }
    } catch (...) { common::DebugLog(L"[price-history] worker initialization failed"); }
}
}
