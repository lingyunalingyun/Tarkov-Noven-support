#pragma once
#include "updates/ReleaseManifest.h"
namespace noven::updates {
std::vector<ReleasePublicKey> CompiledReleaseKeys();
std::string InstallerVersion();
}
