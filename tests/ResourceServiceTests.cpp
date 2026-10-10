#include "resources/ResourceService.h"
#include "resources/ResourceFiles.h"
#include <fstream>
#include <iostream>
#include <windows.h>
using namespace noven::resources;
namespace {
void Check(bool value){if(!value)throw std::runtime_error("resource service assertion");}
void Number(std::string& bytes,std::uint64_t value,unsigned count){for(unsigned i=0;i<count;++i)bytes+=static_cast<char>((value>>(8*i))&255);}
std::string Package(const std::string& id){std::string value="NVR1";Number(value,id.size(),4);value+=id;Number(value,1,4);const std::string name="maps/synthetic/floor.png";Number(value,name.size(),4);Number(value,3,8);return value+name+"PNG";}
struct Mock final:ResourceTransport {
    std::mutex mutex;std::condition_variable_any changed;std::vector<std::uint64_t> offsets;std::string bytes;bool fail{},hold{},identityMismatch{};unsigned opens{},readBytes{};
    struct Stream final:ResourceStream {
        Mock& owner;ResourceRecord record;std::uint64_t start,position;
        Stream(Mock& o,ResourceRecord r,std::uint64_t offset):owner(o),record(std::move(r)),start(offset),position(offset){}
        std::uint64_t Offset() const override{return start;}
        std::uint64_t TotalSize() const override{return owner.bytes.size();}
        std::string Identity() const override{return owner.identityMismatch?std::string(64,'0'):record.sha256;}
        std::size_t Read(std::span<char> out,std::stop_token stop) override {
            {std::unique_lock lock(owner.mutex);if(!owner.changed.wait(lock,stop,[&]{return !owner.hold;}))return 0;}
            if(owner.fail&&position>=10)throw std::runtime_error("simulated interrupted transfer");
            const auto count=std::min<std::size_t>({10,out.size(),owner.bytes.size()-static_cast<std::size_t>(position)});
            std::copy_n(owner.bytes.data()+position,count,out.data());position+=count;{std::lock_guard lock(owner.mutex);owner.readBytes+=static_cast<unsigned>(count);}return count;
        }
    };
    std::unique_ptr<ResourceStream> Open(const ResourceRecord& r,std::uint64_t offset,std::stop_token) override {
        {std::lock_guard lock(mutex);offsets.push_back(offset);++opens;}changed.notify_all();return std::make_unique<Stream>(*this,r,offset);
    }
    bool WaitOpen(unsigned count){std::unique_lock lock(mutex);return changed.wait_for(lock,std::chrono::seconds(5),[&]{return opens>=count;});}
};
ResourceRecord Record(const std::filesystem::path& root,std::string id){
    ResourceRecord r;r.stableMapId=id;r.resourceId="maps."+id;r.titleZh=r.titleEn=id;r.version="1.0.0";r.artifact=id+".nvr";r.installedSize=3;
    const auto bytes=Package(r.resourceId);const auto file=root/(id+".nvr");{std::ofstream out(file,std::ios::binary);out<<bytes;}r.downloadSize=bytes.size();r.sha256=ResourceHash(file);return r;
}
ResourceSnapshot View(ResourceService& service){return service.Snapshot().front();}
void Idle(ResourceService& service){Check(service.WaitIdle(std::chrono::seconds(10)));}
}
int main() try {
    const auto root=std::filesystem::temp_directory_path()/("noven-service-"+std::to_string(GetCurrentProcessId()));Check(!std::filesystem::exists(root));std::filesystem::create_directories(root);
    const auto paths=noven::common::AppPaths::Test(root/"program",root/"test");const auto record=Record(root,"factory");auto transport=std::make_shared<Mock>();transport->bytes=Package(record.resourceId);
    {ResourceService service(paths,{{record}});Check(service.Remote()==RemoteAvailability::ProductionEndpointUnconfigured);Check(View(service).state==ResourceState::Unavailable);
        Check(!service.Act(record.resourceId,ResourceAction::Download)&&!service.Act("../../plugins",ResourceAction::Delete));Check(service.Snapshot().size()==1);}
    using S=ResourceState;for(const auto [a,b]:{std::pair{S::NotInstalled,S::Queued},{S::Queued,S::Downloading},{S::Downloading,S::Paused},{S::Paused,S::Queued},{S::Downloading,S::Verifying},{S::Verifying,S::Installing},{S::Installing,S::Installed},{S::Downloading,S::Error},{S::Verifying,S::Error},{S::Installing,S::Error},{S::Deleting,S::NotInstalled}})Check(LegalResourceTransition(a,b));
    Check(!LegalResourceTransition(S::NotInstalled,S::Installed)&&!LegalResourceTransition(S::Queued,S::Installing));
    transport->fail=true;
    {ResourceService service(paths,{{record}},transport,[]{return 1024*1024ULL;});Check(service.Act(record.resourceId,ResourceAction::Download));Idle(service);Check(View(service).state==S::Error&&View(service).received==10);}
    transport->fail=false;transport->readBytes=0;
    {ResourceService service(paths,{{record}},transport,[]{return 1024*1024ULL;});Check(View(service).state==S::Paused);Check(service.Act(record.resourceId,ResourceAction::Resume));Idle(service);Check(View(service).state==S::Installed);
        Check(transport->offsets.back()==10&&transport->readBytes==record.downloadSize-10);auto lease=service.ResolveMap(record.stableMapId);Check(lease&&VerifyPackage(*lease,record));
        Check(!service.Act(record.resourceId,ResourceAction::Delete));lease.reset();Check(service.Act(record.resourceId,ResourceAction::Verify));Idle(service);Check(View(service).state==S::Installed);}
    {ResourceService service(paths,{{record}});Idle(service);Check(View(service).state==S::Installed&&service.ResolveMap(record.stableMapId));
        Check(!service.Act(record.resourceId,ResourceAction::Repair)&&service.ResolveMap(record.stableMapId));Check(service.Act(record.resourceId,ResourceAction::Delete));Idle(service);Check(View(service).state==S::Unavailable&&!service.ResolveMap(record.stableMapId));}
    {ResourceService service(paths,{{record}},transport,[]{return 1ULL;});Check(service.Act(record.resourceId,ResourceAction::Download));Idle(service);Check(View(service).error=="resources.disk_space");}
    transport->hold=true;const auto previous=transport->opens;
    {ResourceService service(paths,{{record}},transport,[]{return 1024*1024ULL;});Check(service.Act(record.resourceId,ResourceAction::Download));Check(transport->WaitOpen(previous+1));
        Check(!service.Act(record.resourceId,ResourceAction::Download));Check(service.Act(record.resourceId,ResourceAction::Pause));Idle(service);Check(View(service).state==S::Paused);
        Check(service.Act(record.resourceId,ResourceAction::Cancel));Check(service.ClearDownloadCache());Idle(service);Check(View(service).state==S::NotInstalled);}
    transport->hold=false;transport->identityMismatch=true;
    {ResourceService service(paths,{{record}},transport,[]{return 1024*1024ULL;});service.DownloadAll();Idle(service);Check(View(service).state==S::Error&&!View(service).installed);}
    transport->identityMismatch=false;auto bad=record;bad.sha256=std::string(64,'a');
    {ResourceService service(paths,{{bad}},transport,[]{return 1024*1024ULL;});service.DownloadAll();Idle(service);Check(View(service).state==S::Error&&!service.ResolveMap(record.stableMapId));}
    // 新清单身份变化后不能复用旧局部下载；损坏元数据也只从零重试。
    // Changed manifest identities cannot reuse partial bytes; malformed metadata restarts at zero.
    transport->fail=true;{ResourceService service(paths,{{record}},transport,[]{return 1024*1024ULL;});service.DownloadAll();Idle(service);}
    auto next=record;next.version="1.0.1";transport->fail=false;
    {ResourceService service(paths,{{next}},transport,[]{return 1024*1024ULL;});Check(View(service).state==S::NotInstalled);service.DownloadAll();Idle(service);Check(transport->offsets.back()==0&&View(service).state==S::Installed);}
    {ResourceService service(paths,{}, {},{}, {},{{"factory","Factory","Factory"},{"woods","Woods","Woods"}});Idle(service);Check(service.Snapshot().size()==2);Check(View(service).state==S::Installed&&service.ResolveMap("factory"));Check(service.Snapshot().back().state==S::Unavailable&&service.Snapshot().back().record.downloadSize==0);Check(!service.Act("maps.factory",ResourceAction::Download));}
    auto update=next;update.version="1.0.2";update.sha256=std::string(64,'b');
    {ResourceService service(paths,{{update}},transport,[]{return 1024*1024ULL;});Idle(service);Check(View(service).state==S::UpdateAvailable);Check(service.Act(next.resourceId,ResourceAction::Download));Idle(service);
        Check(View(service).state==S::Error&&View(service).installed);auto old=service.ResolveMap(next.stableMapId);Check(old&&VerifyPackage(*old,next));}
    const auto safety=noven::common::AppPaths::Test(root/"safe-program",root/"safe-user");
    for(const auto& path:{safety.Plugins()/"keep.txt",safety.Data()/"settings.json",safety.Data()/"recent-scans.json",safety.Data()/"raid-history.json",safety.Data()/"plugin-storage"/"keep.txt",safety.Diagnostics()/"keep.log",safety.ResourceMaps()/"woods"/"keep.txt",safety.programRoot/"keep.txt"}){
        std::filesystem::create_directories(path.parent_path());Check(WriteResourceText(path,"preserved"));}
    {ResourceService service(safety,{{record}},transport,[]{return 1024*1024ULL;});service.DownloadAll();Idle(service);Check(View(service).state==S::Installed);Check(service.Act(record.resourceId,ResourceAction::Delete));Idle(service);Check(View(service).state==S::NotInstalled);}
    for(const auto& path:{safety.Plugins()/"keep.txt",safety.Data()/"settings.json",safety.Data()/"recent-scans.json",safety.Data()/"raid-history.json",safety.Data()/"plugin-storage"/"keep.txt",safety.Diagnostics()/"keep.log",safety.ResourceMaps()/"woods"/"keep.txt",safety.programRoot/"keep.txt"})Check(ReadResourceText(path,100)=="preserved");
    const auto partialPaths=noven::common::AppPaths::Test(root/"program",root/"partial");transport->fail=true;
    {ResourceService service(partialPaths,{{record}},transport,[]{return 1024*1024ULL;});service.DownloadAll();Idle(service);Check(View(service).received==10);}
    const auto partial=partialPaths.DownloadCache()/"resources"/(record.stableMapId+".part.json");Check(WriteResourceText(partial,"malformed"));transport->fail=false;
    {ResourceService service(partialPaths,{{record}},transport,[]{return 1024*1024ULL;});Check(View(service).state==S::NotInstalled);service.DownloadAll();Idle(service);Check(transport->offsets.back()==0&&View(service).state==S::Installed);}
    auto wrongSize=record;++wrongSize.downloadSize;
    {ResourceService service(noven::common::AppPaths::Test(root/"program",root/"wrong-size"),{{wrongSize}},transport,[]{return 1024*1024ULL;});service.DownloadAll();Idle(service);Check(View(service).state==S::Error&&!service.ResolveMap(record.stableMapId));}
    const auto queuePaths=noven::common::AppPaths::Test(root/"program",root/"queue");const auto woods=Record(root,"woods"),customs=Record(root,"customs");transport->hold=true;const auto opened=transport->opens;
    {ResourceService service(queuePaths,{{record,woods,customs}},transport,[]{return 1024*1024ULL;});service.DownloadAll();Check(transport->WaitOpen(opened+2));
        unsigned queued{};for(const auto& view:service.Snapshot())if(view.state==S::Queued){++queued;Check(service.Act(view.record.resourceId,ResourceAction::Pause));}Check(queued==1);
        for(const auto& view:service.Snapshot())if(view.state==S::Downloading)Check(service.Act(view.record.resourceId,ResourceAction::Pause));Idle(service);
        for(const auto& view:service.Snapshot())Check(view.state==S::Paused);Check(transport->opens==opened+2);service.Shutdown();Check(!service.Act(record.resourceId,ResourceAction::Resume));}
    Check(!std::filesystem::exists(paths.Plugins())&&!std::filesystem::exists(paths.Data())&&!std::filesystem::exists(paths.programRoot));
    std::filesystem::remove_all(root);std::cout<<"state machine/offline/resume/restart/hash/atomic install/delete/leases/cancel/disk/identity PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what();return 1;}
