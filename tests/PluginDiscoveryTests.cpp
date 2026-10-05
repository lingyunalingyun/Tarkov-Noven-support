#include "plugins/PluginDiscovery.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace noven::plugins;
void Check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
struct TemporaryDirectory {
    std::filesystem::path path;
    TemporaryDirectory(){wchar_t parent[MAX_PATH]{},name[MAX_PATH]{};GetTempPathW(MAX_PATH,parent);Check(GetTempFileNameW(parent,L"nvp",0,name)!=0,"temporary directory");DeleteFileW(name);path=name;std::filesystem::create_directory(path);}
    ~TemporaryDirectory(){std::error_code error;std::filesystem::remove_all(path,error);}
};
const std::string valid=R"({"manifestVersion":1,"id":"com.example.loot-route","name":"Loot Route","version":"1.0.0","apiVersion":1,"permissions":["future.permission.read","ui.page.register"]})";
void Write(const std::filesystem::path& directory,const std::string& text){std::filesystem::create_directories(directory);std::ofstream(directory/L"manifest.json",std::ios::binary)<<text;}
int main() try {
    TemporaryDirectory temporary;PluginDiscovery service{temporary.path};
    Check(service.Refresh().records.empty()&&service.Snapshot().diagnostics.empty(),"missing root healthy empty");
    Check(!std::filesystem::exists(service.Root()),"discovery does not create files");
    std::filesystem::create_directory(service.Root());Check(service.Refresh().records.empty(),"empty root");
    Write(service.Root()/L"b",valid);
    auto snapshot=service.Refresh();Check(snapshot.records.size()==1&&snapshot.records[0].state==PluginState::Valid,"valid manifest discovered");
    Check(snapshot.records[0].manifest->requestedPermissions[0]=="future.permission.read","future permissions preserved without grants");
    auto other=valid;other.replace(other.find("com.example.loot-route"),22,"dev.example.other");
    Write(service.Root()/L"a",other);snapshot=service.Refresh();
    Check(snapshot.records.size()==2&&snapshot.records[0].directory.filename()==L"a","deterministic directory order");
    std::filesystem::create_directory(service.Root()/L"missing");Write(service.Root()/L"bad","{");
    Write(service.Root()/L"large",std::string(MaximumManifestBytes+1,' '));
    Write(service.Root()/L"nested"/L"inner",valid);
    snapshot=service.Refresh();Check(snapshot.records.size()==6,"direct children only, invalid neighbors retained");
    Check(snapshot.records[0].state==PluginState::Valid&&snapshot.records[1].state==PluginState::Valid,"invalid neighbors do not suppress valid plugins");
    Write(service.Root()/L"duplicate",valid);snapshot=service.Refresh();int conflicts{};
    for(const auto& record:snapshot.records)if(record.state==PluginState::DuplicateId)++conflicts;
    Check(conflicts==2,"all duplicate IDs conflict without enumeration winner");
    std::filesystem::remove(service.Root()/L"duplicate"/L"manifest.json");snapshot=service.Refresh();
    Check(snapshot.records[1].state==PluginState::Valid,"refresh detects removal and resolves conflict");
    Check(service.RefreshCount()==7,"explicit refresh only");
    for(const auto& record:snapshot.records)if(record.directory.filename()==L"bad"||record.directory.filename()==L"missing"||record.directory.filename()==L"large"||record.directory.filename()==L"nested")
        Check(record.state==PluginState::InvalidManifest&&!record.diagnostics.empty(),"missing/malformed/oversized/nested manifests remain visible diagnostics");
    Check(UnsafeAttributes(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT,true)
        &&UnsafeAttributes(FILE_ATTRIBUTE_REPARSE_POINT,false)&&UnsafeAttributes(FILE_ATTRIBUTE_DIRECTORY,false)
        &&!UnsafeAttributes(FILE_ATTRIBUTE_DIRECTORY,true),"reparse decision logic independent of elevated symlink privileges");
    Write(temporary.path/L"outside",valid);std::filesystem::create_directory(service.Root()/L"hardlink");
    Check(CreateHardLinkW((service.Root()/L"hardlink"/L"manifest.json").c_str(),(temporary.path/L"outside"/L"manifest.json").c_str(),nullptr)!=0,"unprivileged hardlink fixture");
    snapshot=service.Refresh();
    for(const auto& record:snapshot.records)if(record.directory.filename()==L"hardlink")Check(record.state==PluginState::UnsafePath,"hardlinked external manifest rejected before read");
    const auto link=service.Root()/L"linked";
    if(CreateSymbolicLinkW(link.c_str(),(temporary.path/L"outside").c_str(),SYMBOLIC_LINK_FLAG_DIRECTORY|SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)) {
        snapshot=service.Refresh();for(const auto& record:snapshot.records)if(record.directory==link)Check(record.state==PluginState::UnsafePath,"directory symlink rejected");
        std::filesystem::remove(link);
    }else std::cout<<"Directory symlink creation unavailable; attribute decision and hardlink tests exercised\n";
    TemporaryDirectory limited;PluginDiscovery bounded{limited.path};
    for(int i=0;i<129;++i)std::filesystem::create_directories(bounded.Root()/std::to_string(i));
    Check(bounded.Refresh().records.empty()&&!bounded.Snapshot().diagnostics.empty(),"capacity failure explicit, no arbitrary partial winner");
    TemporaryDirectory compatibility;PluginDiscovery versions{compatibility.path};
    auto future=valid;future.replace(future.find("\"apiVersion\":1"),14,"\"apiVersion\":2");
    Write(versions.Root()/L"supported",valid);Write(versions.Root()/L"future",future);
    snapshot=versions.Refresh();
    Check(snapshot.records.size()==2&&snapshot.records[0].state==PluginState::DuplicateId&&snapshot.records[1].state==PluginState::DuplicateId,"API incompatibility cannot hide a duplicate ID");
    std::filesystem::remove(versions.Root()/L"supported"/L"manifest.json");snapshot=versions.Refresh();
    Check(snapshot.records[0].state==PluginState::IncompatibleApi&&snapshot.records[0].manifest->apiVersion==2,"refresh restores incompatible API metadata after conflict removal");
    Write(versions.Root()/L"schema",R"({"manifestVersion":3})");snapshot=versions.Refresh();
    Check(snapshot.records[1].state==PluginState::IncompatibleManifest,"future schema visible without reinterpretation");
    auto native=valid;native.replace(native.find("\"manifestVersion\":1"),19,"\"manifestVersion\":2");native.replace(native.find("com.example.loot-route"),22,"com.example.native");native.pop_back();native+=R"(,"runtime":{"kind":"native-dll","entry":"plugin.dll"}})";
    Write(versions.Root()/L"native",native);snapshot=versions.Refresh();
    bool foundNative=false;for(const auto& record:snapshot.records)if(record.manifest&&record.manifest->id=="com.example.native")foundNative=record.state==PluginState::Valid&&record.manifest->runtime.has_value();
    Check(foundNative,"V2 discovery returns metadata without requiring or loading DLL");
    TemporaryDirectory entries;PluginDiscovery entryBound{entries.path};std::filesystem::create_directory(entryBound.Root());
    for(int i=0;i<1025;++i)std::ofstream(entryBound.Root()/std::to_string(i))<<"not executed";
    Check(entryBound.Refresh().records.empty()&&!entryBound.Snapshot().diagnostics.empty(),"root entry capacity includes files without inspecting or executing them");
    std::cout<<"Read-only plugin discovery PASS\n";
} catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
