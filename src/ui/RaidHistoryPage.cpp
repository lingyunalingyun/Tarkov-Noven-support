#include "ui/RaidHistoryPage.h"
#include "ui/Dropdown.h"
#include "ui/RaidHistoryFormat.h"
#include "data/LocalizedName.h"
#include <chrono>
#include <cmath>

namespace noven::ui {
namespace {
std::wstring Wide(std::string_view text) {
    const int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0);
    std::wstring out(n,L'\0'); if(n) MultiByteToWideChar(CP_UTF8,0,text.data(),static_cast<int>(text.size()),out.data(),n); return out;
}
std::string Utf8(std::wstring_view text) {
    const int n=WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);
    std::string out(n,'\0'); if(n) WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),out.data(),n,nullptr,nullptr); return out;
}
std::wstring Mode(raid::GameMode mode) {
    switch(mode) {
    case raid::GameMode::PvP:return Tr(TextKey::RaidPvp);
    case raid::GameMode::PvE:return Tr(TextKey::RaidPve);
    case raid::GameMode::Practice:return Tr(TextKey::RaidPractice);
    case raid::GameMode::Offline:return Tr(TextKey::RaidOffline);
    default:return Tr(TextKey::Unknown);
    }
}
std::wstring Type(raid::RaidType type) { return Tr(type==raid::RaidType::PMC?TextKey::RaidPmc:type==raid::RaidType::Scav?TextKey::RaidScav:TextKey::Unknown); }
std::wstring Outcome(raid::RaidOutcome value) {
    switch(value) {
    case raid::RaidOutcome::Survived:return Tr(TextKey::RaidSurvived);
    case raid::RaidOutcome::RunThrough:return Tr(TextKey::RaidRunThrough);
    case raid::RaidOutcome::KIA:return Tr(TextKey::RaidKia);
    case raid::RaidOutcome::MIA:return Tr(TextKey::RaidMia);
    case raid::RaidOutcome::Left:return Tr(TextKey::RaidLeft);
    default:return Tr(TextKey::Unknown);
    }
}
std::wstring Price(std::optional<std::int64_t> value) {return value?L"₽"+std::to_wstring(*value):Tr(TextKey::Unknown);}
bool Hit(D2D1_RECT_F rect,float x,float y) {return HitTestDropdownRect(rect,x,y);}
constexpr float rowHeight=104, scansTop=374, scanHeight=154;
}
void RaidHistoryPage::SetSessions(std::vector<raid::RaidSession> sessions,std::optional<raid::RaidSession> active,bool unavailable) {
    browser_.SetSessions(std::move(sessions)); active_=std::move(active); unavailable_=unavailable; RefreshScans();
}
void RaidHistoryPage::SetScans(std::vector<data::RecentScanEntry> scans) {scans_=std::move(scans); RefreshScans();}
void RaidHistoryPage::SetMaps(const data::MapCatalog& maps) {maps_=maps.Maps(); locale_.clear();}
std::wstring RaidHistoryPage::ScanName(const data::RecentScanEntry& scan) const {
    // 译名可跟随界面语言，金额仍严格使用扫描时快照。
    // Names follow UI language; economy values remain strictly scan-time snapshots.
    if(items_)if(const auto* item=items_->FindById(scan.stableItemId))
        return Wide(data::LocalizedName(item->nameZh,item->nameEn,UiLocalization().ActiveLocale()));
    return Wide(scan.canonicalName);
}
bool RaidHistoryPage::Select(std::string id) {
    selected_=std::move(id); detailScroll_=0; RefreshScans();
    const auto& rows=browser_.Rows();
    const auto it=std::find_if(rows.begin(),rows.end(),[&](auto i){return browser_.Sessions()[i].localSessionId==selected_;});
    if(it==rows.end()) {expansion_={};focusTarget_.reset();return false;}
    detailProgress_=0;
    expansion_.Retarget(true,static_cast<std::size_t>(it-rows.begin()));
    focusTarget_=expansion_.ScrollTarget(rowHeight);
    return true;
}
bool RaidHistoryPage::Animating() const noexcept {
    return focusTarget_.has_value() || expansion_.expansionProgress<1 || detailProgress_<1 || (menu_ && (menuClosing_?menuProgress_>0:menuProgress_<1));
}
void RaidHistoryPage::Tick(float elapsed) {
    elapsed=std::clamp(elapsed,0.0F,0.05F);
    expansion_.Advance(elapsed,ContentHeight(true));
    // 只平滑视图偏移，不移动数据行；手动滚动/拖动会取消自动定位。
    // Smooth the viewport offset, never reorder records; manual scrolling/dragging cancels focus motion.
    if(focusTarget_) {
        const float maximum=(std::max)(0.0F,ContentHeight(false)-(listRect_.bottom-listRect_.top));
        const float target=std::clamp(*focusTarget_,0.0F,maximum),remaining=target-listScroll_;
        if(std::abs(remaining)<0.75F) {listScroll_=target;focusTarget_.reset();}
        else listScroll_+=remaining*(1-std::exp(-24*elapsed));
    }
    detailProgress_=(std::min)(1.0F,detailProgress_+elapsed/0.20F);
    if(menu_) {
        menuProgress_=AdvanceDropdownTransition(menuProgress_,menuClosing_,elapsed);
        if(menuClosing_&&menuProgress_<=0)menu_.reset();
    }
}
void RaidHistoryPage::RefreshScans() {
    linked_=raid::ScansForRaid(selected_,scans_);
    if(expansion_.open&&expansion_.expansionProgress>=1)expansion_.extent=ContentHeight(true);
}
// 同一个展开高度驱动绘制、命中和滚动；不创建第二个详情滚动区。
// One expansion extent drives drawing, hit testing and scrolling, without a second detail viewport.
float RaidHistoryPage::RowTop(std::size_t index) const {
    return static_cast<float>(index)*rowHeight+(index>expansion_.rowIndex?expansion_.extent:0);
}
std::optional<std::size_t> RaidHistoryPage::RowAt(float position) const {
    if(position<0)return {};
    const float after=(expansion_.rowIndex+1)*rowHeight;
    if(position>=after&&position<after+expansion_.extent)return {};
    if(position>=after+expansion_.extent)position-=expansion_.extent;
    const auto index=static_cast<std::size_t>(position/rowHeight);
    return index<browser_.Rows().size()?std::optional(index):std::nullopt;
}
float RaidHistoryPage::DetailTop() const {
    return listRect_.top+RowTop(expansion_.rowIndex)+rowHeight-listScroll_;
}
void RaidHistoryPage::ApplyFilter() {
    auto filter=browser_.Filter(); filter.search=Utf8(search_.Text());
    SYSTEMTIME local{};GetLocalTime(&local);
    using namespace std::chrono;
    filter.localMidnight=duration_cast<milliseconds>(sys_days{year{local.wYear}/month{local.wMonth}/day{local.wDay}}.time_since_epoch()).count();
    browser_.SetFilter(std::move(filter));
}
void RaidHistoryPage::Prepare(float width,float height,const UiTheme& theme) {
    const float left=theme.sidebarWidth+theme.contentPadding,right=width-theme.contentPadding;
    compact_=right-left<720;
    searchRect_=D2D1::RectF(left,84,right,122);
    const int columns=compact_?2:4;const float cell=(right-left-8*(columns-1))/columns;
    for(int i=0;i<4;++i) { const float x=left+(i%columns)*(cell+8),y=132+(i/columns)*40.0F;controls_[i]=D2D1::RectF(x,y,x+cell,y+32); }
    const float top=compact_?278.0F:238.0F,bottom=(std::max)(top+20,height-22);
    listRect_=D2D1::RectF(left,top,right,bottom);
    detailRect_=listRect_;
    if(locale_!=UiLocalization().ActiveLocale()) {
        locale_=UiLocalization().ActiveLocale();std::map<std::string,std::string> names;
        for(const auto& map:maps_)names[map.id]=data::LocalizedName(map.nameZh,map.nameEn,locale_);
        browser_.SetMapNames(std::move(names));
    }
    ApplyFilter();
    if(layoutBuilds_!=browser_.Builds()) {
        layoutBuilds_=browser_.Builds();
        focusTarget_.reset();
        const auto& rows=browser_.Rows();
        const auto it=std::find_if(rows.begin(),rows.end(),[&](auto i){return browser_.Sessions()[i].localSessionId==selected_;});
        if(it==rows.end())expansion_={};
        else expansion_.rowIndex=static_cast<std::size_t>(it-rows.begin());
    }
    for(bool detail:{false,true}) { auto& scroll=detail?detailScroll_:listScroll_; const auto rect=detail?detailRect_:listRect_;
        scroll=std::clamp(scroll,0.0F,(std::max)(0.0F,ContentHeight(detail)-(rect.bottom-rect.top))); }
}
float RaidHistoryPage::ContentHeight(bool detail) const {return detail?scansTop+scanHeight*static_cast<float>(linked_.entries.size())+60:rowHeight*static_cast<float>(browser_.Rows().size())+expansion_.ScrollExtra(browser_.Rows().size(),rowHeight,listRect_.bottom-listRect_.top,ContentHeight(true));}
std::optional<ScrollbarGeometry> RaidHistoryPage::Bar(bool detail) const {
    auto rect=detail?detailRect_:listRect_;rect.left=rect.right-14;return MakeScrollbar(rect,ContentHeight(detail),detail?detailScroll_:listScroll_);
}
std::vector<std::wstring> RaidHistoryPage::Options(int control) const {
    std::vector<std::wstring> result{Tr(TextKey::RaidAll)};
    if(control==0)for(auto mode:{raid::GameMode::PvP,raid::GameMode::PvE,raid::GameMode::Practice,raid::GameMode::Offline,raid::GameMode::Unknown})result.push_back(Mode(mode));
    if(control==1)for(auto type:{raid::RaidType::PMC,raid::RaidType::Scav,raid::RaidType::Unknown})result.push_back(Type(type));
    if(control==2)for(const auto& id:browser_.Maps()) {raid::RaidSession s;s.mapId=id; const auto name=browser_.MapName(s);result.push_back(name.empty()?Tr(TextKey::Unknown):Wide(name));}
    if(control==3)result={Tr(TextKey::RaidAllTime),Tr(TextKey::RaidToday),Tr(TextKey::RaidSeven),Tr(TextKey::RaidThirty)};
    return result;
}
std::size_t RaidHistoryPage::OptionIndex(int control) const {
    const auto& f=browser_.Filter();
    if(control==0)return f.mode?(*f.mode==raid::GameMode::Unknown?5:static_cast<std::size_t>(*f.mode)):0;
    if(control==1)return f.type?(*f.type==raid::RaidType::Unknown?3:static_cast<std::size_t>(*f.type)):0;
    if(control==2&&f.mapId) {const auto& maps=browser_.Maps();const auto it=std::find(maps.begin(),maps.end(),*f.mapId);return it==maps.end()?0:1+static_cast<std::size_t>(it-maps.begin());}
    return control==3?static_cast<std::size_t>(f.date):0;
}
std::size_t RaidHistoryPage::MenuRows(int control) const {
    const auto fit=static_cast<std::size_t>((std::max)(1.0F,std::floor((detailRect_.bottom-controls_[control].bottom-16)/32)));
    return (std::min)(Options(control).size(),fit);
}
void RaidHistoryPage::Choose(int control,std::size_t option) {
    auto f=browser_.Filter();
    if(control==0)f.mode=option?std::optional(option==5?raid::GameMode::Unknown:static_cast<raid::GameMode>(option)):std::nullopt;
    if(control==1)f.type=option?std::optional(option==3?raid::RaidType::Unknown:static_cast<raid::RaidType>(option)):std::nullopt;
    if(control==2)f.mapId=option?std::optional(browser_.Maps()[option-1]):std::nullopt;
    if(control==3)f.date=static_cast<raid::HistoryDate>(option);
    browser_.SetFilter(std::move(f));listScroll_=0;CloseMenu();
}
void RaidHistoryPage::Draw(const UiCanvas& canvas,const UiTheme& theme,const std::unordered_map<std::string,Microsoft::WRL::ComPtr<ID2D1Bitmap>>& images) const {
    const auto left=searchRect_.left,right=searchRect_.right;
    DrawPageHeader(canvas,theme,left,right,Tr(TextKey::NavRaidHistory));search_.Draw(canvas,theme,searchRect_,Tr(TextKey::RaidSearch),true);
    const std::array labels{TextKey::RaidMode,TextKey::RaidType,TextKey::RaidMap,TextKey::RaidDate};
    for(int i=0;i<4;++i) {const auto options=Options(i);DrawDropdownHeader(canvas,theme,controls_[i],Tr(labels[i])+L" · "+options[OptionIndex(i)],menu_==i&&!menuClosing_,hover_&&Hit(controls_[i],hover_->x,hover_->y));}
    const auto& summary=browser_.Summary(); const auto count=std::to_wstring(summary.count),pmc=std::to_wstring(summary.pmc),scav=std::to_wstring(summary.scav),unknown=std::to_wstring(summary.unknown),avg=RaidDurationText(summary.averageDuration);
    canvas.Text(UiLocalization().Format(TextKey::RaidSummary,{{L"count",count},{L"pmc",pmc},{L"scav",scav},{L"unknown",unknown},{L"duration",avg}}),canvas.smallFormat,D2D1::RectF(left,listRect_.top-66,right,listRect_.top-40),theme.secondaryText);
    std::wstring current;
    if(active_) {const auto name=browser_.MapName(*active_);current=Tr(TextKey::RaidCurrent)+L" · "+(name.empty()?Tr(TextKey::Unknown):Wide(name))+L" · "+Mode(active_->gameMode)+L" · "+Type(active_->raidType)+L" · "+RaidTimeText(active_->startedAt);}
    else if(unavailable_)current=Tr(TextKey::RaidUnavailable);
    canvas.Text(current,canvas.smallFormat,D2D1::RectF(left,listRect_.top-38,right,listRect_.top-10),theme.accent);
    {
        canvas.target.PushAxisAlignedClip(listRect_,D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        if(browser_.Rows().empty()) DrawEmptyState(canvas,theme,D2D1::RectF(listRect_.left,listRect_.top,listRect_.right,listRect_.top+180),listRect_.right-18,
            Tr(unavailable_?TextKey::RaidUnavailable:browser_.Sessions().empty()?TextKey::RaidEmpty:TextKey::RaidNoResults),Tr(unavailable_?TextKey::RaidUnavailableHint:TextKey::RaidEmptyHint));
        const float after=(expansion_.rowIndex+1)*rowHeight;
        const bool inside=listScroll_>=after&&listScroll_<after+expansion_.extent;
        const float adjusted=listScroll_-(listScroll_>=after+expansion_.extent?expansion_.extent:0);
        const auto first=inside?expansion_.rowIndex:static_cast<std::size_t>((std::max)(0.0F,adjusted)/rowHeight);
        for(std::size_t i=first;i<browser_.Rows().size();++i) {
            const float y=listRect_.top+RowTop(i)-listScroll_;if(y>=listRect_.bottom)break;
            const auto& s=browser_.Sessions()[browser_.Rows()[i]];const auto name=browser_.MapName(s);const float x=listRect_.left+12,r=listRect_.right-22;
            canvas.Round(D2D1::RectF(listRect_.left,y,listRect_.right-16,y+rowHeight-8),theme.cornerRadius,s.localSessionId==selected_&&expansion_.open?theme.selected:hover_&&Hit(D2D1::RectF(listRect_.left,y,listRect_.right-16,y+rowHeight-8),hover_->x,hover_->y)?theme.hover:theme.surface);
            if(s.localSessionId==selected_&&expansion_.open)canvas.Round(D2D1::RectF(listRect_.left,y+12,listRect_.left+3,y+rowHeight-20),1.5F,theme.accent);
            canvas.Text(name.empty()?Tr(TextKey::Unknown):Wide(name),canvas.label,D2D1::RectF(x,y+5,r,y+30),theme.primaryText);
            canvas.Text(Mode(s.gameMode)+L" · "+Type(s.raidType)+(s.outcome==raid::RaidOutcome::Unknown?L"":L" · "+Outcome(s.outcome)),canvas.smallFormat,D2D1::RectF(x,y+30,r,y+52),theme.accent);
            canvas.Text(RaidTimeText(s.startedAt)+L" · "+RaidDurationText(s.duration),canvas.smallFormat,D2D1::RectF(x,y+54,r,y+88),theme.secondaryText);
            if(i==expansion_.rowIndex&&expansion_.extent>0)DrawDetail(canvas,theme,images);
        }
        canvas.target.PopAxisAlignedClip();DrawScrollbar(canvas,theme,{Bar(false),1});
    }
    if(menu_) {
        const auto options=Options(*menu_);const auto rows=MenuRows(*menu_);DropdownLayout layout{controls_[*menu_]};
        const auto pose=SampleDropdownTransition(menuProgress_);
        const ScopedContentTransition transition(canvas,D2D1::Point2F((layout.header.left+layout.header.right)/2,layout.header.bottom),pose.opacity,pose.scale);
        DrawDropdownPanel(canvas,theme,layout,rows);
        const auto offset=(std::min)(menuOffset_,options.size()-rows);
        for(std::size_t i=0;i<rows;++i)DrawDropdownOption(canvas,theme,layout.Option(i),options[i+offset],OptionIndex(*menu_)==i+offset,true,hover_&&Hit(layout.Option(i),hover_->x,hover_->y));
        auto track=layout.Panel(rows);track.left=track.right-14;track.top+=8;track.bottom-=8;
        DrawScrollbar(canvas,theme,{MakeScrollbar(track,static_cast<float>(options.size())*32,static_cast<float>(offset)*32),1});
    }
}
void RaidHistoryPage::DrawDetail(const UiCanvas& canvas,const UiTheme& theme,
    const std::unordered_map<std::string,Microsoft::WRL::ComPtr<ID2D1Bitmap>>& images) const {
    canvas.target.PushAxisAlignedClip(D2D1::RectF(detailRect_.left,DetailTop(),detailRect_.right,DetailTop()+expansion_.extent),D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    const ScopedContentTransition transition(canvas,D2D1::Point2F(detailRect_.left,detailRect_.top),detailProgress_*detailProgress_*(3-2*detailProgress_),1);
    const auto* selected=browser_.Find(selected_);
    if(!selected)DrawEmptyState(canvas,theme,D2D1::RectF(detailRect_.left,detailRect_.top,detailRect_.right,detailRect_.top+160),detailRect_.right-18,Tr(selected_.empty()?TextKey::RaidSelect:TextKey::RaidMissing),L"");
    else {
        const float x=detailRect_.left+14,r=detailRect_.right-22,y=DetailTop();const auto name=browser_.MapName(*selected);
        canvas.Round(D2D1::RectF(detailRect_.left,y,detailRect_.right-16,y+360),theme.cornerRadius,theme.surface);
        canvas.Text(name.empty()?Tr(TextKey::Unknown):Wide(name),canvas.label,D2D1::RectF(x,y+8,r,y+34),theme.primaryText);
        canvas.Text(Mode(selected->gameMode)+L" · "+Type(selected->raidType),canvas.body,D2D1::RectF(x,y+38,r,y+64),theme.accent);
        const std::array fields{Tr(TextKey::RaidStarted)+L" · "+RaidTimeText(selected->startedAt),Tr(TextKey::RaidEnded)+L" · "+RaidTimeText(selected->endedAt),Tr(TextKey::RaidDuration)+L" · "+RaidDurationText(selected->duration),Tr(TextKey::RaidOutcome)+L" · "+Outcome(selected->outcome),Tr(TextKey::RaidScans)+L" · "+std::to_wstring(linked_.entries.size())};
        for(std::size_t i=0;i<fields.size();++i)canvas.Text(fields[i],canvas.smallFormat,D2D1::RectF(x,y+70+static_cast<float>(i)*24,r,y+94+static_cast<float>(i)*24),theme.primaryText);
        const auto flea=Price(linked_.flea.value),trader=Price(linked_.trader.value),fk=std::to_wstring(linked_.flea.known),fu=std::to_wstring(linked_.flea.unknown),tk=std::to_wstring(linked_.trader.known),tu=std::to_wstring(linked_.trader.unknown);
        canvas.Text(UiLocalization().Format(TextKey::RaidSubtotal,{{L"flea",flea},{L"trader",trader},{L"fk",fk},{L"fu",fu},{L"tk",tk},{L"tu",tu}}),canvas.smallFormat,D2D1::RectF(x,y+194,r,y+268),theme.accent);
        canvas.Text(Tr(TextKey::RaidRetained),canvas.smallFormat,D2D1::RectF(x,y+272,r,y+330),theme.secondaryText);
        canvas.Text(Wide(selected->eftRaidId),canvas.smallFormat,D2D1::RectF(x,y+332,r,y+358),theme.secondaryText);
        if(linked_.entries.empty())canvas.Text(Tr(TextKey::RaidNoScans),canvas.body,D2D1::RectF(x,y+scansTop,r,y+scansTop+34),theme.secondaryText);
        for(std::size_t i=0;i<linked_.entries.size();++i) {
            const float top=y+scansTop+static_cast<float>(i)*scanHeight;if(top+scanHeight<detailRect_.top)continue;if(top>=detailRect_.bottom)break;
            const auto& scan=linked_.entries[i];const auto image=images.find(scan.stableItemId);
            DrawItemCard(canvas,theme,D2D1::RectF(detailRect_.left,top,detailRect_.right-16,top+scanHeight-8),{ScanName(scan),Wide(scan.stableItemId),UiLocalization().Format(TextKey::FleaSale,{{L"price",Price(scan.fleaPrice)}}),UiLocalization().Format(TextKey::TraderSale,{{L"price",Price(scan.bestTraderPrice)}})+(scan.bestTraderName.empty()?L"":L" · "+Wide(scan.bestTraderName)),image==images.end()?nullptr:image->second.Get()},true);
        }
    }
    canvas.target.PopAxisAlignedClip();
}

void RaidHistoryPage::MouseDown(float x,float y) {
    pressed_=D2D1::Point2F(x,y);if(search_.HitTest(searchRect_,x,y))search_.Focus();else search_.Blur();if(menu_)return;
    for(bool detail:{false}) {const auto bar=Bar(detail);
        if(bar&&Hit(bar->track,x,y)) {focusTarget_.reset();grab_=std::pair(detail,Hit(bar->thumb,x,y)?y-bar->thumb.top:(bar->thumb.bottom-bar->thumb.top)/2);MouseMove(x,y);pressed_.reset();return;}}
}
std::optional<data::RecentScanEntry> RaidHistoryPage::MouseUp(float x,float y) {
    grab_.reset();if(!pressed_)return {};const auto down=*pressed_;pressed_.reset();
    if(menu_&&menuClosing_)menu_.reset();
    if(menu_) {const int control=*menu_;const auto options=Options(control);DropdownLayout layout{controls_[control]};
        const auto rows=MenuRows(control),offset=(std::min)(menuOffset_,options.size()-rows);
        for(std::size_t i=0;i<rows;++i)if(Hit(layout.Option(i),x,y)&&Hit(layout.Option(i),down.x,down.y)) {Choose(control,i+offset);return {};}
        CloseMenu();return {};}
    for(int i=0;i<4;++i)if(Hit(controls_[i],x,y)&&Hit(controls_[i],down.x,down.y)) {menu_=i;menuOffset_=0;menuProgress_=0;menuClosing_=false;return {};}
    if(Hit(listRect_,x,y)&&Hit(listRect_,down.x,down.y)) {
        const float position=y-listRect_.top+listScroll_;
        const auto row=RowAt(position),downRow=RowAt(down.y-listRect_.top+listScroll_);
        if(row&&downRow==row) {
            const auto& id=browser_.Sessions()[browser_.Rows()[*row]].localSessionId;
            if(id==selected_) {expansion_.Retarget(!expansion_.open,*row);focusTarget_=expansion_.open?std::optional(expansion_.ScrollTarget(rowHeight)):std::nullopt;}
            else Select(id);
        } else if(!row&&expansion_.open) {
            const float scanPosition=y-DetailTop()-scansTop;
            const float downPosition=down.y-DetailTop()-scansTop;
            if(scanPosition>=0&&downPosition>=0) {
                const auto i=static_cast<std::size_t>(scanPosition/scanHeight);
                if(i<linked_.entries.size()&&i==static_cast<std::size_t>(downPosition/scanHeight)) {
                    const auto bounds=ScanBounds(linked_.entries[i].scanId);
                    if(bounds&&Hit(*bounds,x,y)&&Hit(*bounds,down.x,down.y))return linked_.entries[i];
                }
            }
        }
    }
    return {};
}
bool RaidHistoryPage::MouseMove(float x,float y) {const bool changed=!hover_||hover_->x!=x||hover_->y!=y;hover_=D2D1::Point2F(x,y);if(!grab_)return changed;const auto bar=Bar(grab_->first);if(!bar)return false;(grab_->first?detailScroll_:listScroll_)=bar->OffsetFromThumbTop(y-grab_->second);return true;}
bool RaidHistoryPage::Wheel(int delta,float x,float y) {
    if(menu_) {
        const auto rows=MenuRows(*menu_);const auto options=Options(*menu_);const DropdownLayout layout{controls_[*menu_]};
        if(!Hit(layout.Panel(rows),x,y))return false;
        menuOffset_=static_cast<std::size_t>(std::clamp(static_cast<int>(menuOffset_)-delta/WHEEL_DELTA,0,static_cast<int>(options.size()-rows)));return true;
    }
    const bool detail=false;const auto rect=detail?detailRect_:listRect_;if(!Hit(rect,x,y))return false;
    focusTarget_.reset();
    auto& scroll=detail?detailScroll_:listScroll_;scroll=std::clamp(scroll-delta/120.0F*80,0.0F,(std::max)(0.0F,ContentHeight(detail)-(rect.bottom-rect.top)));return true;
}
bool RaidHistoryPage::Key(WPARAM key,bool control) {
    if(key==VK_ESCAPE&&menu_) {CloseMenu();return true;}
    const bool handled=search_.HandleKeyDown(key,control);if(handled){ApplyFilter();listScroll_=0;}return handled;
}
bool RaidHistoryPage::Char(wchar_t value) {const bool handled=search_.HandleChar(value);if(handled){ApplyFilter();listScroll_=0;}return handled;}
std::vector<std::string> RaidHistoryPage::VisibleImages() const {
    std::vector<std::string> ids;if(!expansion_.open||expansion_.extent<=0)return ids;
    for(std::size_t i=0;i<linked_.entries.size();++i) {const float top=DetailTop()+scansTop+static_cast<float>(i)*scanHeight;
        if(top+scanHeight>=detailRect_.top&&top<detailRect_.bottom&&top<DetailTop()+expansion_.extent)ids.push_back(linked_.entries[i].stableItemId);}return ids;
}
std::optional<D2D1_RECT_F> RaidHistoryPage::ScanBounds(std::uint64_t scanId) const {
    if(!expansion_.open||expansion_.extent<=0)return {};
    for(std::size_t i=0;i<linked_.entries.size();++i)if(linked_.entries[i].scanId==scanId) {
        const float top=DetailTop()+scansTop+static_cast<float>(i)*scanHeight;
        if(top+scanHeight<=detailRect_.top||top>=detailRect_.bottom||top>=DetailTop()+expansion_.extent)return {};
        return D2D1::RectF(detailRect_.left,(std::max)(top,detailRect_.top),detailRect_.right-16,(std::min)({top+scanHeight-8,detailRect_.bottom,DetailTop()+expansion_.extent}));
    }
    return {};
}
}
