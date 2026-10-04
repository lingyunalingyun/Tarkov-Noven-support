#include "ui/EventsPage.h"
#include "ui/EventFormat.h"
#include "data/LocalizedName.h"
#include "ui/NavigationButton.h"
#include <chrono>
#include <cmath>

namespace noven::ui {
namespace {
constexpr float rowHeight=196;
bool Hit(D2D1_RECT_F r,float x,float y){return HitNavigationButton(r,x,y);}
std::optional<events::EventStatus> FilterAt(int index) {
    switch(index){case 1:return events::EventStatus::Active;case 2:return events::EventStatus::Upcoming;
        case 3:return events::EventStatus::Ended;case 4:return events::EventStatus::Unknown;default:return {};}
}
int FilterIndex(std::optional<events::EventStatus> status) {
    if(!status)return 0;for(int i=1;i<5;++i)if(FilterAt(i)==status)return i;return 0;
}
events::Timestamp Now(){return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
}
void EventsPage::SetSnapshot(std::vector<events::EventRecord> records,events::EventRefreshState state,std::optional<events::Timestamp> refreshed) {
    const auto oldBuilds=browser_.Builds();browser_.SetEvents(std::move(records));browser_.SetNow(Now());
    detailDirty_|=refresh_.translationWarning!=state.translationWarning;
    refresh_=std::move(state);refreshed_=refreshed;
    if(browser_.Builds()!=oldBuilds)detailDirty_=true;
    if(!selected_.empty()&&!browser_.Find(selected_)){selected_.clear();narrowDetail_=false;detailDirty_=true;}
}
void EventsPage::SetCatalogs(const data::ItemCatalog* items,const data::TaskCatalog* tasks,const data::MapCatalog* maps) {
    items_=items;tasks_=tasks;maps_=maps;locale_.clear();detailDirty_=true;
}
void EventsPage::RebindEntities() {
    std::map<events::EntityKey,std::string> names;
    if(items_)for(const auto& item:items_->Items())names[{events::EntityKind::Item,item.id}]=data::LocalizedName(item.nameZh,item.nameEn,locale_);
    if(tasks_)for(const auto& task:tasks_->Tasks())names.try_emplace(events::EntityKey{events::EntityKind::Task,task.id},data::LocalizedName(task.nameZh,task.nameEn,locale_));
    if(maps_) {
        for(const auto& map:maps_->Maps())names[{events::EntityKind::Map,map.id}]=data::LocalizedName(map.nameZh,map.nameEn,locale_);
        for(const auto& point:maps_->Points())if(point.kind=="boss"&&!point.sourceId.empty())
            names.try_emplace(events::EntityKey{events::EntityKind::Boss,point.sourceId},data::LocalizedName(point.nameZh,point.nameEn,locale_));
    }
    browser_.SetEntities(std::move(names));
}
bool EventsPage::Select(std::string_view id) {
    if(!browser_.Find(id))return false;
    narrowDetail_=true;
    if(selected_==id)return true;
    selected_=id;detailScroll_=detailTarget_=0;detailDirty_=true;showOriginal_=false;
    // 快速切换从当前姿态继续，保持有限动画；选择不重排列表。
    // Rapid switching continues from the current pose with a finite transition; selection never reorders rows.
    if(detailOpacity_==1)detailOpacity_=0.75F;return true;
}
void EventsPage::SetFilter(std::optional<events::EventStatus> status,std::wstring query) {
    search_.SetText(std::move(query));browser_.SetFilter(status,EventUtf8(search_.Text()));
    filterAnimation_.Select(FilterIndex(status),5);listScroll_=listTarget_=0;
    if(narrow_)narrowDetail_=false;
}
void EventsPage::ApplyFilter() {
    const auto builds=browser_.Builds();browser_.SetFilter(browser_.Filter(),EventUtf8(search_.Text()));
    if(builds!=browser_.Builds()){listScroll_=listTarget_=0;if(narrow_)narrowDetail_=false;}
}
bool EventsPage::ClockTick(events::Timestamp now) {
    const auto before=browser_.Builds();browser_.SetNow(now);const bool changed=before!=browser_.Builds();detailDirty_|=changed;return changed;
}
void EventsPage::Prepare(float width,float height,const UiTheme& theme,IDWriteFactory* factory,IDWriteTextFormat* body,IDWriteTextFormat* label) {
    const float left=theme.sidebarWidth+theme.contentPadding,right=(std::max)(left+100,width-theme.contentPadding);
    narrow_=right-left<680;
    searchRect_={left,84,right,122};tabsRect_={left,132,right,170};
    refreshRect_={right-116,196,right,228};listButton_={left,248,left+150,280};
    const float bottom=(std::max)(310.0F,height-22),top=narrow_&&ShowingDetail()?292.0F:248.0F;
    if(narrow_){listRect_={left,top,right,bottom};detailRect_=listRect_;}
    else {const float split=left+std::clamp((right-left)*.32F,230.0F,320.0F);listRect_={left,top,split,bottom};detailRect_={split+16,top,right,bottom};}
    if(locale_!=UiLocalization().ActiveLocale()){locale_=UiLocalization().ActiveLocale();RebindEntities();detailDirty_=true;}
    const float detailWidth=detailRect_.right-detailRect_.left;
    if(detailWidth_!=detailWidth){detailWidth_=detailWidth;detailDirty_=true;}
    if(detailDirty_){BuildDetail(factory,body,label);detailDirty_=false;}
    ClampScroll();
}
void EventsPage::BuildDetail(IDWriteFactory* factory,IDWriteTextFormat* body,IDWriteTextFormat* label) {
    blocks_.clear();officialText_.clear();evidenceText_.clear();detailHeight_=16;
    const auto* event=browser_.Find(selected_);if(!event)return;
    const auto add=[&](std::wstring text,bool heading=false,bool official=false,std::optional<EventAction> action={},std::string image={},bool footnote=false) {
        Block block;block.text=std::move(text);block.heading=heading;block.official=official;block.action=std::move(action);block.imageId=std::move(image);
        block.footnote=footnote;
        block.top=detailHeight_;const float textWidth=(std::max)(20.0F,detailWidth_-56-(block.imageId.empty()?0:44));
        auto* format=heading?label:body;
        if(factory&&format&&SUCCEEDED(factory->CreateTextLayout(block.text.data(),static_cast<UINT32>(block.text.size()),format,textWidth,100000,&block.layout))) {
            block.layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);block.layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            block.layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            if(footnote)block.layout->SetFontSize(format->GetFontSize()*.8F,{0,static_cast<UINT32>(block.text.size())});
            DWRITE_TEXT_METRICS metrics{};block.layout->GetMetrics(&metrics);block.height=(std::max)(footnote?18.0F:heading?32.0F:26.0F,metrics.height+(footnote?8:12));
        }else block.height=heading?38.0F:54.0F;
        if(block.action)block.height=(std::max)(44.0F,block.height);
        if(official)officialText_.push_back(block.text);
        detailHeight_+=block.height+8;blocks_.push_back(std::move(block));
    };
    const bool official=!events::CommunitySourced(*event);
    // 正文优先；社区来源用页底小字标识，不进入官方事实快照。
    // Content comes first; a small footer credits community sources outside official fact snapshots.
    if(official)add(Tr(TextKey::EventOfficial),true);
    const auto content=[&](const std::string& text,bool heading=false,bool sourceFact=false){
        add(ContentText(text),heading);if(sourceFact)officialText_.push_back(EventWide(text));
    };
    content(event->title,true,official);
    if(locale_=="zh-CN"&&!event->machineText.empty()) {
        add(Tr(TextKey::EventMachineTranslation),false,false,{},{},true);
        add(Tr(showOriginal_?TextKey::EventShowTranslation:TextKey::EventShowOriginal),false,false,EventAction{EventAction::Kind::Original,{}});
    }
    if(event->titleIsExcerpt)add(Tr(TextKey::EventExcerpt),false,official);
    add(EventStatusText(browser_.Status(*event))+L" · "+Tr(TextKey::EventScope)+L": "+EventScopeText(*event),false,official);
    add(Tr(TextKey::EventAnnounced)+L": "+EventTimeText(event->announcedAt)+L"\n"+
        Tr(TextKey::EventStarts)+L": "+EventTimeText(event->startsAt)+L"\n"+Tr(TextKey::EventEnds)+L": "+EventTimeText(event->endsAt),false,official);
    content(event->summary,false,official);
    for(const auto& source:event->sourceEvidence)if(source.sourceKind==events::SourceKind::OfficialTelegram&&events::SafeEventSourceUrl(source.sourceUrl))
        add(Tr(TextKey::EventOpenSource)+L" · "+EventTimeText(source.publishedAt),false,true,EventAction{EventAction::Kind::Source,source.sourceUrl});
    bool communityHeading=!official;
    for(const auto& source:event->sourceEvidence)if(source.sourceKind==events::SourceKind::CommunityWiki) {
        if(!communityHeading){add(Tr(TextKey::EventCommunity),true);communityHeading=true;}
        if(official)content(source.summary);
        if(!official)add(Tr(TextKey::EventSourceUpdated)+L": "+EventTimeText(event->lastUpdatedAt));
        if(events::SafeEventSourceUrl(source.sourceUrl))add(Tr(TextKey::EventOpenSource)+L" · "+Tr(TextKey::EventWikiSource),false,false,EventAction{EventAction::Kind::Source,source.sourceUrl});
    }
    add(Tr(TextKey::EventRelated),true);add(Tr(TextKey::EventRelatedHint));
    const auto related=browser_.Associations(*event);
    if(related.empty())add(Tr(TextKey::EventNoRelated));
    for(const auto& entity:related) {
        const auto key=entity.kind==events::EntityKind::Map?TextKey::EventMaps:entity.kind==events::EntityKind::Task?TextKey::EventTasks:
            entity.kind==events::EntityKind::Item?TextKey::EventItems:TextKey::EventBosses;
        std::optional<EventAction> action;
        if(entity.kind!=events::EntityKind::Boss)action=EventAction{entity.kind==events::EntityKind::Map?EventAction::Kind::Map:
            entity.kind==events::EntityKind::Task?EventAction::Kind::Task:EventAction::Kind::Item,entity.id};
        add(Tr(key)+L" · "+EventWide(entity.name),false,false,action,entity.kind==events::EntityKind::Item?entity.id:std::string{});
    }
    if(const auto count=browser_.Unresolved(*event))add(UiLocalization().Format(TextKey::EventUnresolved,{{L"count",std::to_wstring(count)}}));
    add(Tr(TextKey::EventChanges),true);add(Tr(TextKey::EventChangesHint));
    std::string lastSource;bool any=false;
    for(const auto& evidence:event->sourceEvidence)if(evidence.sourceKind==events::SourceKind::TarkovChanges&&evidence.type==events::EvidenceType::ConfigurationChange) {
        any=true;
        const auto text=EventEvidencePreview(evidence.changedKey)+L"\n"+EventEvidencePreview(evidence.oldValue)+L" → "+EventEvidencePreview(evidence.newValue);
        evidenceText_.push_back(text);add(text);
        if(lastSource!=evidence.sourceRecordId) {
            lastSource=evidence.sourceRecordId;add(Tr(TextKey::EventEvidenceSource)+L" · "+EventTimeText(evidence.publishedAt));
            if(events::SafeEventSourceUrl(evidence.sourceUrl))add(Tr(TextKey::EventOpenSource),false,false,EventAction{EventAction::Kind::Source,evidence.sourceUrl});
        }
    }
    if(!any)add(Tr(TextKey::EventNoChanges));
    if(!refresh_.translationWarning.empty())add(Tr(TextKey::EventTranslationUnavailable),false,false,{},{},true);
    if(communityHeading)add(Tr(TextKey::EventCommunityHint),false,false,{},{},true);
    detailHeight_+=16;
}
void EventsPage::ClampScroll() {
    const float listMax=(std::max)(0.0F,rowHeight*static_cast<float>(browser_.Rows().size())-(listRect_.bottom-listRect_.top));
    const float detailMax=(std::max)(0.0F,detailHeight_-(detailRect_.bottom-detailRect_.top));
    listScroll_=std::clamp(listScroll_,0.0F,listMax);listTarget_=std::clamp(listTarget_,0.0F,listMax);
    detailScroll_=std::clamp(detailScroll_,0.0F,detailMax);detailTarget_=std::clamp(detailTarget_,0.0F,detailMax);
}
D2D1_RECT_F EventsPage::BlockRect(const Block& b) const {return {detailRect_.left+16,detailRect_.top+b.top-detailScroll_,detailRect_.right-24,detailRect_.top+b.top+b.height-detailScroll_};}
std::optional<ScrollbarGeometry> EventsPage::Bar(bool detail) const {
    auto rect=detail?detailRect_:listRect_;rect.left=rect.right-14;
    return MakeScrollbar(rect,detail?detailHeight_:rowHeight*static_cast<float>(browser_.Rows().size()),detail?detailScroll_:listScroll_);
}
std::wstring EventsPage::RefreshText() const {
    if(refresh_.phase==events::RefreshPhase::Refreshing)return Tr(TextKey::EventRefreshing);
    if(refresh_.phase==events::RefreshPhase::Failed)return Tr(browser_.Events().empty()?TextKey::EventUnavailable:TextKey::EventCached);
    if(!refresh_.sourceWarning.empty())return Tr(TextKey::EventPartialRefresh);
    return refreshed_?Tr(TextKey::EventUpdated):Tr(TextKey::EventEmpty);
}
std::wstring EventsPage::LastRefreshText() const {
    return refreshed_?UiLocalization().Format(TextKey::EventLastRefresh,{{L"time",EventTimeText(refreshed_)}}):std::wstring{};
}
std::wstring EventsPage::ContentText(std::string_view text) const {
    const auto* event=browser_.Find(selected_);
    if(!showOriginal_&&locale_=="zh-CN"&&event)if(const auto i=event->machineText.find(std::string(text));i!=event->machineText.end())return EventWide(i->second);
    return EventWide(text);
}
void EventsPage::Draw(const UiCanvas& canvas,const UiTheme& theme,const std::unordered_map<std::string,Microsoft::WRL::ComPtr<ID2D1Bitmap>>& images) const {
    DrawPageHeader(canvas,theme,searchRect_.left,searchRect_.right,Tr(TextKey::NavEvents));
    search_.Draw(canvas,theme,searchRect_,Tr(TextKey::EventSearch),true);
    // 标签文本拥有到本次绘制结束；不得把临时本地化字符串作为跨帧 view 保存。
    // Labels must live through this draw; never retain temporary localized string views across frames.
    const std::array<std::wstring,5> labels{Tr(TextKey::EventAll),Tr(TextKey::EventActive),Tr(TextKey::EventUpcoming),Tr(TextKey::EventEnded),Tr(TextKey::Unknown)};
    std::array<TabBarItem<int>,5> ownedTabs{};for(int i=0;i<5;++i)ownedTabs[i]={i,labels[i]};
    DrawTabBar(canvas,theme,canvas.body,ownedTabs,Tabs(),FilterIndex(browser_.Filter()),TabAt(hover_?hover_->x:-1,hover_?hover_->y:-1),FilterIndex(browser_.Filter()),1,filterAnimation_.Position(),filterAnimation_.Weights());
    const auto summary=UiLocalization().Format(TextKey::EventSummary,{{L"active",std::to_wstring(browser_.Count(events::EventStatus::Active))},
        {L"upcoming",std::to_wstring(browser_.Count(events::EventStatus::Upcoming))},{L"total",std::to_wstring(browser_.Events().size())}});
    canvas.Text(summary,canvas.smallFormat,{searchRect_.left,176,searchRect_.right,198},theme.secondaryText);
    canvas.Text(RefreshText(),canvas.smallFormat,{searchRect_.left,198,refreshRect_.left-8,218},theme.secondaryText);
    if(refreshed_)canvas.Text(LastRefreshText(),canvas.smallFormat,
        {searchRect_.left,220,searchRect_.right,242},theme.secondaryText);
    const bool refreshing=refresh_.phase==events::RefreshPhase::Refreshing;
    canvas.Round(refreshRect_,8,theme.surface);canvas.CenteredText(Tr(TextKey::EventRefresh),canvas.smallFormat,refreshRect_,refreshing?theme.secondaryText:theme.accent);
    if(narrow_&&ShowingDetail()){canvas.Round(listButton_,8,theme.surface);canvas.CenteredText(Tr(TextKey::EventList),canvas.body,listButton_,theme.accent);}
    if(!narrow_||!ShowingDetail()) {
        canvas.target.PushAxisAlignedClip(listRect_,D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        const auto& rows=browser_.Rows();
        if(rows.empty())canvas.Text(Tr(browser_.Events().empty()?(refresh_.phase==events::RefreshPhase::Failed?TextKey::EventUnavailable:TextKey::EventEmpty):TextKey::EventNoResults),canvas.body,{listRect_.left+12,listRect_.top+20,listRect_.right-18,listRect_.bottom},theme.secondaryText);
        for(std::size_t i=static_cast<std::size_t>(listScroll_/rowHeight);i<rows.size();++i) {
            const auto& e=browser_.Events()[rows[i]];const float top=listRect_.top+static_cast<float>(i)*rowHeight-listScroll_;if(top>=listRect_.bottom)break;
            const D2D1_RECT_F card{listRect_.left,top,listRect_.right-16,top+rowHeight-8};
            const bool selected=e.eventId==selected_,hovered=hover_&&Hit(card,hover_->x,hover_->y);
            canvas.Round(card,theme.cornerRadius,selected?theme.selected:hovered?theme.hover:theme.surface);
            if(selected||browser_.Status(e)==events::EventStatus::Active)canvas.Round({card.left,top+14,card.left+3,card.bottom-14},2,theme.accent);
            // 临时换行后恢复共享格式，长原文不会改变其他页面的排版契约。
            // Restore the shared format after wrapping long source titles; other pages keep their layout contract.
            const auto wrapping=canvas.label.GetWordWrapping();canvas.label.SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
            const auto translated=e.machineText.find(e.title);
            canvas.Text(EventWide(locale_=="zh-CN"&&translated!=e.machineText.end()?translated->second:e.title),canvas.label,{card.left+12,top+8,card.right-12,top+58},theme.primaryText);
            canvas.label.SetWordWrapping(wrapping);
            const D2D1_RECT_F badge{card.left+12,top+62,(std::min)(card.left+122,card.right-12),top+85};
            canvas.Round(badge,5,theme.background);
            canvas.CenteredText(EventStatusText(browser_.Status(e)),canvas.smallFormat,badge,
                browser_.Status(e)==events::EventStatus::Active?theme.accent:theme.secondaryText);
            const bool community=events::CommunitySourced(e);
            canvas.Text(Tr(community?TextKey::EventWikiSource:TextKey::EventOfficial)+L" · "+EventScopeText(e),canvas.smallFormat,{card.left+12,top+88,card.right-12,top+110},theme.secondaryText);
            canvas.Text(Tr(community?TextKey::EventSourceUpdated:TextKey::EventAnnounced)+L": "+EventTimeText(community?e.lastUpdatedAt:e.announcedAt),canvas.smallFormat,{card.left+12,top+112,card.right-12,top+136},theme.secondaryText);
            canvas.Text(Tr(TextKey::EventStarts)+L": "+EventTimeText(e.startsAt)+L"\n"+Tr(TextKey::EventEnds)+L": "+EventTimeText(e.endsAt),canvas.smallFormat,{card.left+12,top+138,card.right-12,top+183},theme.secondaryText);
        }
        canvas.target.PopAxisAlignedClip();DrawScrollbar(canvas,theme,{Bar(false),1});
    }
    if(ShowingDetail()) {
        canvas.Round(detailRect_,theme.cornerRadius,theme.surface);
        canvas.target.PushAxisAlignedClip(detailRect_,D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        if(selected_.empty())canvas.Text(Tr(TextKey::EventSelect),canvas.body,{detailRect_.left+20,detailRect_.top+24,detailRect_.right-20,detailRect_.bottom},theme.secondaryText);
        {
        const ScopedContentTransition transition(canvas,D2D1::Point2F(detailRect_.left,detailRect_.top),detailOpacity_,.985F+.015F*detailOpacity_);
        for(const auto& b:blocks_) {
            const auto rect=BlockRect(b);if(rect.bottom<=detailRect_.top||rect.top>=detailRect_.bottom)continue;
            if(b.action)canvas.Round(rect,6,hover_&&Hit(rect,hover_->x,hover_->y)?theme.hover:theme.selected);
            float x=rect.left+8;
            if(!b.imageId.empty()){if(const auto image=images.find(b.imageId);image!=images.end())canvas.target.DrawBitmap(image->second.Get(),FitImage(image->second->GetSize(),{x,rect.top+4,x+32,rect.top+36}),canvas.brush.GetOpacity());x+=44;}
            canvas.brush.SetColor(b.footnote?theme.secondaryText:b.heading||b.action?theme.accent:theme.primaryText);
            if(b.layout)canvas.target.DrawTextLayout({x,rect.top+4},b.layout.Get(),&canvas.brush,D2D1_DRAW_TEXT_OPTIONS_CLIP);
            else canvas.Text(b.text,b.footnote?canvas.smallFormat:b.heading?canvas.label:canvas.body,{x,rect.top,rect.right-8,rect.bottom},b.footnote?theme.secondaryText:theme.primaryText);
        }
        }
        canvas.target.PopAxisAlignedClip();DrawScrollbar(canvas,theme,{Bar(true),1});
    }
}
TabBarLayout EventsPage::Tabs() const {return {tabsRect_.left,tabsRect_.top,tabsRect_.bottom,(tabsRect_.right-tabsRect_.left)/5,18};}
std::optional<int> EventsPage::TabAt(float x,float y) const {
    const std::array<TabBarItem<int>,5> tabs{{{0,{}},{1,{}},{2,{}},{3,{}},{4,{}}}};
    return HitTestTabBar(tabs,Tabs(),x,y);
}
std::optional<std::size_t> EventsPage::RowAt(float x,float y) const {
    if((narrow_&&ShowingDetail())||!Hit(listRect_,x,y)||x>=listRect_.right-16)return {};
    const auto index=static_cast<std::size_t>((y-listRect_.top+listScroll_)/rowHeight);return index<browser_.Rows().size()?std::optional(index):std::nullopt;
}
std::optional<EventAction> EventsPage::ActionAt(float x,float y) const {
    if(Hit(refreshRect_,x,y)&&refresh_.phase!=events::RefreshPhase::Refreshing)return EventAction{EventAction::Kind::Refresh,{}};
    if(!ShowingDetail()||!Hit(detailRect_,x,y))return {};
    for(const auto& b:blocks_)if(b.action&&Hit(BlockRect(b),x,y))return b.action;return {};
}
void EventsPage::MouseDown(float x,float y) {
    pressed_=D2D1::Point2F(x,y);if(Hit(searchRect_,x,y))search_.Focus();else search_.Blur();
    for(bool detail:{false,true})if(!(narrow_&&(detail?!ShowingDetail():ShowingDetail())))if(const auto bar=Bar(detail);bar&&Hit(bar->track,x,y)) {
        auto& scroll=detail?detailScroll_:listScroll_;auto& target=detail?detailTarget_:listTarget_;
        if(Hit(bar->thumb,x,y))grab_=std::pair{detail,y-bar->thumb.top};
        else target=scroll=bar->OffsetFromThumbTop(y-(bar->thumb.bottom-bar->thumb.top)/2);
        pressed_.reset();break;
    }
}
std::optional<EventAction> EventsPage::MouseUp(float x,float y) {
    if(grab_){MouseMove(x,y);grab_.reset();return {};}
    const auto down=pressed_;pressed_.reset();if(!down)return {};
    if(const auto tab=TabAt(x,y);tab&&tab==TabAt(down->x,down->y)){SetFilter(FilterAt(*tab),search_.Text());return {};}
    if(narrow_&&ShowingDetail()&&Hit(listButton_,x,y)&&Hit(listButton_,down->x,down->y)){narrowDetail_=false;return {};}
    if(const auto row=RowAt(x,y);row&&row==RowAt(down->x,down->y)){Select(browser_.Events()[browser_.Rows()[*row]].eventId);return {};}
    const auto action=ActionAt(x,y);if(action!=ActionAt(down->x,down->y))return {};
    if(action&&action->kind==EventAction::Kind::Original){showOriginal_=!showOriginal_;detailDirty_=true;detailOpacity_=.75F;return {};}
    return action;
}
bool EventsPage::MouseMove(float x,float y) {
    if(grab_)if(const auto bar=Bar(grab_->first)){auto& scroll=grab_->first?detailScroll_:listScroll_;auto& target=grab_->first?detailTarget_:listTarget_;target=scroll=bar->OffsetFromThumbTop(y-grab_->second);}
    const bool changed=!hover_||hover_->x!=x||hover_->y!=y;hover_=D2D1::Point2F(x,y);return changed;
}
bool EventsPage::Wheel(int delta,float x,float y) {
    const bool detail=ShowingDetail()&&Hit(detailRect_,x,y);
    const bool list=(!narrow_||!ShowingDetail())&&Hit(listRect_,x,y);if(!detail&&!list)return false;
    auto& target=detail?detailTarget_:listTarget_;target-=static_cast<float>(delta)/WHEEL_DELTA*100;ClampScroll();return true;
}
bool EventsPage::Key(WPARAM key,bool control) {if(!search_.HandleKeyDown(key,control))return false;ApplyFilter();return true;}
bool EventsPage::Char(wchar_t value) {if(!search_.HandleChar(value))return false;ApplyFilter();return true;}
void EventsPage::Blur() {
    search_.Blur();CancelDrag();listTarget_=listScroll_;detailTarget_=detailScroll_;detailOpacity_=1;
    filterAnimation_.Select(FilterIndex(browser_.Filter()),5,true);
}
bool EventsPage::Animating() const noexcept {return std::abs(listScroll_-listTarget_)>.1F||std::abs(detailScroll_-detailTarget_)>.1F||detailOpacity_<1||filterAnimation_.Active();}
void EventsPage::Tick(float seconds) {
    seconds=std::clamp(seconds,0.0F,.05F);const float alpha=1-std::exp(-22*seconds);
    filterAnimation_.Tick(seconds);
    for(auto pair:{std::pair{&listScroll_,&listTarget_},std::pair{&detailScroll_,&detailTarget_}})
        if(std::abs(*pair.first-*pair.second)<.1F)*pair.first=*pair.second;else *pair.first+=(*pair.second-*pair.first)*alpha;
    detailOpacity_=(std::min)(1.0F,detailOpacity_+seconds/0.18F);
}
std::vector<std::string> EventsPage::VisibleImages() const {
    std::vector<std::string> ids;if(!ShowingDetail())return ids;
    for(const auto& b:blocks_)if(!b.imageId.empty()){const auto r=BlockRect(b);if(r.bottom>detailRect_.top-80&&r.top<detailRect_.bottom+80)ids.push_back(b.imageId);}return ids;
}
std::optional<D2D1_RECT_F> EventsPage::ActionBounds(EventAction::Kind kind,std::string_view id) const {
    for(const auto& b:blocks_)if(b.action&&b.action->kind==kind&&b.action->id==id)return BlockRect(b);return {};
}
std::optional<D2D1_RECT_F> EventsPage::SourceNoteBounds() const {
    return !blocks_.empty()&&blocks_.back().footnote?std::optional(BlockRect(blocks_.back())):std::nullopt;
}
float EventsPage::SourceNoteFontSize() const {
    float size{};
    if(!blocks_.empty()&&blocks_.back().footnote&&blocks_.back().layout)blocks_.back().layout->GetFontSize(0,&size);
    return size;
}
}
