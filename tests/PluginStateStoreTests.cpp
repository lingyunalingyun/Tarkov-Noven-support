#include "plugins/PluginStateStore.h"
#include "plugins/PluginPipe.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace noven::plugins;
void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
struct Temp {std::filesystem::path path=std::filesystem::temp_directory_path()/("noven-state-test-"+ipc::RandomSecret());Temp(){std::filesystem::create_directory(path);}~Temp(){std::error_code e;std::filesystem::remove_all(path,e);}};
int main() try {
    auto manifest=*ParseManifest(R"({"manifestVersion":2,"id":"com.example.test","name":"Test","version":"1.0.0","apiVersion":1,"runtime":{"kind":"native-dll","entry":"plugin.dll"},"permissions":["ui.page.register"]})").manifest;
    Temp temp;const auto path=temp.path/"data"/"plugin-state.json";auto store=PluginStateStore::Load(path);
    Check(!store.Corrupt()&&!store.Intent(manifest.id).enabled&&!store.Authorized(manifest),"new plugin defaults Disabled without consent");
    Check(store.Consent(manifest)&&store.Authorized(manifest)&&store.Save(path),"explicit consent enables and persists");
    store=PluginStateStore::Load(path);Check(store.Authorized(manifest)&&store.Intent(manifest.id).grantedPermissions==manifest.requestedPermissions,"restart preserves enable/grant");
    const auto json=store.Encode();Check(json.find("session")==json.npos&&json.find("pipe")==json.npos&&json.find("runtime")==json.npos,"no secrets/runtime copied into owned state");
    auto less=manifest;less.requestedPermissions.clear();Check(store.Authorized(less),"permission removal requires no new privilege");
    store.Disable(manifest.id);Check(!store.Authorized(manifest)&&store.Save(path)&&!PluginStateStore::Load(path).Intent(manifest.id).enabled,"disable/crash persists without restart loop");
    Check(store.Consent(less)&&store.Authorized(less)&&!store.Authorized(manifest),"permission expansion invalidates earlier empty consent");
    auto catalog=manifest;catalog.requestedPermissions={"ui.page.register","catalog.items.read","catalog.tasks.read","catalog.maps.read"};
    Check(SupportedPermissions(catalog)&&!store.Authorized(catalog),"newly supported catalogs still require re-consent");
    Check(store.Consent(catalog)&&store.Save(path)&&PluginStateStore::Load(path).Authorized(catalog),"four scoped grants persist");
    auto foreignCatalog=catalog;foreignCatalog.id="com.example.other";Check(!store.Authorized(foreignCatalog),"another ID cannot reuse catalog grants");
    auto history=catalog;history.requestedPermissions.push_back("raid.history.read");history.requestedPermissions.push_back("catalog.events.read");
    Check(SupportedPermissions(history)&&!store.Authorized(history),"history/event expansion requires consent");
    Check(store.Consent(history)&&store.Save(path)&&PluginStateStore::Load(path).Authorized(history),"six scoped grants persist");
    auto foreignHistory=history;foreignHistory.id="com.example.other";Check(!store.Authorized(foreignHistory),"history/event grants cannot cross plugins");
    auto scans=history;scans.requestedPermissions.push_back("scan.history.read");scans.requestedPermissions.push_back("scan.events.subscribe");
    Check(SupportedPermissions(scans)&&!store.Authorized(scans),"scan permission expansion requires re-consent");
    Check(store.Consent(scans)&&store.Save(path)&&PluginStateStore::Load(path).Authorized(scans),"eight grants persist");
    auto foreignScan=scans;foreignScan.id="com.example.other";Check(!store.Authorized(foreignScan),"scan grants scoped to identity");
    auto storage=scans;storage.requestedPermissions.push_back("storage.plugin");Check(SupportedPermissions(storage)&&!store.Authorized(storage),"storage expansion requires re-consent");
    Check(store.Consent(storage)&&store.Save(path)&&PluginStateStore::Load(path).Authorized(storage),"nine grants persist");
    for(const auto permission:{"raid.active.read","network.http","ui.decorate","raw_logs.read","capture_frame.read","filesystem.arbitrary","process.access"})Check(!SupportedPermission(permission),"other product/runtime permissions remain unsupported");
    auto unsupported=manifest;unsupported.requestedPermissions.push_back("network.http");Check(!SupportedPermissions(unsupported)&&!store.Consent(unsupported)&&!store.Authorized(unsupported),"unsupported permissions never granted/enabled");
    auto foreign=manifest;foreign.id="com.example.other";Check(!store.Authorized(foreign),"grant scoped to exact identity");
    auto v1=manifest;v1.manifestVersion=1;v1.runtime.reset();Check(!store.Consent(v1)&&!store.Authorized(v1),"V1 cannot acquire runtime consent");
    for(const auto bad:{"{",R"({"schemaVersion":2,"plugins":[]})",R"({"schemaVersion":1,"plugins":[{"id":"com.example.test","enabled":true,"grantedPermissions":["network.http"]}]})",R"({"schemaVersion":1,"plugins":[{"id":"com.example.test","enabled":true,"grantedPermissions":[],"session":"secret"}]})"}){auto corrupt=PluginStateStore::Decode(bad);Check(corrupt.Corrupt()&&!corrupt.Authorized(manifest),"corruption fails closed");}
    Check(PluginStateStore::Decode(std::string(256*1024+1,' ')).Corrupt(),"state bound before parse");
    {std::ofstream file(path,std::ios::binary|std::ios::trunc);file<<"broken";}Check(PluginStateStore::Load(path).Corrupt(),"file corruption safe fallback");
    Check(store.Save(path)&&!PluginStateStore::Load(path).Corrupt(),"explicit valid save replaces corrupt file atomically");
    std::size_t files=0;for(const auto& file:std::filesystem::directory_iterator(path.parent_path())){++files;Check(file.path()==path,"no temporary state files after successful replace");}Check(files==1,"only owned state retained");
    std::cout<<"Plugin state/permissions PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
