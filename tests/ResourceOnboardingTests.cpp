#include "resources/ResourceOnboarding.h"
#include <fstream>
#include <iostream>
#include <windows.h>
using namespace noven::resources;
int main() try {
    const auto check=[](bool value){if(!value)throw std::runtime_error("onboarding assertion");};
    ResourceManifest manifest;
    for(const auto id:{"factory","woods"}){ResourceRecord record;record.stableMapId=id;record.resourceId="maps."+record.stableMapId;record.titleEn="Same title";record.downloadSize=1024;manifest.components.push_back(record);}
    ResourceSelection selection(manifest);check(selection.TotalBytes()==0&&!selection.Select("Same title",true));
    check(selection.Select("maps.factory",true)&&selection.TotalBytes()==1024);check(selection.Select("maps.factory",true)&&selection.TotalBytes()==1024);
    selection.SelectAll();check(selection.TotalBytes()==2048&&selection.SelectedIds()==std::vector<std::string>({"maps.factory","maps.woods"}));
    check(selection.Select("maps.woods",false)&&selection.TotalBytes()==1024);selection.SelectNone();check(selection.SelectedIds().empty());
    check(manifest.components.size()==2&&manifest.components[0].downloadSize==1024);
    auto invalid=manifest;invalid.components.push_back(invalid.components.front());bool rejected{};try{ResourceSelection bad(invalid);}catch(...){rejected=true;}check(rejected);
    const auto root=std::filesystem::temp_directory_path()/("noven-onboarding-"+std::to_string(GetCurrentProcessId()));
    check(!std::filesystem::exists(root));
    const auto installed=noven::common::AppPaths::Installed(root/"program",root/"local");
    ResourceOnboarding onboarding(installed);check(onboarding.ShouldShow());
    const auto test=noven::common::AppPaths::Test(root/"program",root/"test");
    check(!ResourceOnboarding(test).ShouldShow());check(!ResourceOnboarding(noven::common::AppPaths::Development(root/"dev")).ShouldShow());
    check(!std::filesystem::exists(root));
    // “稍后”与确认选择均记录完成；重启不重复强制弹窗，也不自动下载。
    // Later and confirmed selection both mark completion; restart never forces the dialog or downloads.
    check(onboarding.Complete());check(!onboarding.ShouldShow());check(!ResourceOnboarding(installed).ShouldShow());check(onboarding.Complete());
    const HANDLE locked=CreateFileW((installed.ResourceManifests()/"onboarding.json").c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    check(locked!=INVALID_HANDLE_VALUE);const bool replaced=onboarding.Complete();CloseHandle(locked);check(!replaced&&!onboarding.ShouldShow());
    unsigned files{};for(const auto& entry:std::filesystem::directory_iterator(installed.ResourceManifests())){(void)entry;++files;}check(files==1);
    check(!std::filesystem::exists(installed.Plugins())&&!std::filesystem::exists(installed.Data())&&!std::filesystem::exists(installed.programRoot));
    {std::ofstream file(installed.ResourceManifests()/"onboarding.json",std::ios::binary);file<<"bad";}
    check(ResourceOnboarding(installed).ShouldShow());check(onboarding.Complete());
    {std::ofstream file(installed.ResourceManifests()/"onboarding.json",std::ios::binary);file<<std::string(4097,' ');}
    check(ResourceOnboarding(installed).ShouldShow());check(onboarding.Complete());
    const auto blocked=noven::common::AppPaths::Installed(root/"program",root/"blocked");
    std::filesystem::create_directories(blocked.Resources());{std::ofstream file(blocked.ResourceManifests());file<<"not a directory";}
    check(!ResourceOnboarding(blocked).Complete());
    std::filesystem::remove_all(root);std::cout<<"selection/stable IDs/totals/installed-only/defer/restart/corruption/isolation PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what();return 1;}
