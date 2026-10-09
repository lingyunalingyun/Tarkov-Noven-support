#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace noven::plugins {
inline constexpr std::size_t MaximumManifestBytes=256*1024;
enum class PluginState { Valid,InvalidManifest,IncompatibleManifest,IncompatibleApi,DuplicateId,UnsafePath };
struct NativeRuntime final {std::string kind,entry;};
struct PluginManifest final {
    std::int64_t manifestVersion{},apiVersion{};
    std::string id,name,version,description,author,homepage,source,license;
    std::vector<std::string> requestedPermissions;
    // V1 永不解释运行时字段；只有显式 V2 才有运行时描述，不代表已授权执行。
    // V1 never interprets runtime fields; explicit V2 describes runtime, not execution consent.
    std::optional<NativeRuntime> runtime;
    std::vector<std::string> networkOrigins;
};
// 错误保存稳定的本地化诊断键与字段名，不泄露解析器异常或用户路径。
// Keep stable diagnostic keys and field names, never parser exceptions or unrelated user paths.
struct PluginDiagnostic final {std::string key,field;};
struct ManifestResult final {
    PluginState state{PluginState::InvalidManifest};
    std::optional<PluginManifest> manifest;
    std::vector<PluginDiagnostic> diagnostics;
};
bool ValidPluginId(std::string_view id);
bool ValidSemanticVersion(std::string_view version);
bool ValidRuntimeEntry(std::string_view filename);
ManifestResult ParseManifest(std::string_view utf8);
}
