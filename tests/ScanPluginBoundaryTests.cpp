#include "plugins/PluginProtocol.h"
#include "plugins/PluginStateStore.h"
#include "noven_plugin_abi_v1.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace noven::plugins;
static_assert(sizeof(NovenHostApiV1)==40&&sizeof(NovenPluginInstanceV1)==32);
static_assert(sizeof(NovenCatalogHostApiV1)==24&&sizeof(NovenCatalogInstanceV1)==16);
static_assert(NOVEN_NATIVE_ABI_VERSION==1&&NOVEN_PLUGIN_API_VERSION==1&&NOVEN_CATALOG_SCHEMA_VERSION==1&&ipc::TransportProtocolVersion==1);
void Check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
std::string Read(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);Check(file.good(),"boundary source available");return {std::istreambuf_iterator<char>(file),{}};}
int wmain(int argc,wchar_t** argv) try {
    Check(argc==2,"source root required");const std::filesystem::path root=argv[1];
    const auto abi=Read(root/"sdk/noven_plugin_abi_v1.h");
    for(const auto forbidden:{"HWND","ID2D1RenderTarget","ID3D","capture_frame","screen_coordinates","ocr_tensor","scan_trigger","scan_start","capture_buffer"})Check(abi.find(forbidden)==abi.npos,"no native UI/capture/trigger ABI");
    for(const auto permission:{"scan.start","scan.trigger","scan.capture","capture_frame.read","ocr.raw.read","screen.read","raid.active.read","raw_logs.read","process.access"}){
        Check(!SupportedPermission(permission),"no privileged scan/game capability activated");
        bool rejected=false;try{ipc::ParseMessage("{\"type\":\""+std::string(permission)+"\"}");}catch(const std::exception&){rejected=true;}Check(rejected,"no trigger/capture IPC route");
    }
    const auto app=Read(root/"src/app/App.cpp");const auto start=app.find("void App::OnScanCompletionMessage"),end=app.find("void App::OnScanStepMessage",start);
    Check(start!=app.npos&&end!=app.npos,"existing completion handler");const auto completion=app.substr(start,end-start);
    const auto append=completion.find("recent_scan_store_->Append"),notify=completion.find("NotifyScanCompleted");
    Check(append!=completion.npos&&notify>append&&completion.find("NotifyScanCompleted",notify+1)==completion.npos,"one notification after existing successful history admission");
    const auto runtime=Read(root/"src/plugins/PluginRuntimeManager.cpp");const auto producer=runtime.substr(runtime.find("void PluginRuntimeManager::NotifyScanCompleted"),runtime.find("std::vector<HostSnapshot> PluginRuntimeManager::Snapshots")-runtime.find("void PluginRuntimeManager::NotifyScanCompleted"));
    Check(producer.find("channel.")==producer.npos&&producer.find("lock_guard")==producer.npos&&producer.find("WaitFor")==producer.npos,"completion producer has no IPC/plugin/state-lock waits");
    for(const auto file:{"src/plugins/PluginRuntimeManager.cpp","src/plugins/CatalogPluginService.cpp","src/plugins/PluginScanData.cpp"}){
        const auto text=Read(root/file);for(const auto forbidden:{"#include \"capture/","#include \"ocr/","#include \"scanner/","ReadProcessMemory","WriteProcessMemory","CreateRemoteThread"})Check(text.find(forbidden)==text.npos,"safe result projection never accesses Scanner/game internals");
    }
    const auto demo=Read(root/"examples/scan-plugin/scan_plugin.c");for(const auto forbidden:{"Trigger Scan","CreateProcess","OpenProcess","CreateFile","WinHttp","LoadLibrary","SendInput"})Check(demo.find(forbidden)==demo.npos,"example is metadata/history/subscription only");
    std::cout<<"Scan no-trigger/no-capture/ABI architecture guards PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
