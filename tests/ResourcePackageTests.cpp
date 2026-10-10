#include "resources/ResourceFiles.h"
#include <fstream>
#include <iostream>
#include <windows.h>
using namespace noven::resources;
namespace {
void Check(bool value){if(!value)throw std::runtime_error("package assertion");}
void Number(std::string& bytes,std::uint64_t value,unsigned count){for(unsigned i=0;i<count;++i)bytes+=static_cast<char>((value>>(8*i))&255);}
std::string Package(const std::vector<std::pair<std::string,std::string>>& files,std::string id="maps.factory",unsigned count=0){
    std::string bytes="NVR1";Number(bytes,id.size(),4);bytes+=id;Number(bytes,count?count:files.size(),4);
    for(const auto& [name,content]:files){Number(bytes,name.size(),4);Number(bytes,content.size(),8);bytes+=name;bytes+=content;}return bytes;
}
void Save(const std::filesystem::path& file,const std::string& text){std::ofstream out(file,std::ios::binary);out.write(text.data(),static_cast<std::streamsize>(text.size()));Check(static_cast<bool>(out));}
}
int main() try {
    const auto root=std::filesystem::temp_directory_path()/("noven-package-"+std::to_string(GetCurrentProcessId()));Check(!std::filesystem::exists(root));std::filesystem::create_directories(root);
    const auto paths=noven::common::AppPaths::Test(root/"program",root/"test");const auto file=root/"synthetic.nvr";
    ResourceRecord record;record.resourceId="maps.factory";record.stableMapId="factory";record.version="1.0.0";record.installedSize=7;
    const auto bytes=Package({{"maps/factory/floor.png","PNG"},{"maps/factory/floor.tiles","TILE"}});Save(file,bytes);record.downloadSize=bytes.size();record.sha256=ResourceHash(file);
    const auto installed=InstallPackage(paths,record,file);Check(VerifyPackage(installed,record));Check(ReadResourceText(installed/"maps/factory/floor.png",10)=="PNG");
    Check(InstallPackage(paths,record,file)==installed);
    auto wrong=record;wrong.downloadSize++;bool failure{};try{InstallPackage(paths,wrong,file);}catch(...){failure=true;}Check(failure&&VerifyPackage(installed,record));
    wrong=record;wrong.sha256=std::string(64,'0');failure=false;try{InstallPackage(paths,wrong,file);}catch(...){failure=true;}Check(failure&&VerifyPackage(installed,record));
    for(const auto name:{"../evil.png","/evil.png","C:/evil.png","maps/../evil.png","maps/a/../../evil.png","maps/a/b.exe","maps/CON.png","maps/a/","maps/a./b.png","maps/a\\b.png","maps/a%2fb.png"}){
        Check(!ValidPackagePath(name));const auto bad=Package({{name,"bad"}});Save(file,bad);wrong=record;wrong.downloadSize=bad.size();wrong.sha256=ResourceHash(file);wrong.installedSize=3;failure=false;
        try{InstallPackage(paths,wrong,file);}catch(...){failure=true;}Check(failure&&VerifyPackage(installed,record));
    }
    for(const auto bad:{Package({{"maps/a.png","a"},{"maps/A.png","b"}}),Package({{"maps/a.png","a"}},"maps.woods"),Package({},"maps.factory",30001)}){
        Save(file,bad);wrong=record;wrong.downloadSize=bad.size();wrong.sha256=ResourceHash(file);failure=false;try{InstallPackage(paths,wrong,file);}catch(...){failure=true;}Check(failure);
    }
    Save(file,bytes);std::stop_source cancel;cancel.request_stop();failure=false;try{InstallPackage(paths,record,file,cancel.get_token());}catch(...){failure=true;}Check(failure);
    Save(installed/"maps/factory/floor.png","bad");Check(!VerifyPackage(installed,record));Save(installed/"maps/factory/floor.png","PNG");Check(VerifyPackage(installed,record));
    auto receipt=ReadResourceText(installed/"receipt.json",10000);const auto at=receipt.find("floor.png");receipt.replace(at,9,"other.png");Save(installed/"receipt.json",receipt);Check(!VerifyPackage(installed,record));
    Check(InstallPackage(paths,record,file)==installed&&VerifyPackage(installed,record));
    Check(!RemoveResourceTree(paths.ResourceMaps(),paths.ResourceMaps()));Check(!RemoveResourceTree(paths.ResourceMaps(),paths.Plugins()));Check(!RemoveResourceTree(paths.ResourceMaps(),paths.programRoot));
    Check(!std::filesystem::exists(paths.Plugins())&&!std::filesystem::exists(paths.Data()));Check(RemoveResourceTree(paths.ResourceMaps(),installed));
    Check(std::filesystem::is_empty(paths.ResourceStaging()));std::filesystem::remove_all(root);std::cout<<"package bounds/traversal/collision/identity/integrity/atomicity/deletion PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what();return 1;}
