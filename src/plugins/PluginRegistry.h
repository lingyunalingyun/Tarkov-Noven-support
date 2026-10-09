#pragma once
#include "plugins/PluginManifest.h"
#include <optional>
namespace noven::plugins {
inline constexpr std::size_t MaximumRegistryBytes=1024*1024,MaximumRegistryPlugins=1000;
enum class RegistryReview {Unreviewed,Reviewed,Deprecated,Blocked};
enum class RegistryCompatibility {Compatible,Incompatible,Unknown};
struct RegistryPackage final {std::string url,asset,sha256;std::uint64_t size{};};
struct RegistryPlugin final {
    PluginManifest metadata;
    std::string summary,sourceRef,releaseUrl;
    std::vector<std::string> categories,tags;
    RegistryReview review{RegistryReview::Unreviewed};
    std::optional<RegistryPackage> package;
};
// 远程记录只保存元数据；不包含可执行 runtime 描述，不能传入启用控制器。
// Remote records contain metadata only, never executable runtime descriptors usable by the enable controller.
struct PluginRegistry final {std::vector<RegistryPlugin> plugins;};
PluginRegistry ParsePluginRegistry(std::string_view utf8);
RegistryCompatibility Compatibility(const RegistryPlugin& plugin);
int ComparePluginVersions(std::string_view left,std::string_view right);
}
