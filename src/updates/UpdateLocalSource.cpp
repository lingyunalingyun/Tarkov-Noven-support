#include "updates/UpdateLocalSource.h"
#include "resources/ResourceFiles.h"
#include <fstream>
namespace noven::updates {
namespace {
class Stream final:public resources::ResourceStream {
    std::ifstream file_;std::uint64_t offset_,total_,left_;std::string identity_;std::chrono::milliseconds delay_;std::mutex mutex_;std::condition_variable_any condition_;
public:
    Stream(const std::filesystem::path& path,std::uint64_t offset,std::uint64_t count,std::string identity,std::chrono::milliseconds delay):file_(path,std::ios::binary),offset_(offset),total_(std::filesystem::file_size(path)),left_(count),identity_(std::move(identity)),delay_(delay){
        if(!resources::SafeResourcePath(path)||!file_||offset>total_||count>total_-offset)throw std::runtime_error("invalid offline update Range");file_.seekg(static_cast<std::streamoff>(offset));}
    std::uint64_t Offset() const override{return offset_;}std::uint64_t TotalSize() const override{return total_;}std::string Identity()const override{return identity_;}
    std::size_t Read(std::span<char> out,std::stop_token stop)override {if(stop.stop_requested())throw std::runtime_error("cancelled offline Range");const auto n=static_cast<std::size_t>(std::min<std::uint64_t>(left_,std::min<std::size_t>(65536,out.size())));
        if(!n)return 0;if(delay_.count()){std::unique_lock lock(mutex_);condition_.wait_for(lock,stop,delay_,[]{return false;});if(stop.stop_requested())throw std::runtime_error("cancelled offline Range");}
        file_.read(out.data(),static_cast<std::streamsize>(n));if(file_.gcount()!=static_cast<std::streamsize>(n))throw std::runtime_error("truncated offline pack");left_-=n;return n;}
};
class Source final:public UpdateSource {
    std::filesystem::path root_;std::chrono::milliseconds delay_;
public:
    explicit Source(std::filesystem::path path,std::chrono::milliseconds delay):root_(std::move(path)),delay_(delay){if(!resources::SafeResourcePath(root_)||delay.count()<0||delay.count()>1000)throw std::runtime_error("unsafe offline update fixture");}
    std::string FetchManifest(std::stop_token stop)override{if(stop.stop_requested())throw std::runtime_error("cancelled");return resources::ReadResourceText(root_/L"release.json",2*MaximumManifestBytes+2048);}
    std::unique_ptr<resources::ResourceStream> Open(std::string_view pack,std::uint64_t offset,std::uint64_t count,std::string_view identity,std::stop_token stop)override{
        if(stop.stop_requested()||!ValidReleasePath(pack)||pack.find('/')!=pack.npos||!pack.ends_with(".pack"))throw std::runtime_error("invalid offline pack");return std::make_unique<Stream>(root_/std::string(pack),offset,count,std::string(identity),delay_);}
};
}
std::shared_ptr<UpdateSource> LocalUpdateSource(std::filesystem::path fixture,std::chrono::milliseconds delay){return std::make_shared<Source>(std::move(fixture),delay);}
}
