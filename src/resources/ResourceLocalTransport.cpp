#include "resources/ResourceLocalTransport.h"
#include "resources/ResourceFiles.h"
#include "plugins/PluginPipe.h"
namespace noven::resources {
namespace {
class Stream final:public ResourceStream {
    plugins::ipc::Handle file_;std::uint64_t offset_,total_;std::string identity_;
public:
    Stream(const std::filesystem::path& path,std::uint64_t offset,std::stop_token stop):offset_(offset),identity_(ResourceHash(path,stop)){
        if(!SafeResourcePath(path))throw std::runtime_error("unsafe fixture");
        file_=plugins::ipc::Handle(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        LARGE_INTEGER size{},seek{};BY_HANDLE_FILE_INFORMATION info{};
        if(!file_||!GetFileInformationByHandle(file_.Get(),&info)||info.nNumberOfLinks!=1||(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)||!GetFileSizeEx(file_.Get(),&size)||size.QuadPart<0||static_cast<std::uint64_t>(size.QuadPart)>MaximumPackageBytes)throw std::runtime_error("invalid fixture");
        total_=static_cast<std::uint64_t>(size.QuadPart);seek.QuadPart=static_cast<LONGLONG>(offset);
        if(offset>total_||!SetFilePointerEx(file_.Get(),seek,nullptr,FILE_BEGIN))throw std::runtime_error("invalid range");
    }
    std::uint64_t Offset() const override{return offset_;}
    std::uint64_t TotalSize() const override{return total_;}
    std::string Identity() const override{return identity_;}
    std::size_t Read(std::span<char> bytes,std::stop_token stop) override {
        if(stop.stop_requested())return 0;DWORD count{};
        if(bytes.size()>65536||!ReadFile(file_.Get(),bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr))throw std::runtime_error("fixture read failed");return count;
    }
};
class Transport final:public ResourceTransport {
    std::filesystem::path root_;
public:
    explicit Transport(std::filesystem::path root):root_(std::move(root)){if(!SafeResourcePath(root_))throw std::runtime_error("unsafe fixture root");}
    std::unique_ptr<ResourceStream> Open(const ResourceRecord& r,std::uint64_t offset,std::stop_token stop) override {
        if(!ValidArtifactName(r.artifact))throw std::runtime_error("unsafe fixture name");return std::make_unique<Stream>(root_/r.artifact,offset,stop);
    }
};
}
std::shared_ptr<ResourceTransport> LocalResourceTransport(std::filesystem::path root){return std::make_shared<Transport>(std::move(root));}
}
