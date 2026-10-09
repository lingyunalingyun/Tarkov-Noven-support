#include "plugins/PluginProtocol.h"
#include "plugins/PluginStateStore.h"
#include "noven_plugin_abi_v1.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace noven::plugins;
static_assert(sizeof(NovenHostApiV1)==40&&sizeof(NovenPluginInstanceV1)==32);
static_assert(sizeof(NovenCatalogHostApiV1)==24&&sizeof(NovenCatalogInstanceV1)==16);
static_assert(sizeof(NovenScanHostApiV1)==32&&sizeof(NovenScanInstanceV1)==24);
static_assert(NOVEN_NATIVE_ABI_VERSION==1&&NOVEN_PLUGIN_API_VERSION==1&&ipc::TransportProtocolVersion==1&&NOVEN_STORAGE_SCHEMA_VERSION==1);
void Check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
std::string Read(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);Check(file.good(),"source available");return {std::istreambuf_iterator<char>(file),{}};}
int wmain(int argc,wchar_t** argv) try {
    Check(argc==2,"source root required");const std::filesystem::path root=argv[1];auto abi=Read(root/"sdk/noven_plugin_abi_v1.h");
    const auto extension=abi.substr(abi.find("#define NOVEN_STORAGE_SCHEMA_VERSION"),abi.find("#if defined(NOVEN_PLUGIN_SCAN_IMPLEMENTATION)")-abi.find("#define NOVEN_STORAGE_SCHEMA_VERSION"));
    for(const auto forbidden:{"path","filename","HANDLE","HWND","CreateFile","fopen","directory","plugin_id"})Check(extension.find(forbidden)==extension.npos,"logical storage ABI has no filesystem or namespace selector");
    for(const auto permission:{"filesystem.arbitrary","process.access","network.http","capture_frame.read","raid.active.read","raw_logs.read"})Check(!SupportedPermission(permission),"privileged permissions remain unsupported");
    const auto demo=Read(root/"examples/storage-plugin/storage_plugin.c");for(const auto forbidden:{"CreateFile","fopen","fwrite","WinHttp","CreateProcess","OpenProcess","LoadLibrary","getenv"})Check(demo.find(forbidden)==demo.npos,"demo uses only logical Host API");
    const auto worker=Read(root/"src/plugins/PluginRuntimeManager.cpp");Check(worker.find("Query(session.snapshot.pluginId,session.catalogGrants,incoming.storageRequest)")!=worker.npos,"core namespace/grants derive from authenticated session");
    const auto now=std::chrono::steady_clock::now();DataRequestBudget budget;for(unsigned i=1;i<=16;++i)Check(budget.Begin(i,now)==RequestAdmission::Accepted,"bounded outstanding admission");
    Check(budget.Begin(1,now)==RequestAdmission::Duplicate&&budget.Begin(17,now)==RequestAdmission::Limited,"duplicate and cap");budget.Complete(1);
    Check(budget.Begin(17,now)==RequestAdmission::Limited&&budget.Begin(17,now+std::chrono::seconds(1))==RequestAdmission::Accepted,"rate bound despite completed request");budget.Clear();Check(budget.Pending()==0,"teardown invalidates all outstanding IDs");
    std::cout<<"Storage no-filesystem/session/ABI/rate boundaries PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
