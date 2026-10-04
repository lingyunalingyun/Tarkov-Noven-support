#include "data/AppSettings.h"
#include <iostream>
#include <stdexcept>
using namespace noven::data;
void Check(bool ok){if(!ok)throw std::runtime_error("settings assertion");}
int main() try {
    AppSettings defaults;Check(defaults.Valid());Check(AppSettings::Decode(defaults.Encode())==defaults);
    AppSettings chosen;chosen.mode=GameMode::Pve;chosen.scanKey=VK_F6;chosen.scanModifiers=MOD_CONTROL|MOD_SHIFT;
    chosen.gameDirectory=L"E:\\游戏\\Escape from Tarkov";
    Check(AppSettings::Decode(chosen.Encode())==chosen);
    Check(!AppSettings::Decode("{}"));Check(!AppSettings::Decode(std::string(65537,'x')));
    auto malformed=chosen;malformed.scanKey=VK_ESCAPE;Check(!AppSettings::Decode(malformed.Encode()));
    malformed=chosen;malformed.gameDirectory=L"relative";Check(!AppSettings::Decode(malformed.Encode()));
    const auto root=std::filesystem::temp_directory_path()/("noven-settings-"+std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(root/L"game"/L"build"/L"Logs");
    Check(!AppSettings::Load(root/L"missing.json"));Check(chosen.Save(root/L"settings.json"));
    Check(AppSettings::Load(root/L"settings.json")==chosen);
    Check(GameLogRoot(root/L"game")==root/L"game"/L"build"/L"Logs");
    Check(!GameLogRoot(root/L"unavailable")&&!GameLogRoot(L"relative"));
    chosen.mode=GameMode::Seasonal;Check(chosen.Save(root/L"settings.json")&&AppSettings::Load(root/L"settings.json")==chosen);
    Check(!std::filesystem::exists(root/L"settings.json.tmp"));
    std::filesystem::remove_all(root);std::cout<<"Settings round-trip, validation and explicit log paths passed\n";
}catch(const std::exception& e){std::cerr<<e.what();return 1;}
