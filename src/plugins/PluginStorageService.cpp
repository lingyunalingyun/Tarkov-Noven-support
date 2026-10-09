#include "plugins/PluginStorageService.h"
#include "plugins/PluginManifest.h"
#include "plugins/PluginPipe.h"
#include <bcrypt.h>
#include <array>
#include <algorithm>
#include <limits>

namespace noven::plugins {
namespace {
struct Fault final {StorageStatus status;};
using Entries=std::map<std::string,std::string,std::less<>>;
constexpr std::size_t MaximumStoreFile=MaximumStorageBytes+MaximumStorageKeys*8+76;
std::string Hash(std::string_view bytes){
    BCRYPT_ALG_HANDLE algorithm{};if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw Fault{StorageStatus::IoError};
    std::array<unsigned char,32> hash{};const auto status=BCryptHash(algorithm,nullptr,0,reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())),static_cast<ULONG>(bytes.size()),hash.data(),static_cast<ULONG>(hash.size()));
    BCryptCloseAlgorithmProvider(algorithm,0);if(status<0)throw Fault{StorageStatus::IoError};return {reinterpret_cast<const char*>(hash.data()),hash.size()};
}
std::wstring Namespace(std::string_view id){const auto hash=Hash(id);constexpr wchar_t digits[]=L"0123456789abcdef";std::wstring out;for(unsigned char c:hash){out+=digits[c>>4];out+=digits[c&15];}return out+L".bin";}
ipc::Handle Directory(const std::filesystem::path& path){
    ipc::Handle handle(CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));BY_HANDLE_FILE_INFORMATION info{};
    if(!handle||!GetFileInformationByHandle(handle.Get(),&info)||!(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)||(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT))throw Fault{StorageStatus::IoError};return handle;
}
void Number(std::string& out,std::uint32_t value){for(unsigned i=0;i<4;++i)out+=static_cast<char>(value>>(8*i));}
std::uint32_t Number(std::string_view text,std::size_t& pos){if(text.size()-pos<4)throw Fault{StorageStatus::Corrupt};std::uint32_t value{};for(unsigned i=0;i<4;++i)value|=static_cast<std::uint32_t>(static_cast<unsigned char>(text[pos++]))<<(8*i);return value;}
std::size_t Total(const Entries& entries){std::size_t total=0;for(const auto& [key,value]:entries){if(key.size()+value.size()>MaximumStorageBytes-total)throw Fault{StorageStatus::Quota};total+=key.size()+value.size();}return total;}
Entries Read(const std::filesystem::path& path,std::string_view id){
    ipc::Handle file(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    if(!file){if(GetLastError()==ERROR_FILE_NOT_FOUND)return {};throw Fault{StorageStatus::IoError};}
    BY_HANDLE_FILE_INFORMATION info{};LARGE_INTEGER size{};
    if(GetFileType(file.Get())!=FILE_TYPE_DISK||!GetFileInformationByHandle(file.Get(),&info)||(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))||info.nNumberOfLinks!=1)throw Fault{StorageStatus::IoError};
    if(!GetFileSizeEx(file.Get(),&size))throw Fault{StorageStatus::IoError};if(size.QuadPart<76||size.QuadPart>static_cast<LONGLONG>(MaximumStoreFile))throw Fault{StorageStatus::Corrupt};
    std::string text(static_cast<std::size_t>(size.QuadPart),'\0');DWORD read{};if(!ReadFile(file.Get(),text.data(),static_cast<DWORD>(text.size()),&read,nullptr)||read!=text.size())throw Fault{StorageStatus::IoError};
    const auto body=std::string_view(text).substr(0,text.size()-32);
    if(body.substr(0,8)!="NVSTORE1"||body.substr(8,32)!=Hash(id)||Hash(body)!=std::string_view(text).substr(text.size()-32))throw Fault{StorageStatus::Corrupt};
    std::size_t pos=40;const auto count=Number(body,pos);if(count>MaximumStorageKeys)throw Fault{StorageStatus::Corrupt};Entries entries;
    for(unsigned i=0;i<count;++i){const auto keySize=Number(body,pos),valueSize=Number(body,pos);
        if(!keySize||keySize>128||valueSize>MaximumStorageValue||keySize+valueSize>body.size()-pos)throw Fault{StorageStatus::Corrupt};
        std::string key(body.substr(pos,keySize));pos+=keySize;std::string value(body.substr(pos,valueSize));pos+=valueSize;
        if(!ValidStorageKey(key)||!entries.emplace(std::move(key),std::move(value)).second)throw Fault{StorageStatus::Corrupt};
    }
    if(pos!=body.size())throw Fault{StorageStatus::Corrupt};try{Total(entries);}catch(const Fault&){throw Fault{StorageStatus::Corrupt};}return entries;
}
void Write(const std::filesystem::path& path,std::string_view id,const Entries& entries){
    std::string text="NVSTORE1"+Hash(id);Number(text,static_cast<std::uint32_t>(entries.size()));
    for(const auto& [key,value]:entries){Number(text,static_cast<std::uint32_t>(key.size()));Number(text,static_cast<std::uint32_t>(value.size()));text+=key;text+=value;}text+=Hash(text);
    const auto nonce=ipc::RandomSecret();const auto temporary=path.parent_path()/(std::wstring(nonce.begin(),nonce.end())+L".tmp");
    ipc::Handle file(CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));if(!file)throw Fault{StorageStatus::IoError};
    DWORD written{};const bool ok=WriteFile(file.Get(),text.data(),static_cast<DWORD>(text.size()),&written,nullptr)&&written==text.size()&&FlushFileBuffers(file.Get());file.Reset();
    // 唯一临时文件完整写入并 flush 后才替换；失败保留旧值，不做隐藏清理。
    // Replace only after complete unique-temp write and flush; failure retains old values, no hidden cleanup.
    if(ok&&MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))return;
    DeleteFileW(temporary.c_str());throw Fault{StorageStatus::IoError};
}
}
StorageResult PluginStorageService::Query(std::string_view id,const CatalogGrants& grants,const StorageRequest& request){
    StorageResult result;result.requestId=request.requestId;
    if(!ValidStorageRequest(request)){result.status=StorageStatus::InvalidRequest;return result;}
    if(!ValidPluginId(id)||!grants.AllowsPermission("storage.plugin")){result.status=StorageStatus::PermissionDenied;return result;}
    try{
        std::shared_ptr<std::mutex> lock;{std::lock_guard guard(mutex_);std::erase_if(locks_,[](const auto& entry){return entry.second.expired();});lock=locks_[std::string(id)].lock();if(!lock)locks_[std::string(id)]=lock=std::make_shared<std::mutex>();}
        std::lock_guard guard(*lock);
        // 逐层固定祖先目录，拒绝 reparse；写入只使用 Core 计算的 SHA-256 内部名。
        // Pin ancestors rejecting reparse points; writes use only Core-derived SHA-256 internal names.
        std::vector<ipc::Handle> directories;auto current=data_.root_path();directories.push_back(Directory(current));
        for(const auto& part:data_.relative_path()){current/=part;if(!CreateDirectoryW(current.c_str(),nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)throw Fault{StorageStatus::IoError};directories.push_back(Directory(current));}
        current/=L"plugin-storage";if(!CreateDirectoryW(current.c_str(),nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)throw Fault{StorageStatus::IoError};directories.push_back(Directory(current));
        const auto path=current/Namespace(id);auto entries=Read(path,id);auto found=entries.find(request.key);
        switch(request.operation){
        case StorageOperation::Get:if(found==entries.end())result.status=StorageStatus::NotFound;else result.value=found->second;break;
        case StorageOperation::Set:entries[request.key]=request.value;if(entries.size()>MaximumStorageKeys)throw Fault{StorageStatus::Quota};Total(entries);Write(path,id,entries);break;
        case StorageOperation::Delete:if(found==entries.end())result.status=StorageStatus::NotFound;else{entries.erase(found);Write(path,id,entries);}break;
        case StorageOperation::List:{result.total=static_cast<std::uint32_t>(entries.size());auto it=entries.begin();std::advance(it,std::min<std::size_t>(request.offset,entries.size()));
            for(unsigned i=0;i<request.limit&&it!=entries.end();++i,++it)result.keys.push_back(it->first);result.nextOffset=std::min(request.offset,result.total)+static_cast<std::uint32_t>(result.keys.size());break;}
        }
    }catch(const Fault& fault){result.status=fault.status;result.value.clear();result.keys.clear();result.total=result.nextOffset=0;}
    catch(const std::exception&){result.status=StorageStatus::IoError;result.value.clear();result.keys.clear();result.total=result.nextOffset=0;}
    return result;
}
}
