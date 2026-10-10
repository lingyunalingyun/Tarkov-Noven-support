#include "ui/MapPage.h"
#include "resources/ResourceLocalTransport.h"
#include "resources/ResourceFiles.h"
#include <fstream>
#include <iostream>
using namespace noven;
namespace {
void Check(bool value){if(!value)throw std::runtime_error("map resource assertion");}
void Number(std::string& bytes,std::uint64_t value,unsigned count){for(unsigned i=0;i<count;++i)bytes+=static_cast<char>((value>>(8*i))&255);}
void Png(const std::filesystem::path& path){
    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;Microsoft::WRL::ComPtr<IWICStream> stream;Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame;
    Check(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)))&&SUCCEEDED(factory->CreateStream(&stream))&&SUCCEEDED(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE)));
    Check(SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder))&&SUCCEEDED(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache))&&SUCCEEDED(encoder->CreateNewFrame(&frame,nullptr))&&SUCCEEDED(frame->Initialize(nullptr))&&SUCCEEDED(frame->SetSize(1,1)));
    auto format=GUID_WICPixelFormat24bppBGR;BYTE pixel[3]{30,60,90};Check(SUCCEEDED(frame->SetPixelFormat(&format))&&SUCCEEDED(frame->WritePixels(1,3,3,pixel))&&SUCCEEDED(frame->Commit())&&SUCCEEDED(encoder->Commit()));
}
}
int wmain(int argc,wchar_t** argv) try {
    Check(argc==2);Check(SUCCEEDED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)));
    const auto root=std::filesystem::temp_directory_path()/("noven-map-resource-"+std::to_string(GetCurrentProcessId()));Check(!std::filesystem::exists(root));
    const auto paths=common::AppPaths::Test(root/"program",root/"test");const auto assets=paths.Assets();std::filesystem::create_directories(assets);
    std::filesystem::copy(std::filesystem::path(argv[1])/"data",assets/"data",std::filesystem::copy_options::recursive);
    ui::MapPage page;std::wstring error;Check(page.Initialize(assets,error,true)&&page.RealData());
    const auto& map=page.Catalog().Maps().front();Check(!map.floors.empty());page.SelectMap(map.id);Check(page.MissingResource());
    Png(root/"test.png");const auto png=resources::ReadResourceText(root/"test.png",4096);
    resources::ResourceRecord record;record.resourceId="maps."+map.id;record.stableMapId=map.id;record.titleZh=map.nameZh;record.titleEn=map.nameEn;record.version="1.0.0";record.artifact="synthetic.nvr";
    std::string bytes="NVR1";Number(bytes,record.resourceId.size(),4);bytes+=record.resourceId;Number(bytes,1,4);const auto name=map.floors.front().abstractPath;Check(resources::ValidPackagePath(name));
    Number(bytes,name.size(),4);Number(bytes,png.size(),8);bytes+=name+png;{std::ofstream out(root/record.artifact,std::ios::binary);out<<bytes;}
    record.downloadSize=bytes.size();record.installedSize=png.size();record.sha256=resources::ResourceHash(root/record.artifact);
    // 即使安装目录有同名图片，托管模式也不能回退到程序目录。
    // Managed mode never falls back to program imagery, even if a same-named file exists.
    std::filesystem::create_directories((assets/name).parent_path());std::filesystem::copy_file(root/"test.png",assets/name);
    resources::ResourceService service(paths,{{record}},resources::LocalResourceTransport(root),[]{return 1024*1024ULL;});
    page.SetResourceResolver([&](auto id){return service.ResolveMap(id);},[](auto){return ui::MapPage::ResourceInfo{};},{});Check(page.MissingResource());
    Check(service.Act(record.resourceId,resources::ResourceAction::Download)&&service.WaitIdle(std::chrono::seconds(10)));page.ResourcesChanged();Check(!page.MissingResource());
    Check(!service.Act(record.resourceId,resources::ResourceAction::Delete));page.ResourceActive(false);Check(service.Act(record.resourceId,resources::ResourceAction::Delete));Check(service.WaitIdle(std::chrono::seconds(10)));
    page.ResourceActive(true);Check(page.MissingResource());service.Shutdown();Check(!std::filesystem::exists(paths.Plugins())&&!std::filesystem::exists(paths.Data()));
    std::filesystem::remove_all(root);CoUninitialize();std::cout<<"map managed install/load/delete/missing/no-program-fallback/startup PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what();return 1;}
