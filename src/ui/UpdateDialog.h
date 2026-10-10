#pragma once
#include "updates/UpdateService.h"
#include <windows.h>
#include <array>
namespace noven::ui {
inline std::array<bool,8> UpdateDialogActions(const updates::UpdateSnapshot& v){
    using S=updates::UpdateState;const bool transfer=v.state==S::Downloading||v.state==S::Paused;
    return { !transfer&&v.state!=S::Staged,
        v.state==S::Available||(v.state==S::Error&&!v.target.empty()),
        v.state==S::Downloading&&!v.constructing,v.state==S::Paused,
        transfer&&!v.constructing,v.state==S::Staged,
        !v.previous.empty()&&!transfer&&v.state!=S::Checking,true };
}
bool ShowUpdateDialog(HWND owner,updates::UpdateService& service);
}
