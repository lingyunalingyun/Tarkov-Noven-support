#include "ui/PluginPageView.h"
#include "ui/PageTitle.h"
#include <iostream>
#include <stdexcept>
using namespace noven::ui;
using namespace noven::plugins;
void Check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
int main() try {
    Microsoft::WRL::ComPtr<IDWriteFactory> factory;Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(factory.GetAddressOf()))),"DWrite factory");
    Microsoft::WRL::ComPtr<IDWriteTextFormat> format;Check(SUCCEEDED(factory->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,16,L"zh-CN",&format)),"DWrite format");
    PluginOwnedPage page{"com.example.hello",1,{"dashboard","Hello",ParseUiDocument(R"({"schemaVersion":1,"blocks":[{"type":"heading","text":"Heading"},{"type":"text","text":"Text"},{"type":"keyValue","key":"Count","value":"0"},{"type":"badge","text":"Local"},{"type":"separator"},{"type":"button","id":"refresh","label":"Refresh"}]})")}};
    PluginPageView view;UiTheme theme;view.SetPage(page);view.Prepare(1280,760,theme,factory.Get(),format.Get(),format.Get());
    format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    view.Prepare(1000,760,theme,factory.Get(),format.Get(),format.Get());
    Check(view.TextInsideBlocks(),"centered application formats keep heading/text/keyValue/badge inside measured blocks");
    const auto button=view.ActionBounds("refresh");Check(button&&button->bottom>button->top,"all native blocks laid out with shared button geometry");
    view.Down(button->left+5,button->top+5);Check(view.Up(button->left+5,button->top+5)=="refresh","paired button action");
    view.Down(button->left+5,button->top+5);Check(!view.Up(button->left-5,button->top+5),"release elsewhere rejected");
    view.Down(button->left+5,button->top+5);page.page.document.blocks.pop_back();view.SetPage(page);view.Prepare(1280,760,theme,factory.Get(),format.Get(),format.Get());Check(!view.Up(button->left+5,button->top+5)&&!view.ActionBounds("refresh"),"document removal invalidates stale pressed button");
    PageDescriptor descriptor;descriptor.source=PageSource::Plugin;descriptor.displayTitle="原始名称";Check(PageTitle(descriptor)==L"原始名称","author-provided UTF-8 title not localized as key");
    page.page.document.blocks.assign(64,{BlockType::Text,std::string(256,'x'),{}, {}});view.SetPage(page);view.Prepare(700,300,theme,factory.Get(),format.Get(),format.Get());
    Check(view.TextInsideBlocks(),"wrapped text remains inside blocks after resize");
    Check(view.Wheel(-120,theme.sidebarWidth+theme.contentPadding+30,160)&&view.Animating(),"bounded document smooth-scrolls");
    for(int frame=0;frame<200&&view.Animating();++frame)view.Tick(.016F);Check(!view.Animating(),"scroll settles");
    ++page.generation;view.SetPage(page);Check(!view.Animating(),"new session resets retained input/scroll");
    std::cout<<"Declarative native plugin page view PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
