#include "resources/ResourceManifest.h"
#include "common/AppPaths.h"
#include <iostream>
#include <stdexcept>
using namespace noven::resources;
namespace {
void Check(bool value){if(!value)throw std::runtime_error("resource assertion");}
std::string Record(){return R"({"type":"map","required":false,"resourceId":"maps.factory","stableMapId":"factory","titleZh":"Factory","titleEn":"Factory","version":"1.0.0","sha256":")"+std::string(64,'a')+R"(","artifact":"factory-1.0.0.nvr","downloadSize":1024,"installedSize":2048})";}
std::string Manifest(const std::string& record){return "{\"schemaVersion\":1,\"components\":["+record+"]}";}
std::string Replace(std::string text,std::string_view from,std::string_view to){const auto at=text.find(from);Check(at!=text.npos);text.replace(at,from.size(),to);return text;}
void Reject(const std::string& text){bool failed{};try{(void)ParseResourceManifest(text,{"factory"});}catch(const std::exception&){failed=true;}Check(failed);}
}
int main() try {
    const auto record=Record(),text=Manifest(record);
    const auto parsed=ParseResourceManifest(text,{"factory"});Check(parsed.components.size()==1&&parsed.components[0].resourceId=="maps.factory");
    Check(ParseResourceManifest(Replace(text,"\"schemaVersion\":1","\"future\":{\"entry\":\"evil.exe\"},\"schemaVersion\":1"),{"factory"}).components.size()==1);
    Reject(Replace(text,"\"schemaVersion\":1","\"schemaVersion\":2"));Reject("{");Reject(Manifest(record+","+record));
    Reject(Replace(text,"maps.factory","builtin.settings"));Reject(Replace(text,"\"stableMapId\":\"factory\"","\"stableMapId\":\"unknown\""));
    Reject(Replace(text,"\"required\":false","\"required\":true"));Reject(Replace(text,"\"type\":\"map\"","\"type\":\"exe\""));
    Reject(Replace(text,"1.0.0","invalid"));Reject(Replace(text,std::string(64,'a'),std::string(64,'z')));
    Reject(Replace(text,"\"downloadSize\":1024","\"downloadSize\":0"));Reject(Replace(text,"\"downloadSize\":1024","\"downloadSize\":2147483649"));
    Reject(Replace(text,"\"installedSize\":2048","\"installedSize\":-1"));Reject(std::string(MaximumManifestBytes+1,' '));
    for(const auto bad:{"../factory.nvr","a/b.nvr","a\\b.nvr","C:factory.nvr","https://example.com/a.nvr","file.dll","%TEMP%.nvr"})Check(!ValidArtifactName(bad));
    Reject(Replace(text,"factory-1.0.0.nvr","../factory.nvr"));
    Reject(Replace(text,"\"titleEn\":\"Factory\"","\"titleEn\":\""+std::string(257,'a')+"\""));
    std::string records;for(unsigned i=0;i<65;++i){if(i)records+=',';records+=record;}Reject(Manifest(records));
    auto malformed=text;malformed[malformed.find("Factory")]=static_cast<char>(0xff);Reject(malformed);
    Check(!ResourceSourcePolicy{}.Enabled());bool denied{};try{(void)ResourceSourcePolicy{}.DownloadUrl(parsed.components[0]);}catch(...){denied=true;}Check(denied);
    for(const auto bad:{"http://example.test/","https://localhost/","https://127.0.0.1/","https://user@example.test/","https://example.test/../","https://example.test/?x/","https://example.test:8443/","https://example.test/%2e/"})Check(!ResourceSourcePolicy{bad}.Enabled());
    const ResourceSourcePolicy injected{"https://resources.example.test/maps/"};Check(injected.Enabled());Check(injected.DownloadUrl(parsed.components[0])==injected.baseUrl+parsed.components[0].artifact);
    const auto root=std::filesystem::temp_directory_path()/"noven-resource-path-contract";
    const auto paths=noven::common::AppPaths::Test(root/"program",root/"test");
    Check(paths.Resources()==paths.userRoot/"resources"&&paths.ResourceMaps()==paths.Resources()/"maps");
    Check(paths.ResourceStaging()==paths.Resources()/"staging"&&paths.ResourceManifests()==paths.Resources()/"manifests");
    Check(paths.DownloadCache()==paths.userRoot/"downloads"/"cache"&&paths.Resources()!=paths.Plugins()&&paths.Resources()!=paths.Data());
    std::cout<<"resource manifest/source-policy/path contract PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what();return 1;}
