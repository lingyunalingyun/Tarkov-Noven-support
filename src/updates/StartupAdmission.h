#pragma once
#include "updates/ReleaseManifest.h"
#include <filesystem>
#include <stop_token>

namespace noven::updates {
enum class StartupAdmission {Disabled,Checking,Allowed,UpdateRequired,Blocked};
// 版本不是小数；跨主/次版本拒绝，同分支最多落后五个补丁版本。
// Versions are not decimals: require the same major/minor and at most five patches behind.
bool WithinStartupWindow(std::string_view current,std::string_view latest);
bool VerifyStartupFiles(const std::filesystem::path& root,const AuthenticatedRelease&,std::stop_token);
void RetainReleaseManifest(const std::filesystem::path& history,const AuthenticatedRelease&,
    std::span<const ReleasePublicKey> keys);
}
