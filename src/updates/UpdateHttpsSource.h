#pragma once
#include "updates/UpdateService.h"
#include "resources/ContentHttps.h"
namespace noven::updates {
std::shared_ptr<UpdateSource> HttpsUpdateSource(resources::ResourceSourcePolicy,std::shared_ptr<resources::ContentBackend> = {});
std::shared_ptr<UpdateSource> CompiledUpdateSource();
}
