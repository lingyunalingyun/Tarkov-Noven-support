#include "updates/UpdateHttpsSource.h"
#include <iostream>
#include <array>
using namespace noven::resources;
namespace {
void Check(bool value){if(!value)throw std::runtime_error("HTTPS content assertion");}
template<class F>void Reject(F operation){bool rejected{};try{operation();}catch(const std::exception&){rejected=true;}Check(rejected);}
class Body final:public ContentBody {
    std::string data_;std::size_t at_{};
    bool fail_;std::size_t failAt_;unsigned code_;
public:
    explicit Body(std::string data,bool fail=false,std::size_t after=0,unsigned code=12002):data_(std::move(data)),fail_(fail),failAt_(after),code_(code){}
    std::size_t Read(std::span<char> out,std::stop_token stop)override{if(stop.stop_requested())throw std::runtime_error("cancelled");
        if(fail_&&at_>=failAt_)throw ContentNetworkError("injected body failure",code_);
        auto n=std::min(out.size(),data_.size()-at_);if(fail_)n=std::min(n,failAt_-at_);std::copy_n(data_.data()+at_,n,out.data());at_+=n;return n;}
};
class Backend final:public ContentBackend {
public:
    std::string bytes="abcdefghijk",rangeOverride,lengthOverride,url;unsigned status{};bool extra{},truncated{};std::vector<ContentRange> requests;
    unsigned readRetries{},failuresLeft{},code{12002},notices{};std::size_t failAt{};std::stop_source* cancelRetry{};
    unsigned ReadRetries()const override{return readRetries;}
    void ReadRetry(ContentRange,unsigned,unsigned)override{++notices;if(cancelRetry)cancelRetry->request_stop();}
    ContentResponse Open(std::string_view input,std::optional<ContentRange> range,std::stop_token stop)override{
        if(stop.stop_requested())throw std::runtime_error("cancelled");url=input;if(!range)return {status?status:200,rangeOverride,lengthOverride,std::make_unique<Body>(bytes)};
        requests.push_back(*range);if(range->offset>bytes.size()||range->count>bytes.size()-range->offset)throw std::runtime_error("fixture range");
        auto data=bytes.substr(static_cast<std::size_t>(range->offset),static_cast<std::size_t>(range->count));if(extra)data+='x';if(truncated&&!data.empty())data.pop_back();
        const bool fail=failuresLeft!=0;if(fail)--failuresLeft;
        return {status?status:206,rangeOverride.empty()?"bytes "+std::to_string(range->offset)+"-"+std::to_string(range->offset+range->count-1)+"/"+std::to_string(bytes.size()):rangeOverride,
            lengthOverride.empty()?std::to_string(range->count):lengthOverride,std::make_unique<Body>(std::move(data),fail,failAt,code)};}
};
std::string Read(ResourceStream& stream,std::stop_token stop={}){std::string result;std::array<char,65536> buffer{};while(const auto n=stream.Read(buffer,stop))result.append(buffer.data(),n);return result;}
}
int main()try{
    const ResourceSourcePolicy policy{"https://content.example.test/releases/"};auto backend=std::make_shared<Backend>();
    Check(!HttpsResourceTransport({})&&!noven::updates::HttpsUpdateSource({}));Check(backend->requests.empty());
    Check(static_cast<bool>(noven::updates::CompiledUpdateSource())==static_cast<bool>(NOVEN_EXPECT_UPDATE_SOURCE));
    auto source=noven::updates::HttpsUpdateSource(policy,backend);auto stream=source->Open("core-0.1.1.pack",2,5,"signed-identity",{});
    Check(stream->Offset()==2&&stream->TotalSize()==11&&stream->Identity()=="signed-identity"&&Read(*stream)=="cdefg");
    Check(backend->url==policy.baseUrl+"core-0.1.1.pack");Check(source->FetchManifest({})==backend->bytes);
    Check(source->FetchVersionManifest("0.1.3",{})==backend->bytes);
    Check(backend->url==policy.baseUrl+"release-0.1.3.json");
    for(const auto bad:{"../0.1.3","https://evil.test/0.1.3","0.1.3?x","0.1.3-beta"})Reject([&]{(void)source->FetchVersionManifest(bad,{});});
    for(const auto bad:{"../a.pack","C:a.pack","a/b.pack","https://evil.test/a.pack","a.exe"})Reject([&]{(void)source->Open(bad,0,1,"identity",{});});
    Reject([&]{(void)noven::updates::HttpsUpdateSource({"http://content.example.test/"},backend);});
    for(const auto status:{200U,301U,302U,404U}){backend->status=status;Reject([&]{(void)source->Open("a.pack",2,5,"identity",{});});}backend->status=0;
    for(const auto range:{"bytes 3-6/11","bytes 2-7/11","bytes 2-6/*","bytes 2-6/6","bytes 2-6/8589934593","bytes 2-6/18446744073709551616","bytes 2-6/11x","garbage"}){
        backend->rangeOverride=range;Reject([&]{(void)source->Open("a.pack",2,5,"identity",{});});}backend->rangeOverride.clear();
    backend->lengthOverride="6";Reject([&]{(void)source->Open("a.pack",2,5,"identity",{});});backend->lengthOverride.clear();
    backend->extra=true;Reject([&]{auto s=source->Open("a.pack",2,5,"identity",{});(void)Read(*s);});backend->extra=false;
    backend->truncated=true;Reject([&]{auto s=source->Open("a.pack",2,5,"identity",{});(void)Read(*s);});backend->truncated=false;
    std::stop_source stop;stop.request_stop();Reject([&]{(void)source->Open("a.pack",2,5,"identity",stop.get_token());});
    Reject([&]{(void)FetchContentText(policy,"release.json",2,backend,{});});backend->status=302;Reject([&]{(void)source->FetchManifest({});});backend->status=0;
    backend->lengthOverride="12";Reject([&]{(void)source->FetchManifest({});});backend->lengthOverride.clear();
    ResourceRecord record;record.artifact="factory.nvr";record.downloadSize=backend->bytes.size();record.sha256=std::string(64,'a');
    auto transport=HttpsResourceTransport(policy,backend);stream=transport->Open(record,4,{});Check(Read(*stream)=="efghijk"&&stream->Identity()==record.sha256);
    record.downloadSize=12;Reject([&]{(void)transport->Open(record,4,{});});record.downloadSize=11;
    backend->bytes=std::string(static_cast<std::size_t>(ContentRangeBytes+9),'z');backend->requests.clear();
    stream=source->Open("a.pack",0,backend->bytes.size(),"identity",{});Check(Read(*stream)==backend->bytes);Check(backend->requests.size()==2&&backend->requests[1].offset==ContentRangeBytes&&backend->requests[1].count==9);
    auto retry=std::make_shared<Backend>();auto resource=HttpsResourceTransport(policy,retry);record.downloadSize=11;
    // 更新默认策略不重试；资源仅重取尚未交付的尾段，保留已交付字节。
    // Update defaults remain unchanged; opted-in resources request only undelivered tails.
    retry->failuresLeft=1;retry->failAt=3;
    Reject([&]{auto s=resource->Open(record,0,{});(void)Read(*s);});Check(retry->requests.size()==1);
    retry->readRetries=2;retry->failuresLeft=1;retry->requests.clear();
    auto resumed=resource->Open(record,0,{});Check(Read(*resumed)==retry->bytes&&retry->requests.size()==2&&retry->requests[1].offset==3&&retry->requests[1].count==8);
    retry->failuresLeft=1;retry->failAt=11;retry->requests.clear();
    resumed=resource->Open(record,0,{});Check(Read(*resumed)==retry->bytes&&retry->requests.size()==2&&retry->requests[1].offset==0);
    retry->failuresLeft=9;retry->failAt=0;retry->requests.clear();
    Reject([&]{auto s=resource->Open(record,0,{});(void)Read(*s);});Check(retry->requests.size()==3);
    retry->failuresLeft=1;retry->code=12175;retry->requests.clear();
    Reject([&]{auto s=resource->Open(record,0,{});(void)Read(*s);});Check(retry->requests.size()==1);
    retry->code=12002;retry->failuresLeft=1;retry->failAt=3;retry->requests.clear();
    resumed=resource->Open(record,0,{});std::array<char,3> prefix{};Check(resumed->Read(prefix,{})==3);
    retry->rangeOverride="bytes 0-7/11";Reject([&]{(void)Read(*resumed);});Check(retry->requests.size()==2);retry->rangeOverride.clear();
    retry->failuresLeft=0;retry->truncated=true;retry->requests.clear();Reject([&]{auto s=resource->Open(record,0,{});(void)Read(*s);});Check(retry->requests.size()==1);retry->truncated=false;
    std::stop_source interrupted;retry->cancelRetry=&interrupted;retry->failuresLeft=1;retry->failAt=0;retry->requests.clear();
    Reject([&]{auto s=resource->Open(record,0,interrupted.get_token());(void)Read(*s,interrupted.get_token());});Check(retry->requests.size()==1&&interrupted.stop_requested());
    std::cout<<"content HTTPS policy/range/resource/update tests PASS (offline)\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
