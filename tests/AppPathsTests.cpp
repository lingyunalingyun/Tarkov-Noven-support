#include "common/AppPaths.h"
#include <fstream>
#include <iostream>
#include <windows.h>
using noven::common::AppPaths;
int main() try {
    const auto root=std::filesystem::temp_directory_path()/("noven-paths-"+std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(root/"program");
    const auto check=[](bool ok){if(!ok)throw std::runtime_error("path assertion");};
    check(AppPaths::Resolve(noven::common::ProgramDirectory(),[]()->std::filesystem::path{throw std::runtime_error("tests must never query real Known Folder");}).mode==noven::common::PathMode::Development);
    const auto installed=AppPaths::Installed(root/"program",root/"local");
    check(installed.userRoot==root/L"local"/L"Noven Tarkov Support"&&installed.Plugins()==installed.userRoot/L"plugins"&&installed.Assets()==root/L"program"/L"assets");
    const auto test=AppPaths::Test(root/"program",root/"test");check(test.Data()==root/"test"/"user"/"data");
    unsigned queries{};auto folder=[&]{++queries;return root/"local";};
    check(AppPaths::Resolve(root/"program",folder).Plugins()==root/"program"/"plugins"&&queries==0);
    {std::ofstream marker(root/"program"/"noven-installed.layout");marker<<"1";}
    check(AppPaths::Resolve(root/"program",folder).userRoot==installed.userRoot&&queries==1);
    bool failure{};try{AppPaths::Resolve(root/"program",[]()->std::filesystem::path{throw std::runtime_error("known folder failure");});}catch(...){failure=true;}check(failure);
    failure=false;try{AppPaths::Test(root/"program",root/"program");}catch(...){failure=true;}check(failure);
    check(!std::filesystem::exists(installed.userRoot)&&!std::filesystem::exists(test.userRoot));
    std::filesystem::remove_all(root);std::cout<<"installed/development/test/known-folder/isolation PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what();return 1;}
