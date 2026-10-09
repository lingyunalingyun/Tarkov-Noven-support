#include "plugins/PluginRegistry.h"
#include <fstream>
#include <iostream>
using namespace noven::plugins;
void Check(bool ok,const char* label){if(!ok)throw std::runtime_error(label);}
template<class F> void Reject(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}Check(rejected,"invalid registry must reject");}
int wmain(int argc,wchar_t** argv) try {
    Check(argc==2,"fixture required");std::ifstream file(argv[1],std::ios::binary);std::string fixture(std::istreambuf_iterator<char>(file),{});
    const auto registry=ParsePluginRegistry(fixture);Check(registry.plugins.size()==1,"full fixture");const auto& p=registry.plugins[0];
    Check(p.metadata.id=="com.example.registry-test"&&!p.metadata.runtime&&p.package&&p.package->size==1024,"stable ID, read-only package, executable unknown fields ignored");
    Check(Compatibility(p)==RegistryCompatibility::Compatible,"explicit API and permission compatibility");
    Check(ParsePluginRegistry(R"({"registryVersion":1,"plugins":[]})").plugins.empty(),"valid empty registry");
    const auto edit=[&](const char* from,const char* to){auto text=fixture;const auto index=text.find(from);Check(index!=text.npos,"test replacement target");text.replace(index,std::string(from).size(),to);return text;};
    for(auto invalid:{edit("\"registryVersion\": 1","\"registryVersion\": 2"),edit("com.example.registry-test","builtin.fake"),edit("com.example.registry-test","plugin.fake"),edit("com.example.registry-test","Com.example.test"),edit("1.2.0-beta.1+fixture","01.2.3"),edit("network.http","bad permission"),edit("https://api.example.com","http://api.example.com"),edit("\"origins\": [\"https://api.example.com\"]","\"origins\": []")})Reject([&]{ParsePluginRegistry(invalid);});
    Reject([&]{ParsePluginRegistry("{");});Reject([&]{ParsePluginRegistry(std::string(MaximumRegistryBytes+1,' '));});
    const auto begin=fixture.find('{',fixture.find("\"plugins\"")),end=fixture.rfind('}');auto entry=fixture.substr(begin,fixture.rfind('}',end-1)-begin+1);
    Reject([&]{ParsePluginRegistry("{\"registryVersion\":1,\"plugins\":["+entry+","+entry+"]}");});
    std::string count="{\"registryVersion\":1,\"plugins\":[";for(unsigned i=0;i<MaximumRegistryPlugins+1;++i){if(i)count+=',';count+="{}";}count+="]}";Reject([&]{ParsePluginRegistry(count);});
    Reject([&]{ParsePluginRegistry(edit("Test author",std::string(257,'a').c_str()));});
    auto future=p;future.metadata.manifestVersion=3;Check(Compatibility(future)==RegistryCompatibility::Unknown,"future manifest compatibility unknown");future.metadata.manifestVersion=2;future.metadata.apiVersion=2;Check(Compatibility(future)==RegistryCompatibility::Incompatible,"API mismatch explicit");
    Check(ComparePluginVersions("1.10.0","1.2.0")>0&&ComparePluginVersions("1.2.0","1.2.0-beta")>0&&ComparePluginVersions("1.0.0-beta.2","1.0.0-beta.11")<0&&ComparePluginVersions("1.0.0+one","1.0.0+two")==0,"strict SemVer precedence");
    std::cout<<"Registry schema/identity/limits/forward fields/compatibility/SemVer PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
