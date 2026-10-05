#include "ui/PluginsPage.h"
#include "ui/PageComponents.h"
#include "ui/NavigationButton.h"
#include <cmath>

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
std::vector<PluginPresentation> PresentPlugins(const plugins::PluginSnapshot& snapshot) {
    std::vector<PluginPresentation> rows;
    for(const auto& record:snapshot.records) {
        PluginPresentation row;row.status=PluginStateText(record.state);
        row.title=record.manifest?Wide(record.manifest->name):record.directory.filename().wstring();
        for(auto& character:row.title)if(character<32||character==127)character=L' ';
        const auto line=[&](std::string_view key,const std::string& value){if(!value.empty())row.body+=Tr(key)+L": "+Wide(value)+L"\n";};
        if(record.manifest) {
            const auto& manifest=*record.manifest;
            row.body=Wide(manifest.id)+L"\n";
            line("plugins.version",manifest.version);line("plugins.author",manifest.author);
            row.body+=Tr("plugins.compatibility")+L": "+std::to_wstring(manifest.manifestVersion)+L" / "+std::to_wstring(manifest.apiVersion)+L"\n";
            if(!manifest.description.empty())row.body+=L"\n"+Wide(manifest.description)+L"\n";
            line("plugins.homepage",manifest.homepage);line("plugins.source",manifest.source);line("plugins.license",manifest.license);
            row.body+=L"\n"+Tr("plugins.permissions")+L"\n";
            if(manifest.requestedPermissions.empty())row.body+=Tr("plugins.no_permissions")+L"\n";
            for(const auto& permission:manifest.requestedPermissions)row.body+=L"• "+Wide(permission)+L"\n";
        }
        for(const auto& diagnostic:record.diagnostics)row.body+=L"\n"+Diagnostic(diagnostic);
        rows.push_back(std::move(row));
    }
    return rows;
}
std::wstring PluginsPage::EmptyText() const {
    if(!snapshot_.diagnostics.empty())return Diagnostic(snapshot_.diagnostics.front());
    return snapshot_.records.empty()?Tr("plugins.empty"):std::wstring{};
}
std::optional<ScrollbarGeometry> PluginsPage::Bar() const {
    return MakeScrollbar(D2D1::RectF(viewport_.right-12,viewport_.top,viewport_.right,viewport_.bottom),content_,scroll_);
}
void PluginsPage::Prepare(float width,float height,const UiTheme& theme,IDWriteFactory* factory,IDWriteTextFormat* body,IDWriteTextFormat* label) {
    const float left=theme.sidebarWidth+theme.contentPadding,right=(std::max)(left+100,width-theme.contentPadding);
    refresh_=D2D1::RectF(right-112,95,right,133);viewport_=D2D1::RectF(left,165,right,(std::max)(166.0F,height-22));
    const float available=right-left-56;
    if(dirty_||width_!=available||locale_!=UiLocalization().ActiveLocale()) {
        rows_=PresentPlugins(snapshot_);cards_.clear();content_=0;locale_=UiLocalization().ActiveLocale();width_=available;dirty_=false;
        for(const auto& row:rows_) {
            Card card;card.top=content_;
            const auto layout=[&](const std::wstring& text,IDWriteTextFormat* format,Microsoft::WRL::ComPtr<IDWriteTextLayout>& output) {
                if(!factory||!format||FAILED(factory->CreateTextLayout(text.data(),static_cast<UINT32>(text.size()),format,available,100000,&output)))return 24.0F;
                output->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);DWRITE_TEXT_METRICS metrics{};
                return SUCCEEDED(output->GetMetrics(&metrics))?(std::max)(24.0F,metrics.height):24.0F;
            };
            card.titleHeight=layout(row.title,label,card.title);const float bodyHeight=layout(row.body,body,card.body);
            card.height=24+card.titleHeight+30+bodyHeight+18;content_+=card.height+12;cards_.push_back(std::move(card));
        }
    }
    const float maximum=(std::max)(0.0F,content_-(viewport_.bottom-viewport_.top));scroll_=std::clamp(scroll_,0.0F,maximum);target_=std::clamp(target_,0.0F,maximum);
}
void PluginsPage::Draw(const UiCanvas& canvas,const UiTheme& theme) const {
    DrawPageHeader(canvas,theme,viewport_.left,viewport_.right,Tr("nav.plugins"));
    canvas.Text(Tr("plugins.notice"),canvas.smallFormat,D2D1::RectF(viewport_.left,95,refresh_.left-12,145),theme.secondaryText);
    DrawTextButton(canvas,theme,refresh_,Tr("plugins.refresh"),hovered_,pressed_);
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
    }
    canvas.target.PopAxisAlignedClip();DrawScrollbar(canvas,theme,{Bar(),1});
}
void PluginsPage::Down(float x,float y) {
    pressed_=HitNavigationButton(refresh_,x,y);
    if(const auto bar=Bar();bar&&HitNavigationButton(bar->thumb,x,y)){grab_=y-bar->thumb.top;target_=scroll_;}
}
bool PluginsPage::Up(float x,float y) {const bool refresh=pressed_&&HitNavigationButton(refresh_,x,y);pressed_=false;grab_.reset();return refresh;}
bool PluginsPage::Move(float x,float y) {
    if(grab_)if(const auto bar=Bar()){target_=scroll_=bar->OffsetFromThumbTop(y-*grab_);return true;}
    const bool hover=HitNavigationButton(refresh_,x,y);if(hover==hovered_)return false;hovered_=hover;return true;
}
bool PluginsPage::Wheel(int delta,float x,float y) {
    if(!HitNavigationButton(viewport_,x,y)||grab_)return false;
    target_=std::clamp(target_-static_cast<float>(delta)/WHEEL_DELTA*66,0.0F,(std::max)(0.0F,content_-(viewport_.bottom-viewport_.top)));return Animating();
}
void PluginsPage::Tick(float elapsed) {
    scroll_+=(target_-scroll_)*(1-std::exp(-16*(std::max)(0.0F,elapsed)));if(std::abs(scroll_-target_)<.5F)scroll_=target_;
}
}
