#include "ui/HideoutPage.h"
#include "ui/DurationFormat.h"
#include "ui/NavigationButton.h"
#include "common/DebugLog.h"
#include <array>
#include <sstream>
#include <unordered_set>

namespace noven::ui {
namespace {
constexpr float stripTop=190, stripBottom=310, top=326, materialHeight=88, craftItemHeight=64;
constexpr std::array<TabBarItem<data::GameMode>,3> tabs{{
    {data::GameMode::Pvp,L"PvP"},{data::GameMode::Pve,L"PvE"},{data::GameMode::Seasonal,L"PVPS"}}};
std::wstring Wide(std::string_view s) {
    if(s.empty()) return {};
    const int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);
    std::wstring result(n,L'\0');
    if(n) MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),result.data(),n);
    return result;
}
std::string Utf8(std::wstring_view s) {
    if(s.empty()) return {};
    const int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);
    std::string result(n,'\0');
    if(n) WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),result.data(),n,nullptr,nullptr);
    return result;
}
std::wstring Money(std::int64_t n) {
    auto s=std::to_wstring(n);
    for(int i=static_cast<int>(s.size())-3;i>0;i-=3) s.insert(i,L",");
    return L"₽"+s;
}
std::wstring Quantity(double n) { std::wostringstream out; out<<n; return out.str(); }
std::wstring Format(std::string_view key,std::wstring name,std::wstring value) {
    return UiLocalization().Format(key,{{name,value}});
}
std::wstring LevelName(std::string_view name,std::int64_t level,bool trader=false) {
    return UiLocalization().Format(trader?TextKey::HideoutTraderLevel:TextKey::HideoutLevel,
        {{L"name",Wide(name)},{L"level",std::to_wstring(level)}});
}
float CraftHeight(const data::HideoutCraftView& craft) {
    return 106+craftItemHeight*static_cast<float>(craft.materials.size())+(craft.source->restrictions.empty()?0:42);
}
float MaterialTop(const data::HideoutRow& row) {
    return row.filteredDetails?(row.materials.empty()?90.0F:128.0F):182.0F;
}
}
void HideoutPage::Initialize(const std::filesystem::path& directory,const data::ItemCatalog& items,const data::ItemEconomyStore& economy) {
    std::wstring error;
    if(!catalog_.Load(directory,error)) common::DebugLog(error);
    browser_=std::make_unique<data::HideoutBrowser>(catalog_,items,economy); Refresh();
}
void HideoutPage::LayoutStrip() {
    strip_.Layout(D2D1::RectF(left_+26,stripTop,(std::max)(left_+27,right_-26),stripBottom),stations_.size());
}
void HideoutPage::BeginTransition() {
    const auto pose=SampleTabTransition(detailProgress_);
    outgoingBar_=SampleScrollbarTransition(outgoingBar_,Bar(),detailProgress_).bar;
    outgoingRailBar_=SampleScrollbarTransition(outgoingRailBar_,RailBar(),detailProgress_).bar;
    // 只保留一份旧视图，连续切换不中断当前淡出，也不重复查询数据。
    // Keep one outgoing view; rapid selection continues its fade without repeated data queries.
    if(detailProgress_>=.42F || !outgoingRow_) {
        if(!rows_.empty()) outgoingRow_=rows_[selected_]; else outgoingRow_.reset();
        outgoingCrafts_=crafts_;outgoingScroll_=scroll_;outgoingLevels_.clear();
        for(auto index:levels_) outgoingLevels_.push_back(rows_[index].source->level);
        detailFromOpacity_=detailProgress_>=1?1:pose.incomingOpacity;
    } else detailFromOpacity_*=pose.outgoingOpacity;
    detailProgress_=0;
}
void HideoutPage::Select(std::size_t index) {
    if(index>=rows_.size()) return;
    const bool stationChanged=station_!=rows_[index].source->stationId;
    selected_=index; const auto& row=rows_[index];
    station_=row.source->stationId; selectedLevel_=row.source->id;
    viewedLevels_[station_]=row.source->level;
    levels_.clear();
    for(std::size_t i=0;i<rows_.size();++i) if(rows_[i].source->stationId==station_) levels_.push_back(i);
    std::sort(levels_.begin(),levels_.end(),[&](auto a,auto b){return rows_[a].source->level<rows_[b].source->level;});
    crafts_=browser_->Crafts(*row.source,locale_,Utf8(search_.Text()));
    const auto levelIt=std::find(levels_.begin(),levels_.end(),selected_);
    levelAnimation_.Select(static_cast<std::size_t>(levelIt-levels_.begin()),levels_.size(),stationChanged);
    for(std::size_t i=0;i<stations_.size();++i) if(rows_[stations_[i]].source->stationId==station_)
        stationAnimation_.Select(i,stations_.size());
}
void HideoutPage::Refresh() {
    if(!browser_) return;
    const auto previousStation=station_;
    locale_=UiLocalization().ActiveLocale(); const auto matches=browser_->Query(Utf8(search_.Text()),mode_,locale_);
    auto all=matches;
    updated_=browser_->LastUpdated(mode_); stations_.clear();rows_.clear();
    std::unordered_set<std::string> seen;
    // 搜索只保留相关等级与明细，避免选中不含搜索产物的旧等级。
    // Keep matching levels/details so an old level cannot hide the searched product.
    for(const auto& match:matches) if(seen.insert(match.source->stationId).second) {
        stations_.push_back(rows_.size());
        for(auto& row:all) if(row.source->stationId==match.source->stationId) rows_.push_back(std::move(row));
    }
    LayoutStrip();
    if(rows_.empty()) { stationAnimation_.Select(0,0);levelAnimation_.Select(0,0);selectedLevel_.clear(); levels_.clear(); crafts_.clear(); scroll_=target_=0; return; }
    auto found=std::find_if(rows_.begin(),rows_.end(),[&](const auto& r){return r.source->stationId==station_;});
    if(found==rows_.end()) found=rows_.begin();
    const auto id=found->source->stationId;
    const auto preferred=viewedLevels_.find(id);
    if(preferred!=viewedLevels_.end()) {
        const auto saved=std::find_if(rows_.begin(),rows_.end(),[&](const auto& r){return r.source->stationId==id && r.source->level==preferred->second;});
        if(saved!=rows_.end()) found=saved;
    }
    Select(static_cast<std::size_t>(found-rows_.begin()));
    // 数据刷新不抢占用户的横向浏览位置；仅选择发生变化时自动定位。
    // Data refresh preserves manual rail browsing; reveal only when selection changes.
    if(station_!=previousStation && right_>left_+52) for(std::size_t i=0;i<stations_.size();++i)
        if(rows_[stations_[i]].source->stationId==station_) strip_.Reveal(i);
    scroll_=(std::min)(scroll_,MaxScroll()); target_=(std::min)(target_,MaxScroll());
}
void HideoutPage::Prepare(float width,float height,const UiTheme& theme) {
    height_=height; left_=theme.sidebarWidth+theme.contentPadding; right_=width-theme.contentPadding; LayoutStrip();
    if(browser_ && (locale_!=UiLocalization().ActiveLocale() || updated_!=browser_->LastUpdated(mode_))) Refresh();
    scroll_=(std::min)(scroll_,MaxScroll()); target_=(std::min)(target_,MaxScroll());
}
TabBarLayout HideoutPage::Tabs() const { return {left_,128,171,100,22}; }
float HideoutPage::CraftTop() const {
    if(rows_.empty()) return 0;
    const auto& row=rows_[selected_];
    float y=MaterialTop(row)+materialHeight*static_cast<float>(row.materials.size());
    for(const auto* section:{&row.stations,&row.skills,&row.traders}) if(!section->empty()) y+=38+28*static_cast<float>(section->size());
    return y+(row.filteredDetails?0:80);
}
float HideoutPage::DetailHeight() const {
    if(!rows_.empty() && rows_[selected_].filteredDetails && crafts_.empty()) return CraftTop()+20;
    float y=CraftTop()+76;
    if(crafts_.empty()) return y+50;
    for(const auto& craft:crafts_) y+=CraftHeight(craft);
    return y+20;
}
float HideoutPage::MaxScroll() const { return rows_.empty()?0:(std::max)(0.0F,DetailHeight()-(height_-22-top)); }
std::optional<ScrollbarGeometry> HideoutPage::Bar() const {
    if(rows_.empty()) return {};
    return MakeScrollbar(D2D1::RectF(right_+12,top,right_+24,(std::max)(top,height_-22)),DetailHeight(),scroll_);
}
std::optional<ScrollbarGeometry> HideoutPage::RailBar() const {
    // 将横轴映射到通用滚条的纵轴，复用相同的弹性/渐隐规则。
    // Map horizontal coordinates onto the generic vertical axis to reuse morph/fade rules.
    const float width=(std::max)(1.0F,right_-left_-52);
    auto bar=MakeScrollbar(D2D1::RectF(0,left_+26,4,left_+26+width),width+strip_.Maximum(),strip_.Offset());
    if(bar) { const auto thumb=strip_.Thumb();bar->thumb.top=thumb.left;bar->thumb.bottom=thumb.right; }
    return bar;
}
std::optional<std::size_t> HideoutPage::LevelAt(float x,float y) const {
    if(y<top || y>=height_-22 || y<top-scroll_+44 || y>=top-scroll_+76 || x<left_+20 || x>=right_-20) return {};
    const float slot=(std::min)(90.0F,(right_-left_-40)/(std::max)(1.0F,static_cast<float>(levels_.size())));
    const auto i=static_cast<std::size_t>((x-left_-20)/slot);
    return i<levels_.size()?std::optional<std::size_t>(levels_[i]):std::nullopt;
}
D2D1_RECT_F HideoutPage::ArrowBounds(int direction) const {
    const float center=(stripTop+stripBottom-12)*.5F;
    const float x=direction<0?left_+12:right_-12;
    return D2D1::RectF(x-12,center-22,x+12,center+22);
}
std::optional<int> HideoutPage::ArrowAt(float x,float y) const {
    for(const int direction:{-1,1})
        if(HitNavigationButton(ArrowBounds(direction),x,y,strip_.CanMove(direction))) return direction;
    return {};
}
void HideoutPage::MouseDown(float x,float y) {
    pressedItem_=ItemAt(x,y);
    pressedStation_.reset(); pressedLevel_.reset(); pressedTab_.reset(); pressedArrow_.reset(); grab_.reset();
    if(search_.HitTest(D2D1::RectF(left_,84,right_,122),x,y)) search_.Focus(); else search_.Blur();
    if(strip_.Press(x,y)) return;
    if(const auto bar=Bar();bar && x>=bar->track.left && x<bar->track.right && y>=top && y<height_-22) {
        grab_=y>=bar->thumb.top && y<bar->thumb.bottom?y-bar->thumb.top:(bar->thumb.bottom-bar->thumb.top)/2; MouseMove(x,y); return;
    }
    pressedArrow_=ArrowAt(x,y);
    pressedStation_=strip_.Hit(x,y); pressedLevel_=LevelAt(x,y); pressedTab_=HitTestTabBar(tabs,Tabs(),x,y);
}
std::optional<std::string> HideoutPage::MouseUp(float x,float y) {
    const auto item=pressedItem_; pressedItem_.reset();
    if(strip_.Release()) return {};
    if(grab_) { MouseMove(x,y); grab_.reset(); return {}; }
    if(item && item==ItemAt(x,y)) return item;
    if(pressedArrow_ && pressedArrow_==ArrowAt(x,y)) strip_.Move(*pressedArrow_*272.0F);
    const auto tab=HitTestTabBar(tabs,Tabs(),x,y);
    if(pressedTab_ && pressedTab_==tab && mode_!=*tab) {
        BeginTransition();outgoing_=mode_;mode_=*tab;underlineFrom_=underline_;tabProgress_=0;Refresh();
    }
    if(pressedStation_ && pressedStation_==strip_.Hit(x,y) && rows_[stations_[*pressedStation_]].source->stationId!=station_) {
        BeginTransition();
        const auto index=stations_[*pressedStation_]; station_=rows_[index].source->stationId;
        levelAnimation_.Select(0,0);
        Refresh();scroll_=target_=0;strip_.Reveal(*pressedStation_);
    }
    if(pressedLevel_ && pressedLevel_==LevelAt(x,y) && *pressedLevel_!=selected_) { BeginTransition();Select(*pressedLevel_);scroll_=target_=0; }
    pressedStation_.reset();pressedLevel_.reset();pressedTab_.reset();pressedArrow_.reset();
    return {};
}
std::optional<std::string> HideoutPage::ItemAt(float x,float y) const {
    // 只命中稳定的当前视图，过渡期间不把旧材料映射到新物品。
    // Hit only settled current content, never map outgoing materials to incoming items.
    if(rows_.empty() || detailProgress_<1 || x<left_+20 || x>=right_-20 || y<top || y>=height_-22) return {};
    const auto& row=rows_[selected_];
    float cursor=top-scroll_+MaterialTop(row);
    for(const auto& material:row.materials) {
        if(y>=cursor && y<cursor+70) return material.id;
        cursor+=materialHeight;
    }
    cursor=top-scroll_+CraftTop()+76;
    for(const auto& craft:crafts_) {
        if(y>=cursor && y<cursor+64) return craft.source->itemId;
        float input=cursor+86+(craft.source->restrictions.empty()?0:42);
        for(const auto& material:craft.materials) {
            if(y>=input && y<input+44) return material.id;
            input+=craftItemHeight;
        }
        cursor+=CraftHeight(craft);
    }
    return {};
}
void HideoutPage::MouseMove(float x,float y) {
    strip_.Drag(x);hoveredTab_=HitTestTabBar(tabs,Tabs(),x,y);
    hoveredArrow_=ArrowAt(x,y);
    if(grab_) if(const auto bar=Bar()) scroll_=target_=bar->OffsetFromThumbTop(y-*grab_);
}
bool HideoutPage::Wheel(int delta,float x,float y) {
    if(y>=stripTop && y<stripBottom && x>=left_ && x<right_) strip_.Move(-delta/static_cast<float>(WHEEL_DELTA)*136);
    else target_=std::clamp(target_-delta/static_cast<float>(WHEEL_DELTA)*66,0.0F,MaxScroll());
    return Animating();
}
void HideoutPage::Edited() { BeginTransition();scroll_=target_=0;Refresh(); }
bool HideoutPage::Key(WPARAM key,bool control) {
    const auto before=search_.Text();if(!search_.HandleKeyDown(key,control)) return false;
    if(before!=search_.Text()) Edited();return true;
}
bool HideoutPage::Char(wchar_t c) { if(!search_.HandleChar(c)) return false;Edited();return true; }
bool HideoutPage::Animating() const { return detailProgress_<1 || stationAnimation_.Active() || levelAnimation_.Active() || strip_.Animating() || tabProgress_<1 || std::abs(scroll_-target_)>=0.1F; }
bool HideoutPage::Tick(float seconds) {
    seconds=std::clamp(seconds,0.0F,0.05F);strip_.Tick(seconds);
    stationAnimation_.Tick(seconds);levelAnimation_.Tick(seconds);
    detailProgress_=(std::min)(1.0F,detailProgress_+seconds/.36F);
    if(detailProgress_>=1) { outgoingRow_.reset();outgoingCrafts_.clear();outgoingLevels_.clear(); }
    tabProgress_=(std::min)(1.0F,tabProgress_+seconds/0.36F);
    underline_=underlineFrom_+(static_cast<float>(mode_)-underlineFrom_)*SampleTabTransition(tabProgress_).underlineProgress;
    target_=(std::min)(target_,MaxScroll());scroll_+=(target_-scroll_)*(1-std::exp(-24*seconds));
    if(std::abs(scroll_-target_)<0.1F) scroll_=target_;return Animating();
}
std::vector<std::string> HideoutPage::VisibleImages() const {
    std::vector<std::string> ids;
    const auto add=[&](const std::string& id) { if(!id.empty() && ids.size()<32 && std::find(ids.begin(),ids.end(),id)==ids.end()) ids.push_back(id); };
    // 设施条和详情共享最多 32 个可见图片请求；渲染不触发下载。
    // The station rail and detail share at most 32 visible image requests; drawing never downloads.
    for(std::size_t i=0;i<stations_.size();++i) {
        const auto rect=strip_.Card(i);if(rect.right<left_ || rect.left>right_) continue;
        const auto* level=rows_[stations_[i]].source;add(catalog_.Station(level->mode,level->stationId)->imageKey);
    }
    if(rows_.empty() && !(detailProgress_<.42F && outgoingRow_)) return ids;
    const auto visible=[&](float y,float h) { return y+h>=top-64 && y<height_+64; };
    const bool old=detailProgress_<.42F && outgoingRow_.has_value();
    const auto& row=old?*outgoingRow_:rows_[selected_];
    const float base=top-(old?outgoingScroll_:scroll_);
    float y=base+MaterialTop(row);
    for(const auto& m:row.materials) { if(visible(y,materialHeight)) add(m.id);y+=materialHeight; }
    for(const auto* section:{&row.stations,&row.skills,&row.traders}) if(!section->empty()) y+=38+28*static_cast<float>(section->size());
    y+=row.filteredDetails?76:156;
    for(const auto& c:old?outgoingCrafts_:crafts_) {
        if(visible(y,64)) add(c.source->itemId);
        float my=y+86+(c.source->restrictions.empty()?0:42);
        for(const auto& m:c.materials) { if(visible(my,craftItemHeight)) add(m.id);my+=craftItemHeight; }
        y+=CraftHeight(c);
    }
    return ids;
}
void HideoutPage::Draw(const UiCanvas& canvas,const UiTheme& theme,
    const std::unordered_map<std::string,Microsoft::WRL::ComPtr<ID2D1Bitmap>>& images) const {
    DrawPageHeader(canvas,theme,left_,right_,Tr(TextKey::NavHideout));
    search_.Draw(canvas,theme,D2D1::RectF(left_,84,right_,122),Tr(TextKey::HideoutSearch),true);
    DrawTabBar(canvas,theme,canvas.body,tabs,Tabs(),mode_,hoveredTab_,outgoing_,tabProgress_,underline_);
    if(mode_==data::GameMode::Seasonal) canvas.Text(Tr(TextKey::HideoutSeasonal),canvas.smallFormat,D2D1::RectF(left_,171,right_,190),theme.secondaryText);
    const auto picture=[&](const std::string& id,D2D1_RECT_F box) {
        canvas.Round(box,6,theme.background);const auto image=images.find(id);
        if(image!=images.end()) canvas.target.DrawBitmap(image->second.Get(),FitImage(image->second->GetSize(),box),canvas.brush.GetOpacity());
        else canvas.Text(Tr(TextKey::ImageUnavailable),canvas.smallFormat,box,theme.secondaryText);
    };
    canvas.target.PushAxisAlignedClip(D2D1::RectF(left_+26,stripTop,right_-26,stripBottom),D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    for(std::size_t i=0;i<stations_.size();++i) {
        const auto rect=strip_.Card(i);if(rect.right<left_+26 || rect.left>right_-26) continue;
        const auto& row=rows_[stations_[i]];
        const float weight=stationAnimation_.Weight(i);
        const auto blend=[&](D2D1_COLOR_F to) { const auto from=theme.secondaryText;
            return D2D1::ColorF(from.r+(to.r-from.r)*weight,from.g+(to.g-from.g)*weight,from.b+(to.b-from.b)*weight,1); };
        canvas.Round(rect,theme.cornerRadius,theme.surface);
        const auto* station=catalog_.Station(row.source->mode,row.source->stationId);
        picture(station->imageKey,D2D1::RectF(rect.left+30,rect.top+7,rect.right-30,rect.top+61));
        const auto alignment=canvas.smallFormat.GetTextAlignment();canvas.smallFormat.SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        canvas.Text(Wide(row.name),canvas.smallFormat,D2D1::RectF(rect.left+5,rect.top+63,rect.right-5,rect.top+88),blend(theme.primaryText));
        auto level=row.source->level;const auto saved=viewedLevels_.find(row.source->stationId);
        if(saved!=viewedLevels_.end()) level=saved->second;
        canvas.Text(Format(TextKey::HideoutViewedLevel,L"level",std::to_wstring(level)),canvas.smallFormat,
            D2D1::RectF(rect.left+5,rect.top+86,rect.right-5,rect.bottom-3),blend(theme.accent));
        canvas.smallFormat.SetTextAlignment(alignment);

    }
    if(!stations_.empty()) {
        const float x=strip_.Card(0).left+stationAnimation_.Position()*HorizontalCardStrip::pitch;
        canvas.Fill(D2D1::RectF(x+24,stripBottom-14,x+HorizontalCardStrip::cardWidth-24,stripBottom-12),theme.accent);
    }
    const auto railPose=SampleScrollbarTransition(outgoingRailBar_,RailBar(),detailProgress_);
    if(railPose.bar) {
        auto color=theme.secondaryText;color.a=.45F*railPose.opacity;
        canvas.Round(D2D1::RectF(railPose.bar->thumb.top,stripBottom-5,railPose.bar->thumb.bottom,stripBottom-2),1.5F,color);
    }
    canvas.target.PopAxisAlignedClip();
    if(strip_.Maximum()>0) {
        // 矢量箭头不依赖字体基线；按钮与命中区域一致，到边界时弱化并禁用。
        // Vector chevrons avoid font baselines; matching hit bounds disable at scroll limits.
        for(const int direction:{-1,1}) {
            const bool enabled=strip_.CanMove(direction);
            const bool hover=enabled && hoveredArrow_==direction;
            const bool pressed=hover && pressedArrow_==direction;
            DrawNavigationButton(canvas,theme,ArrowBounds(direction),
                direction<0?NavigationGlyph::Left:NavigationGlyph::Right,enabled,hover,pressed);
        }
    }
    const auto viewport=D2D1::RectF(left_,top,right_,(std::max)(top,height_-22));
    canvas.target.PushAxisAlignedClip(viewport,D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    canvas.Round(viewport,theme.cornerRadius,theme.surface);
    {
    const auto pose=SampleTabTransition(detailProgress_);
    const bool old=detailProgress_<.42F && outgoingRow_.has_value();
    const float opacity=old?detailFromOpacity_*pose.outgoingOpacity:pose.incomingOpacity;
    if(rows_.empty() && !old) {
        DrawEmptyState(canvas,theme,D2D1::RectF(left_,top,right_,top+160),right_-20,
            Tr(browser_ && browser_->Ready()?TextKey::HideoutNoResults:TextKey::HideoutUnavailable),
            Tr(browser_ && browser_->Ready()?TextKey::HideoutNoResultsHint:TextKey::HideoutUnavailableHint));
    } else {
        const auto text=[&](std::wstring_view value,float x,float y,IDWriteTextFormat& font,D2D1_COLOR_F color,float h=28) {
            if(y+h>=top && y<height_-22) canvas.Text(value,font,D2D1::RectF(x,y,right_-20,y+h),color);
        };
        const float base=top-(old?outgoingScroll_:scroll_);const auto& row=old?*outgoingRow_:rows_[selected_];
        const auto& displayedCrafts=old?outgoingCrafts_:crafts_;
        const auto& headerRow=rows_.empty()?row:rows_[selected_];
        const float headerBase=top-scroll_;
        text(LevelName(headerRow.name,headerRow.source->level),left_+20,headerBase+15,canvas.label,theme.primaryText);
        std::vector<std::int64_t> currentLevels;
        for(auto index:levels_) currentLevels.push_back(rows_[index].source->level);
        const auto& displayedLevels=rows_.empty()?outgoingLevels_:currentLevels;
        const bool animateLevels=!rows_.empty();
        const float slot=(std::min)(90.0F,(right_-left_-40)/(std::max)(1.0F,static_cast<float>(displayedLevels.size())));
        float lineIndex=levelAnimation_.Position();
        // 标题和等级导航不参与内容淡入淡出；文字与底线共用槽位中心。
        // Keep heading/level navigation outside the content fade; label and underline share slot centers.
        const auto alignment=canvas.smallFormat.GetTextAlignment();
        canvas.smallFormat.SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        for(std::size_t i=0;i<displayedLevels.size();++i) {
            const float x=left_+20+i*slot;const bool selected=displayedLevels[i]==row.source->level;
            const float weight=animateLevels?levelAnimation_.Weight(i):(selected?1.0F:0.0F);
            if(!animateLevels && selected) lineIndex=static_cast<float>(i);
            const auto from=theme.secondaryText,to=theme.primaryText;
            const auto color=D2D1::ColorF(from.r+(to.r-from.r)*weight,from.g+(to.g-from.g)*weight,from.b+(to.b-from.b)*weight,1);
            canvas.Text(Format(TextKey::HideoutViewedLevel,L"level",std::to_wstring(displayedLevels[i])),canvas.smallFormat,
                D2D1::RectF(x,headerBase+46,x+slot,headerBase+74),color);
        }
        canvas.smallFormat.SetTextAlignment(alignment);
        if(!displayedLevels.empty()) {
            const float center=left_+20+(lineIndex+.5F)*slot;
            const float halfWidth=(std::min)(22.0F,slot*.3F);
            canvas.Fill(D2D1::RectF(center-halfWidth,headerBase+76,center+halfWidth,headerBase+78),theme.accent);
        }
        ScopedContentTransition fade(canvas,D2D1::Point2F(0,0),opacity,1);
        D2D1_MATRIX_3X2_F transform;canvas.target.GetTransform(&transform);
        canvas.target.SetTransform(D2D1::Matrix3x2F::Translation(0,old?-6*(1-pose.outgoingOpacity):6*(1-pose.incomingOpacity))*transform);
        const auto cost=Format(row.completeEstimate?TextKey::HideoutEstimated:TextKey::HideoutKnown,L"price",Money(row.knownSubtotal));
        if(!row.filteredDetails) {
        text(Format(TextKey::HideoutTime,L"time",FormatDuration(row.source->seconds)),left_+20,base+90,canvas.smallFormat,theme.secondaryText);
        text(cost,left_+20,base+117,canvas.smallFormat,theme.primaryText);
        }
        if(!row.filteredDetails || !row.materials.empty())
            text(Tr(TextKey::HideoutMaterials),left_+20,base+MaterialTop(row)-31,canvas.label,theme.primaryText);
        float y=base+MaterialTop(row);
        for(const auto& m:row.materials) {
            if(y+materialHeight>=top && y<height_-22) {
                picture(m.id,D2D1::RectF(left_+20,y+4,left_+82,y+70));
                text(Wide(m.name)+L" ×"+std::to_wstring(m.count),left_+98,y+4,canvas.body,theme.primaryText);
                const auto price=m.subtotal?Format(TextKey::HideoutUnit,L"price",Money(*m.unitPrice))+L" · "+Format(TextKey::HideoutSubtotal,L"price",Money(*m.subtotal)):
                    Tr(m.status==data::FleaStatus::Banned?TextKey::HideoutBanned:m.status==data::FleaStatus::LockedOrUnavailable?TextKey::HideoutLocked:TextKey::Unknown);
                text(price,left_+98,y+35,canvas.smallFormat,theme.secondaryText);
            }
            y+=materialHeight;
        }
        const auto section=[&](const auto& entries,std::string_view key,bool trader=false) {
            if(entries.empty()) return;text(Tr(key),left_+20,y,canvas.label,theme.primaryText);y+=38;
            for(const auto& e:entries) { text(LevelName(e.name,e.level,trader),left_+20,y,canvas.smallFormat,theme.secondaryText);y+=28; }
        };
        section(row.stations,TextKey::HideoutStations);section(row.skills,TextKey::HideoutSkills);section(row.traders,TextKey::HideoutTraders,true);
        if(!row.filteredDetails) {
        text(cost,left_+20,y,canvas.body,theme.primaryText);y+=30;
        if(!row.completeEstimate) text(Format(TextKey::HideoutUnknown,L"count",std::to_wstring(row.unknownRequirementCount)),left_+20,y,canvas.smallFormat,theme.secondaryText);
        y+=50;
        }
        if(!row.filteredDetails || !displayedCrafts.empty()) {
        text(Tr(TextKey::HideoutCrafts),left_+20,y,canvas.label,theme.primaryText);
        text(Tr(TextKey::HideoutCraftHint),left_+20,y+31,canvas.smallFormat,theme.secondaryText,40);y+=76;
        if(displayedCrafts.empty()) text(Tr(TextKey::HideoutCraftEmpty),left_+20,y,canvas.body,theme.secondaryText);
        }
        for(const auto& craft:displayedCrafts) {
            const float h=CraftHeight(craft);
            if(y+h>=top && y<height_-22) {
                canvas.Round(D2D1::RectF(left_+12,y,right_-12,y+h-12),6,theme.background);
                picture(craft.source->itemId,D2D1::RectF(left_+22,y+8,left_+76,y+62));
                text(Wide(craft.name)+L" ×"+std::to_wstring(craft.source->count),left_+90,y+6,canvas.body,theme.primaryText);
                text(Format(TextKey::HideoutCraftTime,L"time",FormatDuration(craft.source->seconds))+L" · "+
                    Format(TextKey::HideoutViewedLevel,L"level",std::to_wstring(craft.source->level)),left_+90,y+36,canvas.smallFormat,theme.secondaryText);
                float my=y+86;
                if(!craft.source->restrictions.empty()) { text(Tr(TextKey::HideoutCraftRestricted),left_+22,y+72,canvas.smallFormat,theme.accent,40);my+=42; }
                for(const auto& m:craft.materials) {
                    if(my+craftItemHeight>=top && my<height_-22) picture(m.id,D2D1::RectF(left_+24,my,left_+67,my+44));
                    text(Wide(m.name)+L" ×"+Quantity(m.count),left_+82,my,canvas.smallFormat,theme.primaryText);
                    if(m.tool || m.functional) text(Tr(m.tool?TextKey::HideoutTool:TextKey::HideoutFunctional),left_+82,my+25,canvas.smallFormat,theme.secondaryText);
                    my+=craftItemHeight;
                }
            }
            y+=h;
        }
    }
    }
    canvas.target.PopAxisAlignedClip();DrawScrollbar(canvas,theme,SampleScrollbarTransition(outgoingBar_,Bar(),detailProgress_));
}
}
