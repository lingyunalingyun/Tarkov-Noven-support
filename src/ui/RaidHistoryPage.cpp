#include "ui/RaidHistoryPage.h"
#include "ui/Dropdown.h"
#include "data/LocalizedName.h"
#include <chrono>
#include <cstdio>

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
std::wstring Time(std::optional<std::int64_t> ms) {
    if(!ms) return Tr(TextKey::Unknown);
    using namespace std::chrono;
    const sys_time<milliseconds> wall{milliseconds{*ms}};
    const auto dateDay=floor<days>(wall); const year_month_day date{dateDay}; const hh_mm_ss clock{wall-dateDay};
    wchar_t text[40]{}; swprintf_s(text,L"%04d-%02u-%02u %02d:%02d:%02d",int(date.year()),unsigned(date.month()),unsigned(date.day()),static_cast<int>(clock.hours().count()),static_cast<int>(clock.minutes().count()),static_cast<int>(clock.seconds().count()));
    return text;
}
std::wstring Duration(std::optional<std::int64_t> ms) {
    if(!ms) return Tr(TextKey::Unknown);
    return std::to_wstring(*ms/60000)+Tr(TextKey::DurationMinute)+L" "+std::to_wstring(*ms/1000%60)+Tr(TextKey::DurationSecond);
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
bool RaidHistoryPage::Select(std::string id) {
    selected_=std::move(id); detailScroll_=0; compactDetail_=true; RefreshScans(); return browser_.Find(selected_)!=nullptr;
}
void RaidHistoryPage::RefreshScans() {linked_=raid::ScansForRaid(selected_,scans_);}
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
    listRect_=D2D1::RectF(left,top,compact_?right:left+(right-left)*0.40F-8,bottom);
    detailRect_=D2D1::RectF(compact_?left:listRect_.right+16,top,right,bottom);
    if(locale_!=UiLocalization().ActiveLocale()) {
        locale_=UiLocalization().ActiveLocale();std::map<std::string,std::string> names;
        for(const auto& map:maps_)names[map.id]=data::LocalizedName(map.nameZh,map.nameEn,locale_);
        browser_.SetMapNames(std::move(names));
    }
    ApplyFilter();
    for(bool detail:{false,true}) { auto& scroll=detail?detailScroll_:listScroll_; const auto rect=detail?detailRect_:listRect_;
        scroll=std::clamp(scroll,0.0F,(std::max)(0.0F,ContentHeight(detail)-(rect.bottom-rect.top))); }
}
float RaidHistoryPage::ContentHeight(bool detail) const {return detail?scansTop+scanHeight*static_cast<float>(linked_.entries.size())+60:rowHeight*static_cast<float>(browser_.Rows().size());}
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
void RaidHistoryPage::Choose(int control,std::size_t option) {
    auto f=browser_.Filter();
    if(control==0)f.mode=option?std::optional(option==5?raid::GameMode::Unknown:static_cast<raid::GameMode>(option)):std::nullopt;
    if(control==1)f.type=option?std::optional(option==3?raid::RaidType::Unknown:static_cast<raid::RaidType>(option)):std::nullopt;
    if(control==2)f.mapId=option?std::optional(browser_.Maps()[option-1]):std::nullopt;
    if(control==3)f.date=static_cast<raid::HistoryDate>(option);
    browser_.SetFilter(std::move(f));listScroll_=0;menu_.reset();
}
void RaidHistoryPage::Draw(const UiCanvas& canvas,const UiTheme& theme,const std::unordered_map<std::string,Microsoft::WRL::ComPtr<ID2D1Bitmap>>& images) const {
    const auto left=searchRect_.left,right=searchRect_.right;
    DrawPageHeader(canvas,theme,left,right,Tr(TextKey::NavRaidHistory));search_.Draw(canvas,theme,searchRect_,Tr(TextKey::RaidSearch),true);
    const std::array labels{TextKey::RaidMode,TextKey::RaidType,TextKey::RaidMap,TextKey::RaidDate};
    for(int i=0;i<4;++i) {const auto options=Options(i);DrawDropdownHeader(canvas,theme,controls_[i],Tr(labels[i])+L" · "+options[OptionIndex(i)],menu_==i,false);}
    const auto& summary=browser_.Summary(); const auto count=std::to_wstring(summary.count),pmc=std::to_wstring(summary.pmc),scav=std::to_wstring(summary.scav),unknown=std::to_wstring(summary.unknown),avg=Duration(summary.averageDuration);
    canvas.Text(UiLocalization().Format(TextKey::RaidSummary,{{L"count",count},{L"pmc",pmc},{L"scav",scav},{L"unknown",unknown},{L"duration",avg}}),canvas.smallFormat,D2D1::RectF(left,listRect_.top-66,right,listRect_.top-40),theme.secondaryText);
    std::wstring current;
    if(active_) {const auto name=browser_.MapName(*active_);current=Tr(TextKey::RaidCurrent)+L" · "+(name.empty()?Tr(TextKey::Unknown):Wide(name))+L" · "+Mode(active_->gameMode)+L" · "+Type(active_->raidType)+L" · "+Time(active_->startedAt);}
    else if(unavailable_)current=Tr(TextKey::RaidUnavailable);
    if(compact_&&compactDetail_)current=Tr(TextKey::RaidList);
    canvas.Text(current,canvas.smallFormat,D2D1::RectF(left,listRect_.top-38,right,listRect_.top-10),theme.accent);
    if(!compact_||!compactDetail_) {
        canvas.target.PushAxisAlignedClip(listRect_,D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        if(browser_.Rows().empty()) DrawEmptyState(canvas,theme,D2D1::RectF(listRect_.left,listRect_.top,listRect_.right,listRect_.top+180),listRect_.right-18,
            Tr(unavailable_?TextKey::RaidUnavailable:browser_.Sessions().empty()?TextKey::RaidEmpty:TextKey::RaidNoResults),Tr(unavailable_?TextKey::RaidUnavailableHint:TextKey::RaidEmptyHint));
        const auto first=static_cast<std::size_t>(listScroll_/rowHeight);
        for(std::size_t i=first;i<browser_.Rows().size();++i) {
            const float y=listRect_.top+static_cast<float>(i)*rowHeight-listScroll_;if(y>=listRect_.bottom)break;
            const auto& s=browser_.Sessions()[browser_.Rows()[i]];const auto name=browser_.MapName(s);const float x=listRect_.left+12,r=listRect_.right-22;
            canvas.Round(D2D1::RectF(listRect_.left,y,listRect_.right-16,y+rowHeight-8),theme.cornerRadius,s.localSessionId==selected_?theme.selected:theme.surface);
            canvas.Text(name.empty()?Tr(TextKey::Unknown):Wide(name),canvas.label,D2D1::RectF(x,y+5,r,y+30),theme.primaryText);
            canvas.Text(Mode(s.gameMode)+L" · "+Type(s.raidType),canvas.smallFormat,D2D1::RectF(x,y+30,r,y+52),theme.accent);
            canvas.Text(Time(s.startedAt)+L" · "+Duration(s.duration),canvas.smallFormat,D2D1::RectF(x,y+54,r,y+88),theme.secondaryText);
        }
        canvas.target.PopAxisAlignedClip();DrawScrollbar(canvas,theme,{Bar(false),1});
    }
    if(!compact_||compactDetail_) {
        canvas.target.PushAxisAlignedClip(detailRect_,D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        const auto* selected=browser_.Find(selected_);
        if(!selected)DrawEmptyState(canvas,theme,D2D1::RectF(detailRect_.left,detailRect_.top,detailRect_.right,detailRect_.top+160),detailRect_.right-18,Tr(selected_.empty()?TextKey::RaidSelect:TextKey::RaidMissing),L"");
        else {
            const float x=detailRect_.left+14,r=detailRect_.right-22,y=detailRect_.top-detailScroll_;const auto name=browser_.MapName(*selected);
            canvas.Round(D2D1::RectF(detailRect_.left,y,detailRect_.right-16,y+360),theme.cornerRadius,theme.surface);
            canvas.Text(name.empty()?Tr(TextKey::Unknown):Wide(name),canvas.label,D2D1::RectF(x,y+8,r,y+34),theme.primaryText);
            canvas.Text(Mode(selected->gameMode)+L" · "+Type(selected->raidType),canvas.body,D2D1::RectF(x,y+38,r,y+64),theme.accent);
            const std::array fields{Tr(TextKey::RaidStarted)+L" · "+Time(selected->startedAt),Tr(TextKey::RaidEnded)+L" · "+Time(selected->endedAt),Tr(TextKey::RaidDuration)+L" · "+Duration(selected->duration),Tr(TextKey::RaidOutcome)+L" · "+Outcome(selected->outcome),Tr(TextKey::RaidScans)+L" · "+std::to_wstring(linked_.entries.size())};
            for(std::size_t i=0;i<fields.size();++i)canvas.Text(fields[i],canvas.smallFormat,D2D1::RectF(x,y+70+static_cast<float>(i)*24,r,y+94+static_cast<float>(i)*24),theme.primaryText);
            const auto flea=Price(linked_.flea.value),trader=Price(linked_.trader.value),fk=std::to_wstring(linked_.flea.known),fu=std::to_wstring(linked_.flea.unknown),tk=std::to_wstring(linked_.trader.known),tu=std::to_wstring(linked_.trader.unknown);
            canvas.Text(UiLocalization().Format(TextKey::RaidSubtotal,{{L"flea",flea},{L"trader",trader},{L"fk",fk},{L"fu",fu},{L"tk",tk},{L"tu",tu}}),canvas.smallFormat,D2D1::RectF(x,y+194,r,y+268),theme.accent);
            canvas.Text(Tr(TextKey::RaidRetained),canvas.smallFormat,D2D1::RectF(x,y+272,r,y+330),theme.secondaryText);
            canvas.Text(Wide(selected->eftRaidId),canvas.smallFormat,D2D1::RectF(x,y+332,r,y+358),theme.secondaryText);
            if(linked_.entries.empty())canvas.Text(Tr(TextKey::RaidNoScans),canvas.body,D2D1::RectF(x,y+scansTop,r,y+scansTop+34),theme.secondaryText);
            for(std::size_t i=0;i<linked_.entries.size();++i) {
                const float top=y+scansTop+static_cast<float>(i)*scanHeight;if(top+scanHeight<detailRect_.top)continue;if(top>=detailRect_.bottom)break;
                const auto& scan=linked_.entries[i];const auto image=images.find(scan.stableItemId);
                DrawItemCard(canvas,theme,D2D1::RectF(detailRect_.left,top,detailRect_.right-16,top+scanHeight-8),{Wide(scan.canonicalName),Wide(scan.stableItemId),UiLocalization().Format(TextKey::FleaSale,{{L"price",Price(scan.fleaPrice)}}),UiLocalization().Format(TextKey::TraderSale,{{L"price",Price(scan.bestTraderPrice)}})+(scan.bestTraderName.empty()?L"":L" · "+Wide(scan.bestTraderName)),image==images.end()?nullptr:image->second.Get()},true);
            }
        }
        canvas.target.PopAxisAlignedClip();if(selected)DrawScrollbar(canvas,theme,{Bar(true),1});
    }
    if(menu_) {const auto options=Options(*menu_);DropdownLayout layout{controls_[*menu_]};DrawDropdownPanel(canvas,theme,layout,options.size());
        for(std::size_t i=0;i<options.size();++i)DrawDropdownOption(canvas,theme,layout.Option(i),options[i],OptionIndex(*menu_)==i,true,false);}
}
void RaidHistoryPage::MouseDown(float x,float y) {
    pressed_=D2D1::Point2F(x,y);if(search_.HitTest(searchRect_,x,y))search_.Focus();else search_.Blur();if(menu_)return;
    for(bool detail:{false,true}) {if(compact_&&detail!=compactDetail_)continue;const auto bar=Bar(detail);
        if(bar&&Hit(bar->track,x,y)) {grab_=std::pair(detail,Hit(bar->thumb,x,y)?y-bar->thumb.top:(bar->thumb.bottom-bar->thumb.top)/2);MouseMove(x,y);pressed_.reset();return;}}
}
std::optional<data::RecentScanEntry> RaidHistoryPage::MouseUp(float x,float y) {
    grab_.reset();if(!pressed_)return {};const auto down=*pressed_;pressed_.reset();
    if(menu_) {const int control=*menu_;const auto options=Options(control);DropdownLayout layout{controls_[control]};
        for(std::size_t i=0;i<options.size();++i)if(Hit(layout.Option(i),x,y)&&Hit(layout.Option(i),down.x,down.y)) {Choose(control,i);return {};}
        menu_.reset();return {};}
    for(int i=0;i<4;++i)if(Hit(controls_[i],x,y)&&Hit(controls_[i],down.x,down.y)) {menu_=i;return {};}
    if(compact_&&compactDetail_&&y>=listRect_.top-38&&y<listRect_.top-10) {compactDetail_=false;return {};}
    if((!compact_||!compactDetail_)&&Hit(listRect_,x,y)&&Hit(listRect_,down.x,down.y)) {
        const auto i=static_cast<std::size_t>((y-listRect_.top+listScroll_)/rowHeight);if(i<browser_.Rows().size())Select(browser_.Sessions()[browser_.Rows()[i]].localSessionId);
    } else if((!compact_||compactDetail_)&&Hit(detailRect_,x,y)&&Hit(detailRect_,down.x,down.y)) {
        const float position=y-detailRect_.top+detailScroll_-scansTop;if(position>=0) {const auto i=static_cast<std::size_t>(position/scanHeight);if(i<linked_.entries.size())return linked_.entries[i];}}
    return {};
}
bool RaidHistoryPage::MouseMove(float,float y) {if(!grab_)return false;const auto bar=Bar(grab_->first);if(!bar)return false;(grab_->first?detailScroll_:listScroll_)=bar->OffsetFromThumbTop(y-grab_->second);return true;}
bool RaidHistoryPage::Wheel(int delta,float x,float y) {
    if(menu_)return false;const bool detail=compact_?compactDetail_:Hit(detailRect_,x,y);const auto rect=detail?detailRect_:listRect_;if(!Hit(rect,x,y))return false;
    auto& scroll=detail?detailScroll_:listScroll_;scroll=std::clamp(scroll-delta/120.0F*80,0.0F,(std::max)(0.0F,ContentHeight(detail)-(rect.bottom-rect.top)));return true;
}
bool RaidHistoryPage::Key(WPARAM key,bool control) {const bool handled=search_.HandleKeyDown(key,control);if(handled){ApplyFilter();listScroll_=0;}return handled;}
bool RaidHistoryPage::Char(wchar_t value) {const bool handled=search_.HandleChar(value);if(handled){ApplyFilter();listScroll_=0;}return handled;}
std::vector<std::string> RaidHistoryPage::VisibleImages() const {
    std::vector<std::string> ids;if(compact_&&!compactDetail_)return ids;
    for(std::size_t i=0;i<linked_.entries.size();++i) {const float top=detailRect_.top-detailScroll_+scansTop+static_cast<float>(i)*scanHeight;
        if(top+scanHeight>=detailRect_.top&&top<detailRect_.bottom)ids.push_back(linked_.entries[i].stableItemId);}return ids;
}
std::optional<D2D1_RECT_F> RaidHistoryPage::ScanBounds(std::uint64_t scanId) const {
    if(compact_&&!compactDetail_)return {};
    for(std::size_t i=0;i<linked_.entries.size();++i)if(linked_.entries[i].scanId==scanId) {
        const float top=detailRect_.top-detailScroll_+scansTop+static_cast<float>(i)*scanHeight;
        if(top+scanHeight<=detailRect_.top||top>=detailRect_.bottom)return {};
        return D2D1::RectF(detailRect_.left,(std::max)(top,detailRect_.top),detailRect_.right-16,(std::min)(top+scanHeight-8,detailRect_.bottom));
    }
    return {};
}
}
