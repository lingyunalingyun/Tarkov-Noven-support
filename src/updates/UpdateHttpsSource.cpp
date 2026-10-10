#include "updates/UpdateHttpsSource.h"
namespace noven::updates {
namespace {
class Source final:public UpdateSource {
    resources::ResourceSourcePolicy policy_;std::shared_ptr<resources::ContentBackend> backend_;
public:
    Source(resources::ResourceSourcePolicy policy,std::shared_ptr<resources::ContentBackend> backend):policy_(std::move(policy)),backend_(std::move(backend)){}
    std::string FetchManifest(std::stop_token stop)override{return resources::FetchContentText(policy_,"release.json",2*MaximumManifestBytes+2048,backend_,stop);}
    std::unique_ptr<resources::ResourceStream> Open(std::string_view pack,std::uint64_t offset,std::uint64_t count,std::string_view identity,std::stop_token stop)override{
        if(!ValidReleasePath(pack)||pack.find('/')!=pack.npos||!pack.ends_with(".pack"))throw std::runtime_error("invalid HTTPS update pack");
        return resources::OpenContentRange(policy_,pack,{offset,count},std::string(identity),0,backend_,stop);}
};
}
std::shared_ptr<UpdateSource> HttpsUpdateSource(resources::ResourceSourcePolicy policy,std::shared_ptr<resources::ContentBackend> backend){
    if(policy.baseUrl.empty())return {};if(policy.baseUrl.size()>512||!policy.Enabled())throw std::runtime_error("invalid first-party update source");
    return std::make_shared<Source>(std::move(policy),backend?std::move(backend):resources::WindowsContentBackend(true));}
std::shared_ptr<UpdateSource> CompiledUpdateSource(){return HttpsUpdateSource({NOVEN_UPDATE_SOURCE_ROOT});}
}
