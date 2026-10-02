#pragma once

#include "data/MapAssetStore.h"

namespace noven::data {

struct MapAssetCompositionTile final {
    std::string outputPath, sourcePath;
    std::uint32_t width{}, height{}, tileSize{};
    int tileX{}, tileY{};
    double left{}, top{}, right{}, bottom{};
    bool over{};
};
bool ParseMapAssetComposition(std::istream& input, std::vector<MapAssetCompositionTile>& rows, std::string& error);

struct MapAssetCompositionReport final {
    std::filesystem::path generationRoot;
    std::vector<std::string> rebuiltPaths;
    bool cancelled{};
    std::string error;
};

// 仅供单个后台工作线程：待重拼源跨启动保存，整代发布后才切换 current 指针。
// One background worker only: retain dirty sources across startups; switch current after whole-generation publication.
class MapAssetComposer final {
public:
    static constexpr std::uint32_t MaxSources = 4096, TileSize = 512, PreviewEdge = 1024;
    static constexpr std::uint32_t MaxDimension = 8192;
    static constexpr std::uint64_t MaxPixels = 64 * 1024 * 1024;
    static constexpr std::size_t MaxRows = 100000, MaxManifestBytes = 32 * 1024 * 1024;

    MapAssetComposer(MapAssetStore sourceStore, std::filesystem::path generationsRoot);
    MapAssetCompositionReport Rebuild(const std::filesystem::path& localComposition,
        std::span<const std::string> changedSources, std::stop_token stop = {}) const;
    std::optional<std::filesystem::path> CurrentGeneration() const;

private:
    MapAssetStore sources_;
    std::filesystem::path generationsRoot_;
};

} // namespace noven::data
