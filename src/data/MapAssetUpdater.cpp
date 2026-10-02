#include "data/MapAssetUpdater.h"

#include <windows.h>
#include <winhttp.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>

namespace noven::data {
namespace {
struct HttpHandle final {
    HINTERNET value{};
    ~HttpHandle() { if (value) WinHttpCloseHandle(value); }
};
std::string Header(HINTERNET request, DWORD query) {
    wchar_t buffer[1025]{};
    DWORD size = sizeof(buffer);
    if (!WinHttpQueryHeaders(request, query, WINHTTP_HEADER_NAME_BY_INDEX,
            buffer, &size, WINHTTP_NO_HEADER_INDEX)) return {};
    std::string result;
    for (const wchar_t c : std::wstring_view(buffer)) {
        if (c < 32 || c > 126 || result.size() == 1024) return {};
        result += static_cast<char>(c);
    }
    return result;
}
struct NativeTransport final {
    HttpHandle session, connection;
MapAssetHttpResponse Download(const MapAssetHttpRequest& input, std::stop_token stop) {
    MapAssetHttpResponse result;
    if (stop.stop_requested() || !ValidMapAssetUrl(input.url)) return result;
    if (!session.value) session.value = WinHttpOpen(L"NovenTarkovSupport/MapAssets", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session.value || !WinHttpSetTimeouts(session.value, 2000, 2000, 2000, 2000)) return result;
    if (!connection.value) connection.value = WinHttpConnect(session.value, L"assets.tarkov.dev", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connection.value || stop.stop_requested()) return result;
    const auto relative = input.url.substr(std::string_view("https://assets.tarkov.dev").size());
    const std::wstring path(relative.begin(), relative.end());
    HttpHandle request{WinHttpOpenRequest(connection.value, L"GET", path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)};
    // 严禁自动重定向与凭据；TLS 保持系统默认验证，不放宽证书检查。
    // Disable redirects and implicit credentials; retain normal system TLS/certificate validation.
    DWORD disabled = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
    DWORD authPolicy = WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
    if (!request.value || !WinHttpSetOption(request.value, WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled))
        || !WinHttpSetOption(request.value, WINHTTP_OPTION_AUTOLOGON_POLICY, &authPolicy, sizeof(authPolicy))) return result;
    std::wstring headers;
    auto append = [&](std::wstring_view name, const std::string& value) {
        if (!value.empty()) headers += std::wstring(name) + std::wstring(value.begin(), value.end()) + L"\r\n";
    };
    append(L"If-None-Match: ", input.validators.etag);
    append(L"If-Modified-Since: ", input.validators.lastModified);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    if (stop.stop_requested() || !WinHttpSendRequest(request.value, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
            static_cast<DWORD>(headers.size()), WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
        || stop.stop_requested() || !WinHttpReceiveResponse(request.value, nullptr)) return result;
    DWORD status{}, size = sizeof(status);
    if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX)) return result;
    result.status = status;
    result.validators = {Header(request.value, WINHTTP_QUERY_ETAG), Header(request.value, WINHTTP_QUERY_LAST_MODIFIED)};
    if (status != 200) return result;
    DWORD length{};
    size = sizeof(length);
    if (WinHttpQueryHeaders(request.value, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &length, &size, WINHTTP_NO_HEADER_INDEX) && length > MapAssetStore::MaxBytes)
        return {};
    unsigned char buffer[16384];
    for (;;) {
        DWORD read{};
        if (stop.stop_requested() || std::chrono::steady_clock::now() > deadline
            || !WinHttpReadData(request.value, buffer, sizeof(buffer), &read)) return {};
        if (!read) return result;
        if (result.bytes.size() + read > MapAssetStore::MaxBytes) return {};
        result.bytes.insert(result.bytes.end(), buffer, buffer + read);
    }
}
};
} // namespace

MapAssetUpdater::MapAssetUpdater(MapAssetStore store, MapAssetTransport transport)
    : store_(std::move(store)), transport_(std::move(transport)) {}
MapAssetUpdater::~MapAssetUpdater() { Stop(); }

bool MapAssetUpdater::StartOnce(std::filesystem::path localManifest) {
    std::lock_guard lock(mutex_);
    if (started_) return false;
    started_ = true;
    worker_ = std::jthread([this, manifest = std::move(localManifest)](std::stop_token stop) {
        auto result = Run(manifest, stop);
        std::lock_guard reportLock(mutex_);
        report_ = std::move(result);
    });
    return true;
}
void MapAssetUpdater::Stop() {
    std::jthread joining;
    {
        std::lock_guard lock(mutex_);
        if (worker_.joinable()) worker_.request_stop();
        joining = std::move(worker_);
    }
    if (joining.joinable()) joining.join();
}
MapAssetUpdateReport MapAssetUpdater::Report() const {
    std::lock_guard lock(mutex_);
    return report_;
}
MapAssetUpdateReport MapAssetUpdater::CheckOnce(const std::filesystem::path& localManifest, std::stop_token stop) {
    {
        std::lock_guard lock(mutex_);
        if (started_) { auto result = report_; result.alreadyStarted = true; return result; }
        started_ = true;
    }
    auto result = Run(localManifest, stop);
    std::lock_guard lock(mutex_);
    report_ = result;
    return result;
}

MapAssetUpdateReport MapAssetUpdater::Run(const std::filesystem::path& manifest, std::stop_token stop) {
    MapAssetUpdateReport report;
    report.finished = true;
    try {
        if (stop.stop_requested()) { report.cancelled = true; return report; }
        std::error_code error;
        const auto size = std::filesystem::file_size(manifest, error);
        if (error || size > 4 * 1024 * 1024) { report.error = "Unreadable/oversized local map asset manifest"; return report; }
        std::ifstream input(manifest, std::ios::binary);
        std::vector<MapAssetSpec> assets;
        if (!ParseMapAssetManifest(input, assets, report.error)) return report;
        enum class Result { Pending, Updated, Unchanged, Failed, Cancelled };
        std::vector<Result> results(assets.size(), Result::Pending);
        std::atomic<std::size_t> next{};
        auto consume = [&] {
            // 每个 worker 独占复用的 HTTP session/connection；结果按独占索引写入，join 后聚合。
            // Each worker owns a reusable HTTP session/connection and exclusive result indices; aggregate after join.
            NativeTransport native;
            while (!stop.stop_requested()) {
                const auto index = next.fetch_add(1);
                if (index >= assets.size()) break;
                const auto& asset = assets[index];
            try {
                auto current = store_.Read(asset);
                // 固定摘要命中时连 HTTP 都跳过；清单变化后不可重用旧版本的条件头。
                // A matching pinned digest skips HTTP; changed manifest hashes cannot reuse old validators.
                if (!asset.sha256.empty()) {
                    if (!current.bytes.empty() && MapAssetStore::Sha256(current.bytes) == asset.sha256) {
                        results[index] = Result::Unchanged;
                        continue;
                    }
                    current.validators = {};
                }
                if (stop.stop_requested()) { results[index] = Result::Cancelled; break; }
                const MapAssetHttpRequest request{asset.url, current.validators};
                const auto response = transport_ ? transport_(request, stop) : native.Download(request, stop);
                if (stop.stop_requested()) { results[index] = Result::Cancelled; break; }
                if (response.status == 304 && !current.bytes.empty()
                    && (!current.validators.etag.empty() || !current.validators.lastModified.empty())) {
                    results[index] = Result::Unchanged;
                    continue;
                }
                if (response.status != 200) { results[index] = Result::Failed; continue; }
                switch (store_.Save(asset, response.bytes, response.validators, stop)) {
                case MapAssetWrite::Updated:
                    results[index] = Result::Updated;
                    break;
                case MapAssetWrite::Unchanged: results[index] = Result::Unchanged; break;
                case MapAssetWrite::Failed: results[index] = Result::Failed; break;
                case MapAssetWrite::Cancelled: results[index] = Result::Cancelled; break;
                }
                if (results[index] == Result::Cancelled) break;
            } catch (...) {
                // 单项失败不终止其它静态资源，也不触碰原有效文件。
                // An individual failure leaves existing files intact and permits other static assets to proceed.
                results[index] = stop.stop_requested() ? Result::Cancelled : Result::Failed;
            }
            }
        };
        {
            std::vector<std::jthread> workers;
            const auto count = (std::min)(MaxWorkers, assets.size());
            for (std::size_t i = 0; i < count; ++i) workers.emplace_back(consume);
        }
        report.cancelled = stop.stop_requested();
        for (std::size_t i = 0; i < results.size(); ++i) {
            switch (results[i]) {
            case Result::Updated: ++report.updated; report.updatedPaths.push_back(assets[i].relativePath); break;
            case Result::Unchanged: ++report.unchanged; break;
            case Result::Failed: ++report.failed; break;
            case Result::Cancelled: report.cancelled = true; break;
            case Result::Pending: break;
            }
        }
    } catch (const std::exception& exception) {
        report.error = exception.what();
    } catch (...) {
        report.error = "Map asset startup check failed";
    }
    return report;
}

} // namespace noven::data
