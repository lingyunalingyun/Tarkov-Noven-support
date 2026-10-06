#include "ui/PluginsPage.h"
#include "ui/PageComponents.h"
#include "ui/NavigationButton.h"
#include <cmath>
#include <cwctype>

namespace noven::ui {
namespace {
std::wstring Wide(std::string_view text) {
    if(text.empty())return {};
    const auto length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0);
    if(!length)return {};
    std::wstring value(length,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),value.data(),length);return value;
}
std::wstring Diagnostic(const plugins::PluginDiagnostic& diagnostic) {
    return Tr(diagnostic.key)+(diagnostic.field.empty()?L"":L" · "+Wide(diagnostic.field));
}
}
std::wstring PluginStateText(plugins::PluginState state) {
    using plugins::PluginState;
    switch(state) {
    case PluginState::Valid:return Tr("plugins.discovered");
    case PluginState::InvalidManifest:return Tr("plugins.invalid");
    case PluginState::IncompatibleManifest:return Tr("plugins.incompatible_manifest");
    case PluginState::IncompatibleApi:return Tr("plugins.incompatible_api");
    case PluginState::DuplicateId:return Tr("plugins.conflict");
    case PluginState::UnsafePath:return Tr("plugins.unsafe_path");
    }
    return Tr("plugins.invalid");
}
std::wstring HostStateText(plugins::HostState state){
    using plugins::HostState;
    switch(state){
    case HostState::Starting:case HostState::Connecting:case HostState::Handshaking:case HostState::Loading:return Tr("plugins.starting");
    case HostState::Running:return Tr("plugins.running");
    case HostState::Stopping:return Tr("plugins.stopping");
    case HostState::Crashed:case HostState::ProtocolError:return Tr("plugins.crashed");
    default:return Tr("plugins.disabled");
    }
}
std::wstring PluginsPage::MarketplaceText() const {return Tr("plugins.marketplace_offline");}
std::array<TabBarItem<PluginCenterTab>,2> PluginsPage::Tabs() const {return {{{PluginCenterTab::Marketplace,marketplaceLabel_},{PluginCenterTab::MyPlugins,myLabel_}}};}
void PluginsPage::SelectTab(PluginCenterTab tab){
    if(tab==tab_)return;outgoingTab_=tab_;tab_=tab;underlineFrom_=underline_;tabProgress_=0;CancelDrag();search_.Blur();pressedFilter_.reset();pressedControl_.reset();pressedTab_.reset();
}
bool PluginsPage::Key(WPARAM key,bool control){if(tab_!=PluginCenterTab::MyPlugins||!search_.HandleKeyDown(key,control))return false;dirty_=true;scroll_=target_=0;CancelDrag();return true;}
bool PluginsPage::Char(wchar_t character){if(tab_!=PluginCenterTab::MyPlugins||search_.Text().size()>=256||!search_.HandleChar(character))return false;dirty_=true;scroll_=target_=0;CancelDrag();return true;}
D2D1_RECT_F PluginsPage::FilterBounds(PluginFilter filter) const {const float top=panel_.top+40+static_cast<int>(filter)*42.0F;return D2D1::RectF(panel_.left+8,top,panel_.right-8,top+36);}
std::vector<PluginPresentation> PresentPlugins(const plugins::PluginSnapshot& snapshot) {
    std::vector<PluginPresentation> rows;
    for(const auto& record:snapshot.records) {
        PluginPresentation row;row.status=PluginStateText(record.state);row.error=record.state==plugins::PluginState::InvalidManifest||record.state==plugins::PluginState::UnsafePath||record.state==plugins::PluginState::DuplicateId;
        row.incompatible=record.state==plugins::PluginState::IncompatibleApi||record.state==plugins::PluginState::IncompatibleManifest;
        row.title=record.manifest?Wide(record.manifest->name):record.directory.filename().wstring();
        for(auto& character:row.title)if(character<32||character==127)character=L' ';
        const auto line=[&](std::string_view key,const std::string& value){if(!value.empty())row.body+=Tr(key)+L": "+Wide(value)+L"\n";};
        if(record.manifest) {
            const auto& manifest=*record.manifest;
            row.id=manifest.id;
            row.body=Wide(manifest.id)+L"\n";
            line("plugins.version",manifest.version);line("plugins.author",manifest.author);
            row.body+=L"Manifest V"+std::to_wstring(manifest.manifestVersion)+L" · API "+std::to_wstring(manifest.apiVersion)+L" · "+row.status+L"\n";
            if(!manifest.description.empty())row.body+=Wide(manifest.description)+L"\n";
            line("plugins.homepage",manifest.homepage);line("plugins.source",manifest.source);line("plugins.license",manifest.license);
            row.body+=Tr("plugins.permissions")+L"\n";
            if(manifest.requestedPermissions.empty())row.body+=Tr("plugins.no_permissions")+L"\n";
            for(const auto& permission:manifest.requestedPermissions)row.body+=L"• "+Wide(permission)+L" · "+Tr(manifest.manifestVersion==2&&permission=="ui.page.register"?"plugins.supported":"plugins.unsupported")+L"\n";
            row.body+=Tr(manifest.manifestVersion==1?"plugins.metadata_only":"plugins.native_unverified");
            if(manifest.manifestVersion==2){
                row.body+=L"\n"+Tr("plugins.notice");
                row.enable=record.state==plugins::PluginState::Valid&&plugins::SupportedPermissions(manifest);
                row.incompatible=row.incompatible||!plugins::SupportedPermissions(manifest);
            }
        }
        for(const auto& diagnostic:record.diagnostics)row.body+=L"\n"+Diagnostic(diagnostic);
        rows.push_back(std::move(row));
    }
    return rows;
}
std::wstring PluginsPage::EmptyText() const {
    if(!snapshot_.diagnostics.empty())return Diagnostic(snapshot_.diagnostics.front());
    return snapshot_.records.empty()?Tr("plugins.empty"):Tr("plugins.no_matches");
}
std::optional<ScrollbarGeometry> PluginsPage::Bar() const {
    return MakeScrollbar(D2D1::RectF(viewport_.right-12,viewport_.top,viewport_.right,viewport_.bottom),content_,scroll_);
}
void PluginsPage::Prepare(float width,float height,const UiTheme& theme,IDWriteFactory* factory,IDWriteTextFormat* body,IDWriteTextFormat* label) {
    const float left=theme.sidebarWidth+theme.contentPadding,right=(std::max)(left+100,width-theme.contentPadding);
    const float bottom=(std::max)(154.0F,height-22),panelWidth=(std::min)(148.0F,(right-left)*.24F);
    panel_=D2D1::RectF(left,153,left+panelWidth,bottom);viewport_=D2D1::RectF(panel_.right+16,153,right,bottom);
    refresh_=D2D1::RectF(panel_.left+8,(std::max)(panel_.top+270,bottom-46),panel_.right-8,(std::max)(panel_.top+308,bottom-8));
    searchBounds_=D2D1::RectF((std::min)(left+330,right-100),95,right,133);
    marketplaceLabel_=Tr("plugins.marketplace");myLabel_=Tr("plugins.my_plugins");
    const float available=(std::max)(1.0F,viewport_.right-viewport_.left-52);
    if(dirty_||width_!=available||locale_!=UiLocalization().ActiveLocale()) {
        rows_=PresentPlugins(snapshot_);cards_.clear();content_=0;locale_=UiLocalization().ActiveLocale();width_=available;dirty_=false;
        for(auto& row:rows_) {
            if(row.enable){
                const auto session=std::find_if(runtime_.begin(),runtime_.end(),[&](const auto& item){return item.pluginId==row.id;});
                row.status=HostStateText(session==runtime_.end()?plugins::HostState::Stopped:session->state);
                if(session!=runtime_.end()){
                    row.disable=!plugins::Terminal(session->state);row.enable=!row.disable;
                    if(session->error!=plugins::HostError::None){row.error=true;row.body+=L"\n"+Tr("plugins.runtime_error")+L" · "+std::to_wstring(static_cast<int>(session->error))+L" / "+std::to_wstring(session->loadResult);}
                }
            }
        }
        auto query=search_.Text();for(auto& c:query)c=std::towlower(c);
        std::erase_if(rows_,[&](const auto& row){auto text=row.title+L"\n"+row.body;for(auto& c:text)c=std::towlower(c);
            return (!query.empty()&&text.find(query)==std::wstring::npos)||(filter_==PluginFilter::Running&&row.status!=Tr("plugins.running"))
                ||(filter_==PluginFilter::Disabled&&row.status!=Tr("plugins.disabled"))||(filter_==PluginFilter::Error&&!row.error)||(filter_==PluginFilter::Incompatible&&!row.incompatible);});
        for(const auto& row:rows_) {
            Card card;card.top=content_;
            const auto layout=[&](const std::wstring& text,IDWriteTextFormat* format,Microsoft::WRL::ComPtr<IDWriteTextLayout>& output) {
                if(!factory||!format||FAILED(factory->CreateTextLayout(text.data(),static_cast<UINT32>(text.size()),format,available,100000,&output)))return 24.0F;
                // 页面格式垂直居中；卡片必须顶对齐并收缩布局高度，否则文字被排到测量框中部。
                // Shared page formats are vertically centered; card layouts must top-align and fit measured height.
                output->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);output->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                output->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);DWRITE_TEXT_METRICS metrics{};
                const float measured=SUCCEEDED(output->GetMetrics(&metrics))?(std::max)(24.0F,metrics.height):24.0F;output->SetMaxHeight(measured);return measured;
            };
            card.titleHeight=layout(row.title,label,card.title);const float bodyHeight=layout(row.body,body,card.body);
            card.height=24+card.titleHeight+30+bodyHeight+18+(row.enable||row.disable?54.0F:0.0F);content_+=card.height+12;cards_.push_back(std::move(card));
        }
    }
    const float maximum=(std::max)(0.0F,content_-(viewport_.bottom-viewport_.top));scroll_=std::clamp(scroll_,0.0F,maximum);target_=std::clamp(target_,0.0F,maximum);
}
bool PluginsPage::TextInsideCards() const {for(const auto& card:cards_)for(auto* text:{card.title.Get(),card.body.Get()})if(text){DWRITE_TEXT_METRICS m{};if(FAILED(text->GetMetrics(&m))||m.top<0||m.top+m.height>text->GetMaxHeight()+1)return false;}return true;}
void PluginsPage::Draw(const UiCanvas& canvas,const UiTheme& theme) const {
    DrawPageHeader(canvas,theme,panel_.left,viewport_.right,Tr("nav.plugins"));
    const auto tabs=Tabs();DrawTabBar(canvas,theme,canvas.body,tabs,TabLayout(),tab_,hoveredTab_,outgoingTab_,tabProgress_,underline_);
    search_.Draw(canvas,theme,searchBounds_,Tr(tab_==PluginCenterTab::MyPlugins?"plugins.search":"plugins.marketplace_offline"),true);
    canvas.Round(panel_,theme.cornerRadius,theme.surface);canvas.Round(viewport_,theme.cornerRadius,theme.surface);
    canvas.target.PushAxisAlignedClip(panel_,D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    canvas.Text(Tr(tab_==PluginCenterTab::Marketplace?"plugins.category":"plugins.filters"),canvas.label,D2D1::RectF(panel_.left+12,panel_.top+6,panel_.right-12,panel_.top+36),theme.primaryText);
    const std::array<std::string_view,5> filterKeys{"plugins.all","plugins.running","plugins.disabled","plugins.error_filter","plugins.incompatible_filter"};
    for(int i=0;i<(tab_==PluginCenterTab::Marketplace?1:5);++i)DrawTextButton(canvas,theme,FilterBounds(static_cast<PluginFilter>(i)),Tr(filterKeys[i]),false,tab_==PluginCenterTab::Marketplace?i==0:filter_==static_cast<PluginFilter>(i));
    if(tab_==PluginCenterTab::MyPlugins)DrawTextButton(canvas,theme,refresh_,Tr("plugins.refresh"),hovered_,pressed_);
    canvas.target.PopAxisAlignedClip();
    if(tab_==PluginCenterTab::Marketplace){canvas.Text(MarketplaceText(),canvas.body,D2D1::RectF(viewport_.left+16,viewport_.top+16,viewport_.right-16,viewport_.top+100),theme.secondaryText);return;}
    canvas.target.PushAxisAlignedClip(viewport_,D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    if(rows_.empty())canvas.Text(EmptyText(),canvas.body,viewport_,theme.secondaryText);
    for(std::size_t index=0;index<cards_.size();++index) {
        const auto& card=cards_[index];const float top=viewport_.top+card.top-scroll_;
        if(top+card.height<viewport_.top||top>viewport_.bottom)continue;
        const auto bounds=D2D1::RectF(viewport_.left,top,viewport_.right-20,top+card.height);
        DrawListCardSurface(canvas,theme,bounds);
        canvas.brush.SetColor(theme.primaryText);
        if(card.title)canvas.target.DrawTextLayout(D2D1::Point2F(bounds.left+16,top+12),card.title.Get(),&canvas.brush,D2D1_DRAW_TEXT_OPTIONS_CLIP);
        canvas.Text(rows_[index].status,canvas.smallFormat,D2D1::RectF(bounds.left+16,top+12+card.titleHeight,bounds.right-16,top+42+card.titleHeight),theme.accent);
        canvas.brush.SetColor(theme.secondaryText);
        if(card.body)canvas.target.DrawTextLayout(D2D1::Point2F(bounds.left+16,top+48+card.titleHeight),card.body.Get(),&canvas.brush,D2D1_DRAW_TEXT_OPTIONS_CLIP);
        if(const auto button=ControlBounds(rows_[index].id))DrawTextButton(canvas,theme,*button,Tr(rows_[index].disable?"plugins.disable":"plugins.enable"),false,pressedControl_==index);
    }
    canvas.target.PopAxisAlignedClip();DrawScrollbar(canvas,theme,{Bar(),1});
}
void PluginsPage::Down(float x,float y) {
    if(tab_==PluginCenterTab::MyPlugins&&search_.HitTest(searchBounds_,x,y))search_.Focus();else search_.Blur();
    pressedFilter_.reset();if(tab_==PluginCenterTab::MyPlugins&&HitNavigationButton(panel_,x,y))for(int i=0;i<5;++i)if(HitNavigationButton(FilterBounds(static_cast<PluginFilter>(i)),x,y))pressedFilter_=static_cast<PluginFilter>(i);
    pressedTab_=HitTestTabBar(Tabs(),TabLayout(),x,y);pressedControl_=ControlAt(x,y);
    pressed_=tab_==PluginCenterTab::MyPlugins&&HitNavigationButton(panel_,x,y)&&HitNavigationButton(refresh_,x,y);
    if(const auto bar=Bar();bar&&HitNavigationButton(bar->thumb,x,y)){grab_=y-bar->thumb.top;target_=scroll_;}
}
bool PluginsPage::Up(float x,float y) {
    const auto filter=pressedFilter_;pressedFilter_.reset();if(filter&&HitNavigationButton(panel_,x,y)&&HitNavigationButton(FilterBounds(*filter),x,y))SelectFilter(*filter);
    const auto tab=HitTestTabBar(Tabs(),TabLayout(),x,y);if(tab&&tab==pressedTab_)SelectTab(*tab);pressedTab_.reset();
    const auto control=ControlAt(x,y);if(control&&control==pressedControl_)controlAction_=PluginControlAction{rows_[*control].id,rows_[*control].enable};pressedControl_.reset();
    const bool refresh=pressed_&&tab_==PluginCenterTab::MyPlugins&&HitNavigationButton(panel_,x,y)&&HitNavigationButton(refresh_,x,y);pressed_=false;grab_.reset();return refresh;
}
std::optional<D2D1_RECT_F> PluginsPage::ControlBounds(std::string_view id) const {
    if(tab_!=PluginCenterTab::MyPlugins)return {};for(std::size_t i=0;i<cards_.size();++i)if(rows_[i].id==id&&(rows_[i].enable||rows_[i].disable)){
        const float bottom=viewport_.top+cards_[i].top+cards_[i].height-scroll_-14;return D2D1::RectF(viewport_.left+16,bottom-38,viewport_.left+128,bottom);
    }return {};
}
std::optional<std::size_t> PluginsPage::ControlAt(float x,float y) const {
    if(!HitNavigationButton(viewport_,x,y))return {};for(std::size_t i=0;i<rows_.size();++i)if(const auto bounds=ControlBounds(rows_[i].id);bounds&&HitNavigationButton(*bounds,x,y))return i;return {};
}
bool PluginsPage::Move(float x,float y) {
    const auto tab=HitTestTabBar(Tabs(),TabLayout(),x,y);const bool changed=tab!=hoveredTab_;hoveredTab_=tab;
    if(grab_)if(const auto bar=Bar()){target_=scroll_=bar->OffsetFromThumbTop(y-*grab_);return true;}
    const bool hover=tab_==PluginCenterTab::MyPlugins&&HitNavigationButton(refresh_,x,y);if(hover==hovered_)return changed;hovered_=hover;return true;
}
bool PluginsPage::Wheel(int delta,float x,float y) {
    if(tab_!=PluginCenterTab::MyPlugins||!HitNavigationButton(viewport_,x,y)||grab_)return false;
    target_=std::clamp(target_-static_cast<float>(delta)/WHEEL_DELTA*66,0.0F,(std::max)(0.0F,content_-(viewport_.bottom-viewport_.top)));return Animating();
}
void PluginsPage::Tick(float elapsed) {
    tabProgress_=std::clamp(tabProgress_+(std::max)(0.0F,elapsed)/.36F,0.0F,1.0F);const float destination=tab_==PluginCenterTab::Marketplace?0.0F:1.0F;
    underline_=underlineFrom_+(destination-underlineFrom_)*SampleTabTransition(tabProgress_).underlineProgress;
    scroll_+=(target_-scroll_)*(1-std::exp(-16*(std::max)(0.0F,elapsed)));if(std::abs(scroll_-target_)<.5F)scroll_=target_;
}
}
