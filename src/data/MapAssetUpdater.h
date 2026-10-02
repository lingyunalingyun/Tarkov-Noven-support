#pragma once

#include "data/MapAssetStore.h"
#include <functional>
#include <mutex>
#include <thread>

namespace noven::data {

struct MapAssetHttpRequest final {
    std::string url;
    MapAssetValidators validators;
};
struct MapAssetHttpResponse final {
    unsigned status{};
    std::vector<std::uint8_t> bytes;
    MapAssetValidators validators;
};
using MapAssetTransport = std::function<MapAssetHttpResponse(const MapAssetHttpRequest&, std::stop_token)>;
struct MapAssetUpdateReport final {
    std::size_t updated{}, unchanged{}, failed{};
    bool cancelled{}, alreadyStarted{}, finished{};
    std::string error;
    // 仅表示源 PNG 已提交；衍生 atlas/.tiles 重建及图像缓存失效由调用方负责。
    // These are committed source PNGs only; callers rebuild derived atlases/tiles and invalidate image caches.
    std::vector<std::string> updatedPaths;
};

// 一次启动检查，无轮询、绘制回调或 UI 所有权；Stop 必须在销毁前 join。
// One startup check, with no polling, drawing callbacks or UI ownership; Stop joins before destruction.
class MapAssetUpdater final {
public:
    static constexpr std::size_t MaxWorkers = 4;
    // 自定义传输可能同时被四个 worker 调用，调用方负责其捕获状态的同步。
    // Custom transports may be called by four workers concurrently; synchronize captured state.
    explicit MapAssetUpdater(MapAssetStore store, MapAssetTransport transport = {});
    ~MapAssetUpdater();
    MapAssetUpdater(const MapAssetUpdater&) = delete;
    MapAssetUpdater& operator=(const MapAssetUpdater&) = delete;

    bool StartOnce(std::filesystem::path localManifest);
    void Stop();
    MapAssetUpdateReport Report() const;
    // 同步入口与后台入口共用一次启动保护，供离线测试/无 UI 工具使用。
    // Synchronous and background entry points share the once guard, for offline tests/headless tools.
    MapAssetUpdateReport CheckOnce(const std::filesystem::path& localManifest, std::stop_token stop = {});

private:
    MapAssetUpdateReport Run(const std::filesystem::path& manifest, std::stop_token stop);
    MapAssetStore store_;
    MapAssetTransport transport_;
    mutable std::mutex mutex_;
    bool started_{};
    MapAssetUpdateReport report_;
    std::jthread worker_;
};

} // namespace noven::data
