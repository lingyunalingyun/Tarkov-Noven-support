#include "updates/UpdateHttpsSource.h"
#include <iostream>
#include <array>
using namespace noven::resources;
namespace {
void Check(bool value){if(!value)throw std::runtime_error("HTTPS content assertion");}
template<class F>void Reject(F operation){bool rejected{};try{operation();}catch(const std::exception&){rejected=true;}Check(rejected);}
class Body final:public ContentBody {
    std::string data_;std::size_t at_{};
public:
    explicit Body(std::string data):data_(std::move(data)){}
    std::size_t Read(std::span<char> out,std::stop_token stop)override{if(stop.stop_requested())throw std::runtime_error("cancelled");const auto n=std::min(out.size(),data_.size()-at_);std::copy_n(data_.data()+at_,n,out.data());at_+=n;return n;}
};
class Backend final:public ContentBackend {
public:
    std::string bytes="abcdefghijk",rangeOverride,lengthOverride,url;unsigned status{};bool extra{},truncated{};std::vector<ContentRange> requests;
    ContentResponse Open(std::string_view input,std::optional<ContentRange> range,std::stop_token stop)override{
        if(stop.stop_requested())throw std::runtime_error("cancelled");url=input;if(!range)return {status?status:200,rangeOverride,lengthOverride,std::make_unique<Body>(bytes)};
        requests.push_back(*range);if(range->offset>bytes.size()||range->count>bytes.size()-range->offset)throw std::runtime_error("fixture range");
        auto data=bytes.substr(static_cast<std::size_t>(range->offset),static_cast<std::size_t>(range->count));if(extra)data+='x';if(truncated&&!data.empty())data.pop_back();
        return {status?status:206,rangeOverride.empty()?"bytes "+std::to_string(range->offset)+"-"+std::to_string(range->offset+range->count-1)+"/"+std::to_string(bytes.size()):rangeOverride,
            lengthOverride.empty()?std::to_string(range->count):lengthOverride,std::make_unique<Body>(std::move(data))};}
};
std::string Read(ResourceStream& stream){std::string result;std::array<char,65536> buffer{};while(const auto n=stream.Read(buffer,{}))result.append(buffer.data(),n);return result;}
}
int main()try{
    const ResourceSourcePolicy policy{"https://content.example.test/releases/"};auto backend=std::make_shared<Backend>();
    Check(!HttpsResourceTransport({})&&!noven::updates::HttpsUpdateSource({}));Check(backend->requests.empty());
    Check(static_cast<bool>(noven::updates::CompiledUpdateSource())==static_cast<bool>(NOVEN_EXPECT_UPDATE_SOURCE));
    auto source=noven::updates::HttpsUpdateSource(policy,backend);auto stream=source->Open("core-0.1.1.pack",2,5,"signed-identity",{});
    Check(stream->Offset()==2&&stream->TotalSize()==11&&stream->Identity()=="signed-identity"&&Read(*stream)=="cdefg");
    Check(backend->url==policy.baseUrl+"core-0.1.1.pack");Check(source->FetchManifest({})==backend->bytes);
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
    std::cout<<"content HTTPS policy/range/resource/update tests PASS (offline)\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
