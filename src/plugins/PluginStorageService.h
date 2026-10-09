#pragma once
#include "plugins/PluginStorageData.h"
#include "plugins/CatalogData.h"
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>

namespace noven::plugins {
// 只有认证会话工作线程调用；路径/namespace 不进入插件 ABI，存储不是凭据保险箱。
// Authenticated session workers only; paths/namespaces never enter plugin ABI, and storage is not a credential vault.
class PluginStorageService final {
public:
    explicit PluginStorageService(std::filesystem::path dataDirectory):data_(std::filesystem::absolute(std::move(dataDirectory))){}
    StorageResult Query(std::string_view authenticatedId,const CatalogGrants& grants,const StorageRequest& request);
private:
    std::filesystem::path data_;
    std::mutex mutex_;
    std::map<std::string,std::weak_ptr<std::mutex>,std::less<>> locks_;
};
}
