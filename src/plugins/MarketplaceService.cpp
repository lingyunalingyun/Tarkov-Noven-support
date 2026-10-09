#include "plugins/MarketplaceService.h"
#include "plugins/PluginPipe.h"
#include "raid/RaidJson.h"
namespace noven::plugins {
namespace {
constexpr std::size_t MaximumCacheBytes=6*MaximumRegistryBytes+4096;
std::string ReadCache(const std::filesystem::path& path){
    ipc::Handle file(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    LARGE_INTEGER size{};BY_HANDLE_FILE_INFORMATION info{};
    if(!file||!GetFileInformationByHandle(file.Get(),&info)||(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))||info.nNumberOfLinks!=1
        ||!GetFileSizeEx(file.Get(),&size)||size.QuadPart<=0||size.QuadPart>MaximumCacheBytes)return {};
    std::string text(static_cast<std::size_t>(size.QuadPart),'\0');DWORD read{};
    return ReadFile(file.Get(),text.data(),static_cast<DWORD>(text.size()),&read,nullptr)&&read==text.size()?text:std::string{};
}
bool SaveCache(const std::filesystem::path& path,const std::string& text){
    if(text.size()>MaximumCacheBytes)return false;
    std::error_code error;std::filesystem::create_directories(path.parent_path(),error);if(error)return false;
    auto temporary=path;const auto secret=ipc::RandomSecret();temporary+=L"."+std::wstring(secret.begin(),secret.end());
    ipc::Handle file(CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));if(!file)return false;
    DWORD written{};const bool complete=WriteFile(file.Get(),text.data(),static_cast<DWORD>(text.size()),&written,nullptr)&&written==text.size()&&FlushFileBuffers(file.Get());file.Reset();
    if(complete&&MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))return true;
    DeleteFileW(temporary.c_str());return false;
}
}
MarketplaceService::MarketplaceService(std::filesystem::path cache,std::string source,std::unique_ptr<IRegistryTransport> transport,
    std::function<void()> changed,bool reviewFixture,Clock clock)
    :cache_(std::move(cache)),source_(std::move(source)),transport_(std::move(transport)),changed_(std::move(changed)),clock_(std::move(clock)){
    if(!clock_)clock_=[] {return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();};
    MarketplaceSnapshot initial;initial.reviewFixture=reviewFixture;
    if(transport_&&!source_.empty()){
        initial.state=MarketplaceState::Empty;
        try{
            const auto text=ReadCache(cache_);if(!text.empty()){
                const auto root=raid::json::Parser(text,true).Parse();
                if(root.At("cacheVersion").Int()!=1||root.At("source").String()!=source_)throw std::runtime_error("cache source/schema");
                initial.fetchedAt=root.At("fetchedAt").Int();if(initial.fetchedAt<=0||initial.fetchedAt>clock_()+300)throw std::runtime_error("cache timestamp");
                // 缓存保存原始有界 Registry JSON；载入必须再次验证，不能信任本地缓存元数据。
                // Cache preserves bounded original Registry JSON; reload revalidates it rather than trusting cached metadata.
                initial.registry=std::make_shared<PluginRegistry>(ParsePluginRegistry(root.At("registry").String()));
                initial.state=MarketplaceState::Cached;
            }
        }catch(const std::exception&){initial.registry.reset();initial.fetchedAt=0;}
    }
    snapshot_=std::make_shared<MarketplaceSnapshot>(std::move(initial));
    if(transport_&&!source_.empty())worker_=std::jthread([this](std::stop_token stop){Run(stop);});
}
MarketplaceService::~MarketplaceService(){worker_.request_stop();wake_.notify_all();if(worker_.joinable())worker_.join();}
std::shared_ptr<const MarketplaceSnapshot> MarketplaceService::Snapshot() const {std::lock_guard lock(mutex_);return snapshot_;}
void MarketplaceService::Notify() const {if(changed_)changed_();}
bool MarketplaceService::Refresh(bool explicitRequest){
    {
        std::lock_guard lock(mutex_);const auto now=clock_();
        if(!transport_||source_.empty()||snapshot_->state==MarketplaceState::Loading||(attempted_&&now-attemptedAt_<(explicitRequest?5:30))
            ||(!explicitRequest&&snapshot_->state==MarketplaceState::Live&&now-snapshot_->fetchedAt<900))return false;
        attempted_=true;attemptedAt_=now;requested_=true;auto next=std::make_shared<MarketplaceSnapshot>(*snapshot_);next->state=MarketplaceState::Loading;next->error=MarketplaceError::None;snapshot_=std::move(next);
    }
    wake_.notify_one();Notify();return true;
}
void MarketplaceService::Run(std::stop_token stop){
    for(;;){
        {std::unique_lock lock(mutex_);wake_.wait(lock,stop,[this]{return requested_;});if(stop.stop_requested())return;requested_=false;}
        MarketplaceError error=MarketplaceError::Fetch;std::shared_ptr<const PluginRegistry> registry;std::string text;
        try{text=transport_->Fetch(stop);error=MarketplaceError::InvalidRegistry;registry=std::make_shared<PluginRegistry>(ParsePluginRegistry(text));error=MarketplaceError::None;}catch(const std::exception&){}
        if(stop.stop_requested())return;
        const auto time=clock_();
        if(registry)try{
            const auto cache="{\"cacheVersion\":1,\"source\":"+raid::json::Quote(source_)+",\"fetchedAt\":"+std::to_string(time)+",\"registry\":"+raid::json::Quote(text)+"}";
            if(!SaveCache(cache_,cache))error=MarketplaceError::CacheWrite;
        }catch(const std::exception&){error=MarketplaceError::CacheWrite;}
        {
            std::lock_guard lock(mutex_);auto next=std::make_shared<MarketplaceSnapshot>(*snapshot_);next->error=error;
            if(registry){next->registry=std::move(registry);next->fetchedAt=time;next->state=MarketplaceState::Live;}
            else next->state=next->registry?MarketplaceState::Cached:MarketplaceState::Error;
            snapshot_=std::move(next);
        }
        Notify();
    }
}
}
