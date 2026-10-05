#pragma once
#include "plugins/PluginManifest.h"
#include <filesystem>
#include <windows.h>

namespace noven::plugins {
struct PluginRecord final {
    std::filesystem::path directory;
    PluginState state{PluginState::InvalidManifest};
    std::optional<PluginManifest> manifest;
    std::vector<PluginDiagnostic> diagnostics;
};
struct PluginSnapshot final {
    std::vector<PluginRecord> records;
    std::vector<PluginDiagnostic> diagnostics;
};
inline bool UnsafeAttributes(DWORD attributes,bool directory) {
    return (attributes&FILE_ATTRIBUTE_REPARSE_POINT)!=0||((attributes&FILE_ATTRIBUTE_DIRECTORY)!=0)!=directory;
}
// UI 线程在启动和显式刷新时调用；没有线程、轮询、网络、写入或代码加载。
// Called on the UI thread at startup and explicit refresh only: no thread, polling, network, writes or code loading.
class PluginDiscovery final {
public:
    explicit PluginDiscovery(const std::filesystem::path& executableDirectory):root_(executableDirectory/L"plugins"){}
    const PluginSnapshot& Refresh();
    const PluginSnapshot& Snapshot() const noexcept {return snapshot_;}
    const std::filesystem::path& Root() const noexcept {return root_;}
    std::uint64_t RefreshCount() const noexcept {return refreshCount_;}
private:
    std::filesystem::path root_;
    PluginSnapshot snapshot_;
    std::uint64_t refreshCount_{};
};
}
