#pragma once
#include "resources/ResourceManifest.h"
#include "common/AppPaths.h"
#include <filesystem>
#include <stop_token>
namespace noven::resources {
bool SafeResourcePath(const std::filesystem::path& path);
bool ResourceDirectories(const std::filesystem::path& path);
std::string ReadResourceText(const std::filesystem::path& path,std::size_t maximum);
bool WriteResourceText(const std::filesystem::path& path,std::string_view text);
std::string ResourceHash(const std::filesystem::path& path,std::stop_token stop={});
bool ValidPackagePath(std::string_view path);
std::filesystem::path InstallPackage(const common::AppPaths& paths,const ResourceRecord& record,
    const std::filesystem::path& package,std::stop_token stop={});
bool VerifyPackage(const std::filesystem::path& root,const ResourceRecord& record,std::stop_token stop={});
bool RemoveResourceTree(const std::filesystem::path& managedRoot,const std::filesystem::path& target);
}
