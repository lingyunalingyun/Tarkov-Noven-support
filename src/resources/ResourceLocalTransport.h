#pragma once
#include "resources/ResourceService.h"
namespace noven::resources {
// 仅用于显式 Review/Test 的本地传输；无网络、无任意 URL，不能注入 Installed 生产模式。
// Explicit Review/Test transport only: no network/arbitrary URLs, rejected by Installed production mode.
std::shared_ptr<ResourceTransport> LocalResourceTransport(std::filesystem::path fixtureRoot);
}
