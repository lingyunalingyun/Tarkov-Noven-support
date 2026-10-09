#include "common/AppPaths.h"
#include "plugins/PluginDiscovery.h"
#include "plugins/PluginRuntimeManager.h"
#include "plugins/PluginStateStore.h"
#include "plugins/PluginPipe.h"
#include <fstream>
#include <iostream>
using namespace noven::plugins;
int main() try {
    const auto root=std::filesystem::temp_directory_path()/("noven-path-integration-"+ipc::RandomSecret());
    struct Cleanup {std::filesystem::path root;~Cleanup(){std::error_code error;std::filesystem::remove_all(root,error);}} cleanup{root};
    const auto a=noven::common::AppPaths::Test(root/"program-a",root/"test");
    const auto b=noven::common::AppPaths::Test(root/"program-b",root/"test");
    std::filesystem::create_directories(a.Plugins()/"com.example.paths");
    {std::ofstream file(a.Plugins()/"com.example.paths"/"manifest.json");file<<R"({"manifestVersion":2,"id":"com.example.paths","name":"Paths","version":"1.0.0","apiVersion":1,"permissions":["storage.plugin"],"runtime":{"kind":"native-dll","entry":"plugin.dll"}})";}
    PluginDiscovery discovery(a);const auto snapshot=discovery.Refresh();if(snapshot.records.size()!=1||snapshot.records[0].state!=PluginState::Valid)throw std::runtime_error("user-root discovery");
    PluginRuntimeManager runtime(a);if(runtime.HostPath()!=a.programRoot/L"NovenPluginHost.exe"||runtime.SessionCount()!=0)throw std::runtime_error("program-only host ownership");
    PluginStateStore state;if(!state.Consent(*snapshot.records[0].manifest)||!state.Save(a.Data()/"plugin-state.json"))throw std::runtime_error("persist intent");
    if(!PluginStateStore::Load(b.Data()/"plugin-state.json").Authorized(*snapshot.records[0].manifest)||PluginDiscovery(b).Refresh().records.size()!=1)throw std::runtime_error("program replacement loses user data/grants");
    if(std::filesystem::exists(a.programRoot)||std::filesystem::exists(b.programRoot))throw std::runtime_error("user operations wrote program root");
    std::cout<<"user plugin root / host program root / upgrade intent preservation PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what();return 1;}
