#pragma once
#include "plugins/PluginManifest.h"
#include <filesystem>
#include <map>

namespace noven::plugins {
bool SupportedPermission(std::string_view permission);
bool SupportedPermissions(const PluginManifest& manifest);
struct PluginIntent final {bool enabled{};std::vector<std::string> grantedPermissions,grantedOrigins;};
// Noven 自有数据，不从插件文件恢复授权；permissions 集合包含关系就是确定性授权指纹。
// Noven-owned state, never grants from plugin files; set containment is the deterministic grant fingerprint.
class PluginStateStore final {
public:
    static PluginStateStore Load(const std::filesystem::path& path);
    bool Save(const std::filesystem::path& path) const;
    bool Consent(const PluginManifest& manifest);
    void Disable(std::string_view id);
    bool Authorized(const PluginManifest& manifest) const;
    PluginIntent Intent(std::string_view id) const;
    bool Corrupt() const noexcept {return corrupt_;}
    std::string Encode() const;
    static PluginStateStore Decode(std::string_view text);
private:
    std::map<std::string,PluginIntent,std::less<>> intents_;
    bool corrupt_{};
};
}
