#include "plugins/PluginStorageService.h"
#include "plugins/PluginPipe.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace noven::plugins;
void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
struct Temp {std::filesystem::path path=std::filesystem::temp_directory_path()/("noven-storage-"+ipc::RandomSecret());Temp(){std::filesystem::create_directory(path);}~Temp(){std::error_code error;std::filesystem::remove_all(path,error);}};
int main() try {
    Temp temp;PluginStorageService service(temp.path/"data");CatalogGrants grants{true,{"storage.plugin"},{"storage.plugin"}};
    StorageRequest request;request.requestId=1;request.key="settings";const auto query=[&](const char* id="com.example.a"){return service.Query(id,grants,request);};
    Check(query().status==StorageStatus::NotFound,"default missing");request.operation=StorageOperation::Set;request.value=std::string("a\0\xff",3);Check(query().status==StorageStatus::Ok,"binary set");
    request.operation=StorageOperation::Get;request.value.clear();Check(query().value==std::string("a\0\xff",3),"exact opaque bytes");
    Check(query("com.example.b").status==StorageStatus::NotFound,"foreign namespace get denied by isolation");
    for(unsigned i=0;i<3;++i){auto denied=grants;if(i==0)denied.declared.clear();if(i==1)denied.granted.clear();if(i==2)denied.valid=false;Check(service.Query("com.example.a",denied,request).status==StorageStatus::PermissionDenied,"declaration grant validity required");}
    for(const auto bad:{"","../x","x..y","a/b","a\\b","C:x","\n","\x7f","\xc0\xaf"})Check(!ValidStorageKey(bad),"unsafe key rejected");
    Check(!ValidStorageKey(std::string(129,'a'))&&!ValidStorageKey(std::string("a\0b",3)),"key size and NUL");
    for(const auto key:{"settings","route-cache","ui.preferences","中文","é","é"})Check(ValidStorageKey(key),"UTF-8 exact comparison keys");
    for(const auto key:{"中文","a","é","é"}){request.key=key;request.operation=StorageOperation::Set;request.value="value";Check(query().status==StorageStatus::Ok,"distinct Unicode keys");}
    request={1,StorageOperation::List,"","",0,2};auto list=query();Check(list.total==5&&list.keys==std::vector<std::string>{"a","é"}&&list.nextOffset==2,"deterministic list page");
    Check(query("com.example.b").keys.empty(),"list isolated");request.offset=2;list=query();Check(list.keys.size()==2&&list.nextOffset==4,"pagination");
    request={1,StorageOperation::Get,"settings"};PluginStorageService restarted(temp.path/"data");Check(restarted.Query("com.example.a",grants,request).value==std::string("a\0\xff",3),"service restart persists binary");
    std::filesystem::path original;for(const auto& file:std::filesystem::directory_iterator(temp.path/"data"/"plugin-storage"))if(file.file_size()>76)original=file.path();
    ipc::Handle held(CreateFileW(original.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));Check(static_cast<bool>(held),"hold old copy against replacement");
    request.operation=StorageOperation::Set;request.value="new";Check(query().status==StorageStatus::IoError,"failed replacement reported");held.Reset();request.operation=StorageOperation::Get;request.value.clear();Check(query().value==std::string("a\0\xff",3),"failed write retains valid old bytes");
    request={1,StorageOperation::Delete,"settings"};Check(query("com.example.b").status==StorageStatus::NotFound&&query().status==StorageStatus::Ok&&query().status==StorageStatus::NotFound,"delete isolation and missing status");
    request={1,StorageOperation::Set,"big",std::string(MaximumStorageValue+1,'x')};Check(query().status==StorageStatus::InvalidRequest,"value cap enforced");
    request.value.resize(MaximumStorageValue);for(unsigned i=0;i<128;++i){request.key="v"+std::to_string(i);const auto status=query("com.example.quota").status;Check(status==(i<127?StorageStatus::Ok:StorageStatus::Quota),"key plus value total quota");}
    request={1,StorageOperation::Set,"v0",""};Check(query("com.example.quota").status==StorageStatus::Ok,"overwrite releases exact bytes");request.key="v127";request.value=std::string(MaximumStorageValue,'x');Check(query("com.example.quota").status==StorageStatus::Ok,"released quota reusable");
    for(unsigned i=0;i<1024;++i){request={1,StorageOperation::Set,"k"+std::to_string(i),""};Check(query("com.example.keys").status==StorageStatus::Ok,"bounded key insertion");}request.key="overflow";Check(query("com.example.keys").status==StorageStatus::Quota,"key cap");
    request={1,StorageOperation::Delete,"k0"};Check(query("com.example.keys").status==StorageStatus::Ok,"delete releases key quota");request={1,StorageOperation::Set,"overflow",""};Check(query("com.example.keys").status==StorageStatus::Ok,"key quota reusable");
    {std::ofstream output(original,std::ios::binary|std::ios::trunc);output<<"broken";}request={1,StorageOperation::Get,"a"};Check(query().status==StorageStatus::Corrupt,"corruption explicit");request={1,StorageOperation::List,"","",0,32};Check(query().status==StorageStatus::Corrupt&&query("com.example.b").status==StorageStatus::Ok,"bad namespace isolated; no silent recovery");
    for(const auto bytes:{std::string{},std::string("\0\xff",2),std::string(MaximumStorageValue,'x')})Check(DecodeStorageBytes(EncodeStorageBytes(bytes))==bytes,"canonical binary wire codec");
    for(const auto bad:{"A","====","AB==","AA=A","AAA=AA=="}){bool failed=false;try{DecodeStorageBytes(bad);}catch(const std::exception&){failed=true;}Check(failed,"malformed base64 rejected");}
    request.offset=UINT32_MAX;Check(!ValidStorageRequest(request),"offset overflow");request.offset=0;request.limit=65;Check(!ValidStorageRequest(request),"list bound");
    std::cout<<"Scoped storage bytes/keys/quota/atomicity/corruption/restart PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
