#include "resources/ResourceFiles.h"
#include "plugins/PluginPipe.h"
#include "raid/RaidJson.h"
#include <bcrypt.h>
#include <array>
#include <set>
namespace noven::resources {
namespace {
using plugins::ipc::Handle;
[[noreturn]] void Fail(){throw std::runtime_error("resource file validation failed");}
bool Hex(std::string_view s){return s.size()==64&&s.find_first_not_of("0123456789abcdef")==s.npos;}
class Hash final {
    BCRYPT_ALG_HANDLE algorithm_{};BCRYPT_HASH_HANDLE hash_{};
public:
    Hash(){if(BCryptOpenAlgorithmProvider(&algorithm_,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)Fail();
        if(BCryptCreateHash(algorithm_,&hash_,nullptr,0,nullptr,0,0)<0){BCryptCloseAlgorithmProvider(algorithm_,0);Fail();}}
    ~Hash(){BCryptDestroyHash(hash_);BCryptCloseAlgorithmProvider(algorithm_,0);}
    void Add(const char* bytes,DWORD size){if(BCryptHashData(hash_,reinterpret_cast<PUCHAR>(const_cast<char*>(bytes)),size,0)<0)Fail();}
    std::string Finish(){std::array<UCHAR,32> bytes{};if(BCryptFinishHash(hash_,bytes.data(),32,0)<0)Fail();
        constexpr char hex[]="0123456789abcdef";std::string result;for(auto b:bytes){result+=hex[b>>4];result+=hex[b&15];}return result;}
};
Handle Open(const std::filesystem::path& path){
    if(!SafeResourcePath(path))Fail();Handle file(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    BY_HANDLE_FILE_INFORMATION info{};if(!file||!GetFileInformationByHandle(file.Get(),&info)||info.nNumberOfLinks!=1||
        (info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)))Fail();return file;
}
std::uint64_t Size(HANDLE file){LARGE_INTEGER size{};if(!GetFileSizeEx(file,&size)||size.QuadPart<0)Fail();return static_cast<std::uint64_t>(size.QuadPart);}
void Read(HANDLE file,char* bytes,DWORD count){DWORD read{};if(!ReadFile(file,bytes,count,&read,nullptr)||read!=count)Fail();}
std::uint64_t Number(HANDLE file,unsigned bytes){char value[8]{};Read(file,value,bytes);std::uint64_t result{};for(unsigned i=0;i<bytes;++i)result|=std::uint64_t(static_cast<unsigned char>(value[i]))<<(i*8);return result;}
std::string Digest(HANDLE file,std::stop_token stop){Hash hash;std::array<char,65536> bytes{};DWORD read{};
    do{if(stop.stop_requested())Fail();if(!ReadFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&read,nullptr))Fail();hash.Add(bytes.data(),read);}while(read);return hash.Finish();}
std::string Fold(std::string text){for(auto& c:text)if(c>='A'&&c<='Z')c+=32;return text;}
void HashNumber(Hash& hash,std::uint64_t value,unsigned count){char bytes[8]{};for(unsigned i=0;i<count;++i)bytes[i]=static_cast<char>((value>>(8*i))&255);hash.Add(bytes,count);}
bool Descendant(const std::filesystem::path& root,const std::filesystem::path& target){
    if(!root.is_absolute()||!target.is_absolute()||root==target)return false;
    auto a=root.lexically_normal(),b=target.lexically_normal();auto x=a.begin(),y=b.begin();
    for(;x!=a.end();++x,++y)if(y==b.end()||CompareStringOrdinal(x->c_str(),-1,y->c_str(),-1,TRUE)!=CSTR_EQUAL)return false;return y!=b.end();
}
}
bool SafeResourcePath(const std::filesystem::path& path){
    if(!path.is_absolute())return false;auto current=path.root_path();
    for(const auto& part:path.relative_path()){
        if(part==L".."||part==L".")return false;current/=part;const auto attr=GetFileAttributesW(current.c_str());
        if(attr!=INVALID_FILE_ATTRIBUTES){if(attr&FILE_ATTRIBUTE_REPARSE_POINT)return false;}
        else if(GetLastError()!=ERROR_FILE_NOT_FOUND&&GetLastError()!=ERROR_PATH_NOT_FOUND)return false;
    }return true;
}
bool ResourceDirectories(const std::filesystem::path& path){if(!SafeResourcePath(path))return false;std::error_code ec;std::filesystem::create_directories(path,ec);return !ec&&SafeResourcePath(path);}
std::string ReadResourceText(const std::filesystem::path& path,std::size_t maximum){auto file=Open(path);const auto size=Size(file.Get());if(size>maximum)Fail();std::string text(static_cast<std::size_t>(size),'\0');Read(file.Get(),text.data(),static_cast<DWORD>(text.size()));return text;}
bool WriteResourceText(const std::filesystem::path& path,std::string_view text) try {
    if(text.size()>16*1024*1024||!ResourceDirectories(path.parent_path())||!SafeResourcePath(path))return false;
    const auto nonce=plugins::ipc::RandomSecret();auto temp=path;temp+=L"."+std::wstring(nonce.begin(),nonce.end());
    Handle file(CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));if(!file)return false;
    DWORD written{};const bool ok=WriteFile(file.Get(),text.data(),static_cast<DWORD>(text.size()),&written,nullptr)&&written==text.size()&&FlushFileBuffers(file.Get());file.Reset();
    if(ok&&MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))return true;DeleteFileW(temp.c_str());return false;
}catch(...){return false;}
std::string ResourceHash(const std::filesystem::path& path,std::stop_token stop){auto file=Open(path);return Digest(file.Get(),stop);}
bool ValidPackagePath(std::string_view path){
    if(!path.starts_with("maps/")||path.size()>240||path.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._/")!=path.npos)return false;
    auto rest=path;while(!rest.empty()){
        const auto slash=rest.find('/');const auto part=rest.substr(0,slash);
        if(part.empty()||part.size()>80||part=="."||part==".."||part.back()=='.')return false;
        const auto stem=Fold(std::string(part.substr(0,part.find('.'))));
        if(stem=="con"||stem=="prn"||stem=="aux"||stem=="nul"||stem=="com1"||stem=="lpt1"||
            (stem.size()==4&&(stem.starts_with("com")||stem.starts_with("lpt"))&&stem[3]>='1'&&stem[3]<='9'))return false;
        if(slash==rest.npos)break;rest.remove_prefix(slash+1);if(rest.empty())return false;
    }
    const auto lower=Fold(std::string(path));return lower.ends_with(".png")||lower.ends_with(".tiles")||lower.ends_with(".json")||lower.ends_with(".svg")||lower.ends_with(".md")||lower.ends_with(".txt");
}
bool RemoveResourceTree(const std::filesystem::path& root,const std::filesystem::path& target) try {
    if(!Descendant(root,target)||!SafeResourcePath(root)||!SafeResourcePath(target))return false;
    if(!std::filesystem::exists(target))return true;
    for(const auto& entry:std::filesystem::recursive_directory_iterator(target)){
        if(!SafeResourcePath(entry.path()))return false;if(entry.is_regular_file()){auto file=Open(entry.path());(void)file;}
    }
    std::error_code ec;std::filesystem::remove_all(target,ec);return !ec&&!std::filesystem::exists(target);
}catch(...){return false;}
std::filesystem::path InstallPackage(const common::AppPaths& paths,const ResourceRecord& record,const std::filesystem::path& package,std::stop_token stop){
    if(!ValidMapIdentity(record.stableMapId)||record.resourceId!="maps."+record.stableMapId||!Hex(record.sha256)||!record.downloadSize||record.downloadSize>MaximumPackageBytes||!record.installedSize||record.installedSize>4*MaximumPackageBytes)Fail();
    auto input=Open(package);if(Size(input.Get())!=record.downloadSize||Digest(input.Get(),stop)!=record.sha256)Fail();
    LARGE_INTEGER zero{};if(!SetFilePointerEx(input.Get(),zero,nullptr,FILE_BEGIN))Fail();
    char magic[4]{};Read(input.Get(),magic,4);if(std::string_view(magic,4)!="NVR1")Fail();
    const auto idLength=Number(input.Get(),4);if(idLength>80)Fail();std::string id(static_cast<std::size_t>(idLength),'\0');Read(input.Get(),id.data(),static_cast<DWORD>(id.size()));if(id!=record.resourceId)Fail();
    const auto count=Number(input.Get(),4);if(!count||count>30000)Fail();
    const auto nonce=plugins::ipc::RandomSecret();const auto stage=paths.ResourceStaging()/std::filesystem::path(nonce);
    const auto final=paths.ResourceMaps()/record.stableMapId/record.sha256;
    if(!ResourceDirectories(stage)||!ResourceDirectories(final.parent_path()))Fail();
    try{
        std::set<std::string> names;std::uint64_t total{};std::string receipt="{\"schemaVersion\":1,\"resourceId\":"+raid::json::Quote(record.resourceId)+",\"sha256\":"+raid::json::Quote(record.sha256)+",\"files\":[";
        for(std::uint64_t index=0;index<count;++index){
            if(stop.stop_requested())Fail();const auto length=Number(input.Get(),4),size=Number(input.Get(),8);
            if(!length||length>240||!size||size>512ULL*1024*1024||size>record.installedSize-total)Fail();total+=size;
            std::string name(static_cast<std::size_t>(length),'\0');Read(input.Get(),name.data(),static_cast<DWORD>(length));if(!ValidPackagePath(name)||!names.insert(Fold(name)).second)Fail();
            const auto out=stage/std::filesystem::path(name);if(!ResourceDirectories(out.parent_path())||!SafeResourcePath(out))Fail();
            Handle output(CreateFileW(out.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));if(!output)Fail();
            Hash hash;std::array<char,65536> bytes{};std::uint64_t left=size;
            while(left){if(stop.stop_requested())Fail();const auto chunk=static_cast<DWORD>(std::min<std::uint64_t>(left,bytes.size()));Read(input.Get(),bytes.data(),chunk);DWORD written{};
                if(!WriteFile(output.Get(),bytes.data(),chunk,&written,nullptr)||written!=chunk)Fail();hash.Add(bytes.data(),chunk);left-=chunk;}
            if(!FlushFileBuffers(output.Get()))Fail();output.Reset();if(index)receipt+=',';
            receipt+="{\"path\":"+raid::json::Quote(name)+",\"size\":"+std::to_string(size)+",\"sha256\":"+raid::json::Quote(hash.Finish())+"}";
        }
        char tail{};DWORD read{};if(!ReadFile(input.Get(),&tail,1,&read,nullptr)||read||total!=record.installedSize)Fail();receipt+="]}";
        if(stop.stop_requested()||!WriteResourceText(stage/L"receipt.json",receipt))Fail();
        // 新版本目录一次发布，不覆盖旧版本；激活指针由服务在提交成功后原子更新。
        // Publish a new generation without overwriting old content; service atomically activates only after commit.
        if(std::filesystem::exists(final)){
            if(VerifyPackage(final,record,stop)){if(!RemoveResourceTree(paths.ResourceStaging(),stage))Fail();return final;}
            const auto damaged=paths.ResourceStaging()/(nonce+"-damaged");
            if(!SafeResourcePath(final)||!MoveFileExW(final.c_str(),damaged.c_str(),MOVEFILE_WRITE_THROUGH))Fail();
            if(!MoveFileExW(stage.c_str(),final.c_str(),MOVEFILE_WRITE_THROUGH)){
                MoveFileExW(damaged.c_str(),final.c_str(),MOVEFILE_WRITE_THROUGH);Fail();
            }
            RemoveResourceTree(paths.ResourceStaging(),damaged);return final;
        }
        if(!MoveFileExW(stage.c_str(),final.c_str(),MOVEFILE_WRITE_THROUGH))Fail();return final;
    }catch(...){RemoveResourceTree(paths.ResourceStaging(),stage);throw;}
}
bool VerifyPackage(const std::filesystem::path& root,const ResourceRecord& record,std::stop_token stop) try {
    const auto receipt=raid::json::Parser(ReadResourceText(root/L"receipt.json",16*1024*1024)).Parse();
    if(receipt.At("schemaVersion").Int()!=1||receipt.At("resourceId").String()!=record.resourceId||receipt.At("sha256").String()!=record.sha256)return false;
    const auto& files=receipt.At("files").Array();if(files.empty()||files.size()>30000)return false;std::set<std::string> names;std::uint64_t total{};
    // 从文件与目录重建包摘要；本地 receipt 被改写也不能伪造清单中的可信包哈希。
    // Reconstruct the package digest from files/index; an edited local receipt cannot forge the trusted manifest hash.
    Hash package;package.Add("NVR1",4);HashNumber(package,record.resourceId.size(),4);package.Add(record.resourceId.data(),static_cast<DWORD>(record.resourceId.size()));HashNumber(package,files.size(),4);
    for(const auto& item:files){const auto name=item.At("path").String(),hash=item.At("sha256").String();const auto size=item.At("size").Int();
        if(!ValidPackagePath(name)||!names.insert(Fold(name)).second||!Hex(hash)||size<=0||static_cast<std::uint64_t>(size)>record.installedSize-total)return false;
        auto file=Open(root/name);if(Size(file.Get())!=static_cast<std::uint64_t>(size))return false;
        HashNumber(package,name.size(),4);HashNumber(package,static_cast<std::uint64_t>(size),8);package.Add(name.data(),static_cast<DWORD>(name.size()));
        Hash content;std::array<char,65536> bytes{};DWORD read{};do{if(stop.stop_requested())return false;
            if(!ReadFile(file.Get(),bytes.data(),static_cast<DWORD>(bytes.size()),&read,nullptr))return false;package.Add(bytes.data(),read);content.Add(bytes.data(),read);}while(read);
        if(content.Finish()!=hash)return false;total+=static_cast<std::uint64_t>(size);}
    return total==record.installedSize&&package.Finish()==record.sha256;
}catch(...){return false;}
}
