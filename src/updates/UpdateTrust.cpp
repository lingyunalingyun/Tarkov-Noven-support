#include "updates/UpdateTrust.h"
namespace noven::updates {
std::vector<ReleasePublicKey> CompiledReleaseKeys(){
    constexpr std::string_view id=NOVEN_UPDATE_KEY_ID,hex=NOVEN_UPDATE_PUBLIC_KEY_HEX;
    if(id.empty()||hex.empty())return {};return {{std::string(id),Unhex(hex,72)}};
}
std::string InstallerVersion(){return NOVEN_INSTALLER_VERSION;}
}
