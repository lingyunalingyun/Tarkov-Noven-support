#pragma once
#include "updates/UpdateService.h"
namespace noven::updates {
// 离线验收可加有界延迟以观察暂停；生产构建不链接该传输。
// Bounded review latency makes pause observable; production never links this transport.
std::shared_ptr<UpdateSource> LocalUpdateSource(std::filesystem::path fixture,std::chrono::milliseconds reviewDelay={});
}
