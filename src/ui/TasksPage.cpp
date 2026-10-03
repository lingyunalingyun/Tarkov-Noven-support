#include "ui/TasksPage.h"

#include "ui/NavigationButton.h"
#include "ui/PageComponents.h"
#include "ui/OverflowText.h"
#include "data/LocalizedName.h"
#include "common/DebugLog.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace noven::ui {
namespace {
constexpr float kTraderTop = 136.0F;
constexpr float kTraderBottom = 256.0F;
constexpr float kCompactTaskTop = 264.0F;
constexpr float kCompactTaskBottom = 316.0F;
constexpr float kCompactDetailTop = 328.0F;
constexpr float kWideDetailTop = 274.0F;
constexpr float kTaskRailGap = 16.0F;
constexpr float kTaskPitch = 158.0F;
constexpr float kTaskWidth = 150.0F;
constexpr float kVerticalTaskPitch = 58.0F;
constexpr float kWideBreakpoint = 680.0F;
constexpr float kDetailColumnsBreakpoint = 760.0F;

std::string Utf8(std::wstring_view value){if(value.empty())return {};const int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);std::string result(n,'\0');if(n)WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),result.data(),n,nullptr,nullptr);return result;}

float ChainEntriesHeight(std::size_t count) {
    return 27.0F+(count==0?34.0F:38.0F*static_cast<float>(count));
}

float ObjectiveRowsHeight(const data::TaskRecord& task) {
    float height{};
    for (const auto& objective : task.objectives) height += objective.itemIds.empty() ? 62.0F : 78.0F;
    return height;
}

std::size_t RewardColumns(float width) {
    return width>=760.0F?4:width>=470.0F?2:1;
}

float RewardGroupHeight(std::size_t count,float width) {
    if(count==0)return 0.0F;
    const auto columns=RewardColumns(width);
    return 40.0F+70.0F*static_cast<float>((count+columns-1)/columns);
}

std::size_t MarkerRows(const data::TaskRecord& task) {
    const std::size_t count=static_cast<std::size_t>(task.kappaRequired)
        +static_cast<std::size_t>(task.lightkeeperRequired);
    return (std::max)(std::size_t{1},count);
}

enum class TaskMarker { Blocked, Firework, Check };

void DrawTaskMarker(const UiCanvas& canvas,D2D1_POINT_2F center,TaskMarker marker,
    D2D1_COLOR_F color) {
    canvas.brush.SetColor(color);
    if(marker==TaskMarker::Blocked){
        canvas.target.DrawEllipse(D2D1::Ellipse(center,7.0F,7.0F),&canvas.brush,1.7F);
        canvas.target.DrawLine(D2D1::Point2F(center.x-5.0F,center.y-5.0F),
            D2D1::Point2F(center.x+5.0F,center.y+5.0F),&canvas.brush,1.7F);
    } else if(marker==TaskMarker::Check){
        canvas.target.DrawLine(D2D1::Point2F(center.x-6.0F,center.y),
            D2D1::Point2F(center.x-1.5F,center.y+4.5F),&canvas.brush,2.0F);
        canvas.target.DrawLine(D2D1::Point2F(center.x-1.5F,center.y+4.5F),
            D2D1::Point2F(center.x+7.0F,center.y-5.0F),&canvas.brush,2.0F);
    } else {
        for(int i=0;i<8;++i){
            const float angle=static_cast<float>(i)*3.14159265F/4.0F;
            canvas.target.DrawLine(D2D1::Point2F(center.x+std::cos(angle)*3.5F,center.y+std::sin(angle)*3.5F),
                D2D1::Point2F(center.x+std::cos(angle)*7.5F,center.y+std::sin(angle)*7.5F),
                &canvas.brush,1.5F);
        }
        canvas.Circle(center,2.0F,color);
    }
}
}

void TasksPage::Initialize(const std::filesystem::path& directory,const data::ItemCatalog& items) {
    std::wstring error;if(!catalog_.Load(directory,error))common::DebugLog(error);
    browser_=std::make_unique<data::TaskBrowser>(catalog_,items);Refresh();
}

void TasksPage::SetMapLinks(const data::MapCatalog& catalog,data::GameMode mode) {
    CloseMapTargets();
    (mode==data::GameMode::Pve?pveMapLinks_:mapLinks_).Bind(catalog);
}

void TasksPage::SetMode(data::GameMode mode) {
    CloseMapTargets();
    CancelDrag();
    pressedTrader_.reset();pressedTask_.reset();pressedChain_.reset();pressedRewardItem_.reset();pressedArrow_.reset();
    hoveredMapPoint_.reset();
    if(mode_==mode)return;
    const auto previousTask=taskId_;
    mode_=mode;
    if(browser_)if(const auto* task=browser_->Task(mode_,taskId_))traderId_=task->traderId;
    Refresh();
    if(taskId_!=previousTask)scroll_=scrollTarget_=0.0F;
    scroll_=std::clamp(scroll_,0.0F,MaxScroll());
    scrollTarget_=std::clamp(scrollTarget_,0.0F,MaxScroll());
    taskScroll_=std::clamp(taskScroll_,0.0F,TaskMaximum());
    taskTarget_=std::clamp(taskTarget_,0.0F,TaskMaximum());
    detailProgress_=0.0F;textScrollTime_=0.0F;
}

void TasksPage::CloseMapTargets() {
    mapObjective_.reset();pressedTarget_.reset();hoveredTarget_.reset();
    mapTargetOffset_=0;mapClosePressed_=mapDismissPressed_=false;
}

std::optional<TasksPage::MapTargetList> TasksPage::MapTargetsLayout() const {
    if(!mapObjective_)return {};
    const auto targets=MapLinks().Targets(taskId_,*mapObjective_);
    const auto viewport=DetailBounds();
    // 列表几何只使用详情视口；显示与命中共享完整可见行，不借用正文滚动。
    // Drawing and hit testing share fully visible rows within the detail viewport, independent of body scroll.
    const float width=viewport.right-viewport.left-16,height=viewport.bottom-viewport.top-16;
    if(targets.size()<2||width<64||height<100)return {};
    const auto capacity=static_cast<std::size_t>((height-36)/64);
    const auto count=(std::min)(targets.size(),capacity);
    const auto offset=(std::min)(mapTargetOffset_,targets.size()-count);
    const float right=viewport.right-8,left=right-(std::min)(width,360.0F);
    const float top=viewport.top+8;
    MapTargetList result{{left,top,right,top+36+64*static_cast<float>(count)},
        {right-32,top+2,right-2,top+32},{}};
    for(std::size_t i=0;i<count;++i){
        const auto index=offset+i;const auto* target=MapLinks().Metadata(targets[index]);
        std::wstring source;
        if(target){
            std::wostringstream out;out<<Text(target->mapZh,target->mapEn)<<L" · X "<<target->position.x
                <<L", Y "<<target->position.y<<L", Z "<<target->position.z;source=out.str();
        }
        const float y=top+36+64*static_cast<float>(i);
        result.rows.push_back({targets[index],Tr(TextKey::TasksLocation)+L" "+std::to_wstring(index+1),
            std::move(source),{left+4,y,right-4,y+60}});
    }
    return result;
}

std::optional<std::string> TasksPage::MapTargetAt(float x,float y) const {
    if(const auto layout=MapTargetsLayout())for(const auto& row:layout->rows)
        if(x>=row.bounds.left&&x<row.bounds.right&&y>=row.bounds.top&&y<row.bounds.bottom)return row.id;
    return {};
}

std::wstring TasksPage::Text(const std::string& zh,const std::string& en) const {
    const auto& value=data::LocalizedName(zh,en,locale_);
    if(value.empty())return {};const int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0);
    std::wstring result(n,L'\0');if(n)MultiByteToWideChar(CP_UTF8,0,value.data(),static_cast<int>(value.size()),result.data(),n);return result;
}

const data::TaskTrader* TasksPage::Trader() const {
    const auto found=std::find_if(traders_.begin(),traders_.end(),[&](const auto* trader){return trader->id==traderId_;});
    return found==traders_.end()?nullptr:*found;
}

const data::TaskRecord* TasksPage::Task() const {
    const auto found=std::find_if(tasks_.begin(),tasks_.end(),[&](const auto* task){return task->id==taskId_;});
    return found==tasks_.end()?nullptr:*found;
}

std::vector<std::string> TasksPage::Unlocks() const {
    std::vector<std::string> result;const auto* selected=Task();if(!selected)return result;
    for(const auto& row:rows_)if(std::find(row.source->prerequisites.begin(),row.source->prerequisites.end(),selected->id)!=row.source->prerequisites.end())result.push_back(row.source->id);
    return result;
}

TasksPage::RewardGroups TasksPage::Rewards() const {
    RewardGroups result;const auto* task=Task();if(!task)return result;
    if(task->experience>0)result.direct.push_back({L"+"+std::to_wstring(task->experience)+L" XP",{}});
    for(const auto& reward:task->rewards){
        std::wostringstream out;
        if(reward.type=="item"||reward.type=="craftUnlock"||reward.type=="offerUnlock"){
            const auto* item=browser_->Item(reward.targetId);out<<(item?Text(item->nameZh,item->nameEn):std::wstring(reward.targetId.begin(),reward.targetId.end()))<<L" ×"<<reward.value;
        } else if(reward.type=="standing") {
            const auto* rewardTrader=browser_->Trader(mode_,reward.targetId);
            if(rewardTrader)out<<Text(rewardTrader->nameZh,rewardTrader->nameEn)<<L" ";
            if(reward.value>=0)out<<L"+";out<<reward.value;
        } else out<<Tr(TextKey::TasksUnlocks);
        const std::string itemId=(reward.type=="item"||reward.type=="craftUnlock"||reward.type=="offerUnlock")
            ?reward.targetId:std::string{};
        if(reward.type=="craftUnlock")result.crafts.push_back({out.str(),itemId});
        else if(reward.type=="offerUnlock")result.offers.push_back({out.str(),itemId});
        else result.direct.push_back({out.str(),itemId});
    }return result;
}

std::string_view TasksPage::SelectedTrader() const {
    return traderId_;
}

std::string_view TasksPage::SelectedTask() const {
    return taskId_;
}

D2D1_RECT_F TasksPage::SearchBounds() const {
    return D2D1::RectF(left_, 84.0F, right_, 122.0F);
}

D2D1_RECT_F TasksPage::TaskListBounds() const {
    if (narrow_) return D2D1::RectF(left_ + 12.0F, kCompactTaskTop,
        right_ - 12.0F, kCompactTaskBottom);
    const float railWidth=(right_-left_-kTaskRailGap)*0.21F;
    return D2D1::RectF(left_, kWideDetailTop, left_ + railWidth,
        (std::max)(kWideDetailTop, height_ - 22.0F));
}

D2D1_RECT_F TasksPage::DetailBounds() const {
    return narrow_
        ? D2D1::RectF(left_, kCompactDetailTop, right_, (std::max)(kCompactDetailTop, height_ - 22.0F))
        : D2D1::RectF(TaskListBounds().right + kTaskRailGap, kWideDetailTop,
            right_, (std::max)(kWideDetailTop, height_ - 22.0F));
}

void TasksPage::Prepare(float width, float height, const UiTheme& theme) {
    left_ = theme.sidebarWidth + theme.contentPadding;
    right_ = width - theme.contentPadding;
    height_ = height;
    const bool nextNarrow = right_ - left_ < kWideBreakpoint;
    const bool layoutChanged = nextNarrow != narrow_;
    narrow_ = nextNarrow;
    const float detailWidth = narrow_ ? right_ - left_
        : DetailBounds().right-DetailBounds().left;
    stackedDetails_ = narrow_ || detailWidth < kDetailColumnsBreakpoint;
    traderStrip_.Layout(D2D1::RectF(left_ + 26.0F, kTraderTop, (std::max)(left_ + 27.0F, right_ - 26.0F),
        kTraderBottom), traders_.size());
    if (locale_ != UiLocalization().ActiveLocale()) {
        locale_ = UiLocalization().ActiveLocale();
        Refresh();
    }
    if (layoutChanged) {
        taskScroll_ = taskTarget_ = 0.0F;
        if (Trader()) {
            const auto selected = std::find_if(tasks_.begin(), tasks_.end(), [&](const auto* task) {
                return task->id == taskId_;
            });
            if (selected != tasks_.end()) RevealTask(static_cast<std::size_t>(selected - tasks_.begin()));
        }
    }
    scrollTarget_ = std::clamp(scrollTarget_, 0.0F, MaxScroll());
    scroll_ = std::clamp(scroll_, 0.0F, MaxScroll());
    taskTarget_ = std::clamp(taskTarget_, 0.0F, TaskMaximum());
    taskScroll_ = std::clamp(taskScroll_, 0.0F, TaskMaximum());
}

void TasksPage::Refresh(bool queryChanged) {
    CloseMapTargets();
    if(!browser_)return;
    BeginScrollbarTransition();
    rows_=browser_->Query(Utf8(search_.Text()),mode_,locale_);traders_.clear();
    for(const auto& row:rows_)if(std::none_of(traders_.begin(),traders_.end(),[&](const auto* trader){return trader->id==row.trader->id;}))traders_.push_back(row.trader);
    auto selectedTrader = std::find_if(traders_.begin(), traders_.end(), [&](const auto* trader) {return trader->id == traderId_;});
    if (selectedTrader == traders_.end()) {
        traderId_ = traders_.empty() ? std::string{} : traders_.front()->id;
        selectedTrader = traders_.begin();
    }
    tasks_.clear();
    const auto* trader = Trader();
    if(trader)for(const auto& row:rows_)if(row.trader->id==trader->id)tasks_.push_back(row.source);
    auto selectedTask = trader ? std::find_if(tasks_.begin(), tasks_.end(), [&](const auto* task) {return task->id == taskId_;}) : tasks_.end();
    if (!trader || selectedTask == tasks_.end()) {
        taskId_ = tasks_.empty() ? std::string{} : tasks_.front()->id;
        selectedTask = tasks_.begin();
    }
    traderStrip_.Layout(D2D1::RectF(left_ + 26.0F, kTraderTop, (std::max)(left_ + 27.0F, right_ - 26.0F),
        kTraderBottom), traders_.size());
    const std::size_t traderPosition = selectedTrader == traders_.end() ? 0
        : static_cast<std::size_t>(selectedTrader - traders_.begin());
    const std::size_t taskPosition = selectedTask == tasks_.end() ? 0
        : static_cast<std::size_t>(selectedTask - tasks_.begin());
    traderAnimation_.Select(traderPosition, traders_.size(), queryChanged);
    taskAnimation_.Select(taskPosition, tasks_.size(), queryChanged);
    if (!traders_.empty()) traderStrip_.Reveal(traderPosition);
    if (!tasks_.empty()) RevealTask(taskPosition);
    if (queryChanged) {
        scroll_ = scrollTarget_ = taskScroll_ = taskTarget_ = 0.0F;
        taskHistory_.clear();
    }
}

void TasksPage::SelectTrader(std::size_t visibleIndex, bool snap) {
    if (visibleIndex >= traders_.size()) return;
    const auto* trader = traders_[visibleIndex];
    if (trader->id == traderId_) return;
    BeginScrollbarTransition();
    traderId_ = trader->id;
    taskId_.clear();
    taskHistory_.clear();
    Refresh(snap);
    traderAnimation_.Select(visibleIndex, traders_.size());
    traderStrip_.Reveal(visibleIndex);
    scroll_ = scrollTarget_ = taskScroll_ = taskTarget_ = 0.0F;
    detailProgress_ = 0.0F;
    textScrollTime_=0.0F;
}

void TasksPage::SelectTask(std::size_t visibleIndex, bool snap) {
    if (visibleIndex >= tasks_.size()) return;
    const auto* task = tasks_[visibleIndex];
    if (task->id == taskId_) return;
    CloseMapTargets();
    BeginScrollbarTransition();
    taskId_ = task->id;
    taskHistory_.clear();
    taskAnimation_.Select(visibleIndex, tasks_.size(), snap);
    RevealTask(visibleIndex);
    scroll_ = scrollTarget_ = 0.0F;
    detailProgress_ = 0.0F;
    textScrollTime_=0.0F;
}

void TasksPage::NavigateTask(std::string_view id,bool remember) {
    if(!browser_)return;
    const auto* target=browser_->Task(mode_,id);
    if(!target)return;
    if(target->id==taskId_)return;
    BeginScrollbarTransition();
    if(remember&&!taskId_.empty())taskHistory_.push_back(taskId_);
    if(!search_.Text().empty())search_.SetText({});
    traderId_=target->traderId;
    taskId_=target->id;
    Refresh();
    scroll_=scrollTarget_=0.0F;
    detailProgress_=0.0F;
    textScrollTime_=0.0F;
}

bool TasksPage::OpenTask(std::string_view id,data::GameMode mode) {
    if(!browser_||!browser_->Task(mode,id))return false;
    // 外部入口只接受稳定身份；清理旧任务链返回，不影响来源页的常驻状态。
    // External entry accepts stable identity only; clear old task-chain back state, preserving the source page.
    SetMode(mode);taskHistory_.clear();search_.SetText({});NavigateTask(id,false);Refresh();
    return taskId_==id;
}
bool TasksPage::GoBackTask() {
    while(!taskHistory_.empty()){
        const auto id=std::move(taskHistory_.back());
        taskHistory_.pop_back();
        if(browser_&&browser_->Task(mode_,id)){NavigateTask(id,false);return true;}
    }
    return false;
}

void TasksPage::RevealTask(std::size_t visibleIndex) {
    const auto bounds = TaskListBounds();
    const float viewport = (std::max)(1.0F, narrow_
        ? bounds.right - bounds.left : bounds.bottom - bounds.top - 24.0F);
    const float pitch = narrow_ ? kTaskPitch : kVerticalTaskPitch;
    const float extent = narrow_ ? kTaskWidth : 50.0F;
    const float position = static_cast<float>(visibleIndex) * pitch;
    if (position < taskTarget_) taskTarget_ = position;
    else if (position + extent > taskTarget_ + viewport)
        taskTarget_ = position + extent - viewport;
    taskTarget_ = std::clamp(taskTarget_, 0.0F, TaskMaximum());
}

float TasksPage::TaskMaximum() const {
    const auto bounds = TaskListBounds();
    if (!narrow_) return (std::max)(0.0F,
        24.0F + static_cast<float>(tasks_.size()) * kVerticalTaskPitch
        - (bounds.bottom - bounds.top));
    return (std::max)(0.0F, static_cast<float>(tasks_.size()) * kTaskPitch - 8.0F
        - (std::max)(1.0F, bounds.right - bounds.left));
}

float TasksPage::DetailHeight() const {
    const auto* task = Task();
    if (!task) return 160.0F;
    const auto viewport = DetailBounds();
    const float inner = (std::max)(200.0F, viewport.right - viewport.left - 56.0F);
    const float chainY=28.0F+184.0F+24.0F*static_cast<float>(MarkerRows(*task))+47.0F
        +ChainEntriesHeight(task->prerequisites.size())+ChainEntriesHeight(Unlocks().size());
    const float objectiveY=(stackedDetails_?chainY+20.0F:28.0F)+46.0F+ObjectiveRowsHeight(*task);
    const auto rewards=Rewards();
    const float rewardsTop=stackedDetails_?objectiveY+24.0F:(std::max)(chainY,objectiveY)+26.0F;
    const float rewardsHeight=47.0F+RewardGroupHeight(rewards.direct.size(),inner)
        +RewardGroupHeight(rewards.crafts.size(),inner)+RewardGroupHeight(rewards.offers.size(),inner);
    return rewardsTop+rewardsHeight+28.0F;
}

float TasksPage::MaxScroll() const {
    const auto viewport = DetailBounds();
    return (std::max)(0.0F, DetailHeight() - (viewport.bottom - viewport.top));
}

std::optional<ScrollbarGeometry> TasksPage::Bar() const {
    const auto viewport = DetailBounds();
    return MakeScrollbar(D2D1::RectF(right_ + 12.0F, viewport.top, right_ + 24.0F,
        viewport.bottom), DetailHeight(), scroll_);
}

std::optional<ScrollbarGeometry> TasksPage::TaskBar() const {
    if(narrow_)return {};
    const auto bounds=TaskListBounds();
    const auto track=D2D1::RectF(bounds.right-10.0F,bounds.top+12.0F,
        bounds.right,bounds.bottom-12.0F);
    const float viewport=track.bottom-track.top;
    return MakeScrollbar(track,viewport+TaskMaximum(),taskScroll_);
}

ScrollbarPose TasksPage::TaskBarPose() const {
    return SampleScrollbarTransition(taskBarFrom_,TaskBar(),taskBarProgress_);
}

void TasksPage::BeginScrollbarTransition() {
    // 切换数据前保留当前可见几何；快速重选从正在显示的长度接续。
    // Capture visible geometry before changing data; rapid reselection continues from the current length.
    taskBarFrom_=TaskBarPose().bar;
    detailBarFrom_=SampleScrollbarTransition(detailBarFrom_,Bar(),taskBarProgress_).bar;
    taskBarProgress_=0.0F;
}

D2D1_RECT_F TasksPage::TaskBackBounds() const {
    const auto bounds=DetailBounds();
    // 与正文共用滚动坐标；图标可见左沿对齐正文，中心对齐标题行。
    // Share content scrolling; align the visible glyph left with body text and center with the header.
    return D2D1::RectF(bounds.left+22.0F,bounds.top+28.0F-scroll_,
        bounds.left+48.0F,bounds.top+57.0F-scroll_);
}

D2D1_RECT_F TasksPage::TraderArrowBounds(int direction) const {
    const float x = direction < 0 ? left_ + 12.0F : right_ - 12.0F;
    return D2D1::RectF(x - 12.0F, 174.0F, x + 12.0F, 218.0F);
}

std::optional<int> TasksPage::TraderArrowAt(float x, float y) const {
    for (const int direction : {-1, 1})
        if (HitNavigationButton(TraderArrowBounds(direction), x, y, traderStrip_.CanMove(direction)))
            return direction;
    return {};
}

std::optional<std::size_t> TasksPage::TaskAt(float x, float y) const {
    const auto bounds = TaskListBounds();
    if (x < bounds.left || x >= bounds.right || y < bounds.top || y >= bounds.bottom) return {};
    const float local = narrow_ ? x - bounds.left + taskScroll_ : y - bounds.top - 12.0F + taskScroll_;
    if (local < 0.0F) return {};
    const float pitch = narrow_ ? kTaskPitch : kVerticalTaskPitch;
    const float extent = narrow_ ? kTaskWidth : 50.0F;
    const auto index = static_cast<std::size_t>(local / pitch);
    return index < tasks_.size() && std::fmod(local, pitch) < extent
        ? std::optional<std::size_t>(index) : std::nullopt;
}

std::optional<std::string> TasksPage::ChainAt(float x, float y) const {
    const auto* task=Task();const auto viewport=DetailBounds();
    if(!task||x<viewport.left||x>=viewport.right||y<viewport.top||y>=viewport.bottom)return {};
    const float innerLeft=viewport.left+28.0F;
    const float innerWidth=(std::max)(200.0F,viewport.right-viewport.left-56.0F);
    const float leftWidth=stackedDetails_?innerWidth:(innerWidth-26.0F)*0.42F;
    if(x<innerLeft||x>=innerLeft+leftWidth)return {};
    float rowY=viewport.top-scroll_+28.0F+184.0F
        +24.0F*static_cast<float>(MarkerRows(*task))+47.0F;
    const auto hit=[&](const std::vector<std::string>& entries)->std::optional<std::string>{
        rowY+=27.0F;
        if(entries.empty()){rowY+=34.0F;return {};}
        for(const auto& id:entries){
            if(y>=rowY&&y<rowY+31.0F)return id;
            rowY+=38.0F;
        }
        return {};
    };
    if(const auto id=hit(task->prerequisites))return id;
    return hit(Unlocks());
}

std::optional<std::string> TasksPage::RewardItemAt(float x,float y) const {
    const auto* task=Task();const auto viewport=DetailBounds();
    if(!task||x<viewport.left||x>=viewport.right||y<viewport.top||y>=viewport.bottom)return {};
    const float innerLeft=viewport.left+28.0F,innerRight=viewport.right-28.0F;
    const float innerWidth=innerRight-innerLeft;
    const float chainY=viewport.top-scroll_+28.0F+184.0F
        +24.0F*static_cast<float>(MarkerRows(*task))+47.0F
        +ChainEntriesHeight(task->prerequisites.size())+ChainEntriesHeight(Unlocks().size());
    const float objectiveY=(stackedDetails_?chainY+20.0F:viewport.top-scroll_+28.0F)
        +46.0F+ObjectiveRowsHeight(*task);
    float rewardY=(stackedDetails_?objectiveY+24.0F:(std::max)(chainY,objectiveY)+26.0F)+47.0F;
    const auto rewards=Rewards();const auto columns=RewardColumns(innerWidth);
    const float rewardGap=10.0F;
    const float rewardWidth=(innerWidth-rewardGap*static_cast<float>(columns-1))/static_cast<float>(columns);
    const auto hit=[&](const std::vector<RewardEntry>& entries)->std::optional<std::string>{
        if(entries.empty())return {};
        rewardY+=30.0F;
        for(std::size_t i=0;i<entries.size();++i){
            const float rowY=rewardY+static_cast<float>(i/columns)*70.0F;
            const float rowX=innerLeft+static_cast<float>(i%columns)*(rewardWidth+rewardGap);
            if(!entries[i].itemId.empty()&&x>=rowX&&x<rowX+rewardWidth&&y>=rowY&&y<rowY+58.0F)
                return entries[i].itemId;
        }
        rewardY+=70.0F*static_cast<float>((entries.size()+columns-1)/columns)+10.0F;
        return {};
    };
    if(const auto item=hit(rewards.direct))return item;
    if(const auto item=hit(rewards.crafts))return item;
    return hit(rewards.offers);
}

std::optional<D2D1_RECT_F> TasksPage::ObjectiveBounds(std::string_view id) const {
    const auto* task=Task();if(!task)return {};
    const auto viewport=DetailBounds();const float left=viewport.left+28,width=viewport.right-viewport.left-56;
    const float leftWidth=stackedDetails_?width:(width-26)*.42F;
    const float column=stackedDetails_?left:left+leftWidth+26;
    const float chainEnd=viewport.top-scroll_+28+184+24*static_cast<float>(MarkerRows(*task))+47
        +ChainEntriesHeight(task->prerequisites.size())+ChainEntriesHeight(Unlocks().size());
    float y=(stackedDetails_?chainEnd+20:viewport.top-scroll_+28)+46;
    for(const auto& objective:task->objectives){const float height=objective.itemIds.empty()?54.0F:70.0F;
        if(objective.id==id)return D2D1_RECT_F{column,y,viewport.right-28,y+height};y+=height+8;}
    return {};
}
std::optional<std::string> TasksPage::ObjectiveMapPointAt(float x,float y) const {
    const auto* task=Task();const auto viewport=DetailBounds();
    if(!task||x<viewport.left||x>=viewport.right||y<viewport.top||y>=viewport.bottom)return {};
    for(const auto& objective:task->objectives){const auto targets=MapLinks().Targets(task->id,objective.id);
        if(targets.empty())continue;const auto row=ObjectiveBounds(objective.id);
        if(row&&x>=row->left&&x<row->right&&y>=row->top&&y<row->bottom)return objective.id;}
    return {};
}
void TasksPage::MouseDown(float x, float y) {
    pressedMapPoint_.reset();
    pressedTrader_.reset(); pressedTask_.reset(); pressedChain_.reset(); pressedRewardItem_.reset(); pressedArrow_.reset(); scrollGrab_.reset(); taskScrollGrab_.reset();
    if(mapObjective_){
        pressedTarget_=MapTargetAt(x,y);
        if(const auto layout=MapTargetsLayout()){
            const auto close=layout->close;
            mapClosePressed_=x>=close.left&&x<close.right&&y>=close.top&&y<close.bottom;
            const auto bounds=layout->bounds;
            mapDismissPressed_=x<bounds.left||x>=bounds.right||y<bounds.top||y>=bounds.bottom;
        } else mapDismissPressed_=true;
        return;
    }
    if (search_.HitTest(SearchBounds(), x, y)) search_.Focus(); else search_.Blur();
    taskBackPressed_=HitNavigationButton(TaskBackBounds(),x,y,!taskHistory_.empty()
        &&y>=DetailBounds().top&&y<DetailBounds().bottom);
    if(taskBackPressed_)return;
    if (traderStrip_.Press(x, y)) return;
    if (const auto bar = Bar(); bar && x >= bar->track.left && x < bar->track.right
        && y >= bar->track.top && y < bar->track.bottom) {
        scrollGrab_ = y >= bar->thumb.top && y < bar->thumb.bottom ? y - bar->thumb.top
            : (bar->thumb.bottom - bar->thumb.top) * 0.5F;
        MouseMove(x, y);
        return;
    }
    if(const auto bar=TaskBar();bar&&x>=bar->track.left&&x<bar->track.right
        &&y>=bar->track.top&&y<bar->track.bottom){
        taskScrollGrab_=y>=bar->thumb.top&&y<bar->thumb.bottom?y-bar->thumb.top
            :(bar->thumb.bottom-bar->thumb.top)*.5F;
        MouseMove(x,y);
        return;
    }
    pressedArrow_ = TraderArrowAt(x, y);
    pressedTrader_ = traderStrip_.Hit(x, y);
    pressedTask_ = TaskAt(x, y);
    pressedChain_ = ChainAt(x, y);
    pressedRewardItem_=RewardItemAt(x,y);
    pressedMapPoint_=ObjectiveMapPointAt(x,y);
}

std::optional<TasksPage::Action> TasksPage::MouseUp(float x, float y) {
    if(mapObjective_){
        const auto target=pressedTarget_&&pressedTarget_==MapTargetAt(x,y)?pressedTarget_:std::optional<std::string>{};
        bool close=mapDismissPressed_;
        if(const auto layout=MapTargetsLayout()){
            const auto rect=layout->close;
            close|=mapClosePressed_&&x>=rect.left&&x<rect.right&&y>=rect.top&&y<rect.bottom;
        }
        pressedTarget_.reset();mapClosePressed_=mapDismissPressed_=false;
        if(target||close)CloseMapTargets();
        if(target)return Action{Action::Destination::Map,*target,mode_};
        return {};
    }
    if(taskBackPressed_){
        taskBackPressed_=false;
        if(HitNavigationButton(TaskBackBounds(),x,y,!taskHistory_.empty()
            &&y>=DetailBounds().top&&y<DetailBounds().bottom))GoBackTask();
        return {};
    }
    if (traderStrip_.Release()) return {};
    if (scrollGrab_) { MouseMove(x, y); scrollGrab_.reset(); return {}; }
    if(taskScrollGrab_){MouseMove(x,y);taskScrollGrab_.reset();return {};}
    if (pressedArrow_ && pressedArrow_ == TraderArrowAt(x, y)) traderStrip_.Move(*pressedArrow_ * 272.0F);
    if (pressedTrader_ && pressedTrader_ == traderStrip_.Hit(x, y)) SelectTrader(*pressedTrader_);
    if (pressedTask_ && pressedTask_ == TaskAt(x, y)) SelectTask(*pressedTask_);
    if(pressedChain_&&pressedChain_==ChainAt(x,y))NavigateTask(*pressedChain_);
    const auto reward=pressedRewardItem_&&pressedRewardItem_==RewardItemAt(x,y)
        ?pressedRewardItem_:std::optional<std::string>{};
    const auto point=pressedMapPoint_&&pressedMapPoint_==ObjectiveMapPointAt(x,y)?pressedMapPoint_:std::optional<std::string>{};
    pressedMapPoint_.reset();
    pressedTrader_.reset(); pressedTask_.reset(); pressedChain_.reset(); pressedRewardItem_.reset(); pressedArrow_.reset();
    if(point){
        const auto targets=MapLinks().Targets(taskId_,*point);
        if(targets.size()==1)return Action{Action::Destination::Map,targets.front(),mode_};
        if(targets.size()>1){mapObjective_=*point;mapTargetOffset_=0;}
        return {};
    }
    if(reward)return Action{Action::Destination::Prices,*reward};return {};
}

void TasksPage::MouseMove(float x, float y) {
    if(mapObjective_){hoveredTarget_=MapTargetAt(x,y);return;}
    hoveredMapPoint_=ObjectiveMapPointAt(x,y);
    taskBackHovered_=HitNavigationButton(TaskBackBounds(),x,y,!taskHistory_.empty()
        &&y>=DetailBounds().top&&y<DetailBounds().bottom);
    traderStrip_.Drag(x);
    hoveredArrow_ = TraderArrowAt(x, y);
    const auto hovered=TaskAt(x,y);if(hovered!=hoveredTask_){hoveredTask_=hovered;textScrollTime_=0.0F;}
    if (scrollGrab_) if (const auto bar = Bar())
        scroll_ = scrollTarget_ = bar->OffsetFromThumbTop(y - *scrollGrab_);
    if(taskScrollGrab_)if(const auto bar=TaskBar()){
        taskScroll_=taskTarget_=bar->OffsetFromThumbTop(y-*taskScrollGrab_);
        taskBarProgress_=1.0F;
    }
}

void TasksPage::CancelDrag() {
    pressedTarget_.reset();mapClosePressed_=mapDismissPressed_=false;
    pressedMapPoint_.reset();
    traderStrip_.Release();
    scrollGrab_.reset();
    taskScrollGrab_.reset();
    taskBackPressed_=false;
}

bool TasksPage::Wheel(int delta, float x, float y) {
    if(mapObjective_){
        if(const auto layout=MapTargetsLayout()){
            const auto count=MapLinks().Targets(taskId_,*mapObjective_).size();
            const auto maximum=count-layout->rows.size();
            const int steps=delta==0?0:delta>0?-1:1;
            mapTargetOffset_=static_cast<std::size_t>(std::clamp(static_cast<int>((std::min)(mapTargetOffset_,maximum))+steps,0,static_cast<int>(maximum)));
            pressedTarget_.reset();hoveredTarget_.reset();
        }
        return true;
    }
    const float steps = -static_cast<float>(delta) / WHEEL_DELTA;
    if (y >= kTraderTop && y < kTraderBottom && x >= left_ && x < right_)
        traderStrip_.Move(steps * 136.0F);
    else if (const auto bounds = TaskListBounds(); x >= bounds.left && x < bounds.right
        && y >= bounds.top && y < bounds.bottom)
        taskTarget_ = std::clamp(taskTarget_ + steps * (narrow_ ? kTaskPitch : kVerticalTaskPitch),
            0.0F, TaskMaximum());
    else scrollTarget_ = std::clamp(scrollTarget_ + steps * 66.0F, 0.0F, MaxScroll());
    return Animating();
}

bool TasksPage::Key(WPARAM key, bool control) {
    if(mapObjective_){if(key==VK_ESCAPE)CloseMapTargets();return true;}
    const auto before = search_.Text();
    if (!search_.HandleKeyDown(key, control)) return false;
    if (before != search_.Text()) Refresh(true);
    return true;
}

bool TasksPage::Char(wchar_t character) {
    if(mapObjective_)return true;
    if (!search_.HandleChar(character)) return false;
    Refresh(true);
    return true;
}

bool TasksPage::Tick(float seconds) {
    seconds = std::clamp(seconds, 0.0F, 0.05F);
    taskBarProgress_=(std::min)(1.0F,taskBarProgress_+seconds/.36F);
    traderStrip_.Tick(seconds);
    traderAnimation_.Tick(seconds);
    taskAnimation_.Tick(seconds);
    textScrollTime_+=seconds;
    detailProgress_ = (std::min)(1.0F, detailProgress_ + seconds / 0.30F);
    scrollTarget_ = (std::min)(scrollTarget_, MaxScroll());
    scroll_ += (scrollTarget_ - scroll_) * (1.0F - std::exp(-24.0F * seconds));
    taskScroll_ += (taskTarget_ - taskScroll_) * (1.0F - std::exp(-20.0F * seconds));
    if (std::abs(scrollTarget_ - scroll_) < 0.1F) scroll_ = scrollTarget_;
    if (std::abs(taskTarget_ - taskScroll_) < 0.1F) taskScroll_ = taskTarget_;
    return Animating();
}

bool TasksPage::Animating() const {
    return traderStrip_.Animating() || traderAnimation_.Active() || taskAnimation_.Active()
        || taskBarProgress_<1.0F
        || detailProgress_ < 1.0F || textOverflowActive_ || std::abs(scrollTarget_ - scroll_) >= 0.1F
        || std::abs(taskTarget_ - taskScroll_) >= 0.1F;
}

std::vector<std::string> TasksPage::VisibleImages() const {
    std::vector<std::string> result;
    for(std::size_t i=0;i<traders_.size();++i){
        const auto rect=traderStrip_.Card(i);
        if(rect.right<left_||rect.left>right_||traders_[i]->imageKey.empty())continue;
        result.push_back(traders_[i]->imageKey);
    }
    const auto rewards=Rewards();
    const auto append=[&](const std::vector<RewardEntry>& entries){
        for(const auto& entry:entries)if(!entry.itemId.empty()&&result.size()<32
            &&std::find(result.begin(),result.end(),entry.itemId)==result.end())result.push_back(entry.itemId);
    };
    append(rewards.direct);append(rewards.crafts);append(rewards.offers);
    return result;
}

void TasksPage::Draw(const UiCanvas& canvas, const UiTheme& theme,
    const std::unordered_map<std::string,Microsoft::WRL::ComPtr<ID2D1Bitmap>>& images) const {
    DrawPageHeader(canvas, theme, left_, right_, Tr(TextKey::NavTasks));
    search_.Draw(canvas, theme, SearchBounds(), Tr(TextKey::TasksSearch), true);

    canvas.target.PushAxisAlignedClip(D2D1::RectF(left_ + 26.0F, kTraderTop, right_ - 26.0F, kTraderBottom),
        D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    for (std::size_t i = 0; i < traders_.size(); ++i) {
        const auto rect = traderStrip_.Card(i);
        if (rect.right < left_ + 26.0F || rect.left > right_ - 26.0F) continue;
        const auto* trader = traders_[i];
        const float weight = traderAnimation_.Weight(i);
        const auto color = D2D1::ColorF(
            theme.secondaryText.r + (theme.primaryText.r - theme.secondaryText.r) * weight,
            theme.secondaryText.g + (theme.primaryText.g - theme.secondaryText.g) * weight,
            theme.secondaryText.b + (theme.primaryText.b - theme.secondaryText.b) * weight, 1.0F);
        canvas.Round(rect, theme.cornerRadius, theme.surface);
        canvas.Round(D2D1::RectF(rect.left + 30.0F, rect.top + 8.0F, rect.right - 30.0F, rect.top + 61.0F),
            7.0F, theme.background);
        const auto oldBody = canvas.body.GetTextAlignment();
        const auto oldSmall = canvas.smallFormat.GetTextAlignment();
        canvas.body.SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        canvas.smallFormat.SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        const auto traderName=Text(trader->nameZh,trader->nameEn);
        const auto picture=D2D1::RectF(rect.left+30.0F,rect.top+8.0F,rect.right-30.0F,rect.top+61.0F);
        if(const auto image=images.find(trader->imageKey);image!=images.end()&&image->second){
            const auto size=image->second->GetSize();const float scale=(std::min)((picture.right-picture.left)/size.width,(picture.bottom-picture.top)/size.height);
            const float w=size.width*scale,h=size.height*scale,cx=(picture.left+picture.right)*.5F,cy=(picture.top+picture.bottom)*.5F;
            canvas.target.DrawBitmap(image->second.Get(),D2D1::RectF(cx-w*.5F,cy-h*.5F,cx+w*.5F,cy+h*.5F),1,D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        } else {
            const std::wstring badge=traderName.empty()?L"?":traderName.substr(0,1);
            canvas.Text(badge, canvas.body, D2D1::RectF(rect.left + 30.0F, rect.top + 23.0F,rect.right - 30.0F, rect.top + 50.0F), color);
        }
        canvas.Text(traderName, canvas.smallFormat, D2D1::RectF(rect.left + 5.0F, rect.top + 67.0F,
            rect.right - 5.0F, rect.top + 91.0F), color);
        const auto taskCount=std::count_if(rows_.begin(),rows_.end(),[&](const auto& row){return row.trader->id==trader->id;});
        canvas.Text(std::to_wstring(taskCount), canvas.smallFormat,
            D2D1::RectF(rect.left + 5.0F, rect.top + 89.0F, rect.right - 5.0F, rect.bottom),
            weight > 0.5F ? theme.accent : theme.secondaryText);
        canvas.body.SetTextAlignment(oldBody);
        canvas.smallFormat.SetTextAlignment(oldSmall);
    }
    if (!traders_.empty()) {
        const float x = traderStrip_.Card(0).left + traderAnimation_.Position() * HorizontalCardStrip::pitch;
        canvas.Fill(D2D1::RectF(x + 24.0F, kTraderBottom - 14.0F,
            x + HorizontalCardStrip::cardWidth - 24.0F, kTraderBottom - 12.0F), theme.accent);
    }
    if (traderStrip_.Maximum() > 0.0F) {
        const auto thumb = traderStrip_.Thumb();
        auto color = theme.secondaryText; color.a = 0.45F;
        canvas.Round(D2D1::RectF(thumb.left, kTraderBottom - 5.0F, thumb.right, kTraderBottom - 2.0F), 1.5F, color);
    }
    canvas.target.PopAxisAlignedClip();
    if (traderStrip_.Maximum() > 0.0F) for (const int direction : {-1, 1})
        DrawNavigationButton(canvas, theme, TraderArrowBounds(direction),
            direction < 0 ? NavigationGlyph::Left : NavigationGlyph::Right,
            traderStrip_.CanMove(direction), hoveredArrow_ == direction, pressedArrow_ == direction);

    const auto* trader = Trader();
    const auto taskBounds = TaskListBounds();
    textOverflowActive_=false;
    if (!narrow_) canvas.Round(taskBounds, theme.cornerRadius, theme.surface);
    canvas.target.PushAxisAlignedClip(taskBounds, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    const auto oldAlign = canvas.body.GetTextAlignment();
    canvas.body.SetTextAlignment(narrow_ ? DWRITE_TEXT_ALIGNMENT_CENTER : DWRITE_TEXT_ALIGNMENT_LEADING);
    for (std::size_t i = 0; trader && i < tasks_.size(); ++i) {
        const auto marker=tasks_[i]->kappaRequired?TaskMarker::Check
            :tasks_[i]->lightkeeperRequired?TaskMarker::Firework:TaskMarker::Blocked;
        const float weight = taskAnimation_.Weight(i);
        const auto from = theme.secondaryText;
        const auto color = D2D1::ColorF(from.r + (theme.primaryText.r - from.r) * weight,
            from.g + (theme.primaryText.g - from.g) * weight,
            from.b + (theme.primaryText.b - from.b) * weight, 1.0F);
        if (narrow_) {
            const float x = taskBounds.left + i * kTaskPitch - taskScroll_;
            if (weight > 0.08F) canvas.Round(D2D1::RectF(x, taskBounds.top + 4.0F,
                x + kTaskWidth, taskBounds.bottom - 9.0F), 7.0F,
                D2D1::ColorF(theme.selected.r, theme.selected.g, theme.selected.b,
                    0.35F + weight * 0.45F));
            DrawTaskMarker(canvas,D2D1::Point2F(x+15.0F,taskBounds.top+26.0F),marker,color);
            textOverflowActive_|=DrawOverflowText(canvas,Text(tasks_[i]->nameZh,tasks_[i]->nameEn),canvas.body,
                D2D1::RectF(x+29.0F,taskBounds.top+13.0F,x+kTaskWidth-8.0F,taskBounds.bottom-10.0F),color,
                weight>.5F||hoveredTask_==i,textScrollTime_);
        } else {
            const float y = taskBounds.top + 12.0F + i * kVerticalTaskPitch - taskScroll_;
            const auto row = D2D1::RectF(taskBounds.left + 10.0F, y,
                taskBounds.right - 10.0F, y + 50.0F);
            if (weight > 0.08F) canvas.Round(row, 7.0F,
                D2D1::ColorF(theme.selected.r, theme.selected.g, theme.selected.b,
                    0.35F + weight * 0.45F));
            DrawTaskMarker(canvas,D2D1::Point2F(row.left+20.0F,row.top+25.0F),marker,color);
            textOverflowActive_|=DrawOverflowText(canvas,Text(tasks_[i]->nameZh,tasks_[i]->nameEn),canvas.body,
                D2D1::RectF(row.left+36.0F,row.top+13.0F,row.right-10.0F,row.bottom-8.0F),color,
                weight>.5F||hoveredTask_==i,textScrollTime_);
        }
    }
    if (!tasks_.empty()) {
        if (narrow_) {
            const float x = taskBounds.left + taskAnimation_.Position() * kTaskPitch - taskScroll_;
            canvas.Fill(D2D1::RectF(x + 30.0F, taskBounds.bottom - 5.0F,
                x + kTaskWidth - 30.0F, taskBounds.bottom - 3.0F), theme.accent);
        } else {
            const float y = taskBounds.top + 12.0F
                + taskAnimation_.Position() * kVerticalTaskPitch - taskScroll_;
            canvas.Fill(D2D1::RectF(taskBounds.left + 10.0F, y + 9.0F,
                taskBounds.left + 13.0F, y + 41.0F), theme.accent);
        }
    }
    canvas.body.SetTextAlignment(oldAlign);
    if (TaskMaximum() > 0.0F) {
        auto color = theme.secondaryText; color.a = 0.35F;
        if (narrow_) {
            const float width = taskBounds.right - taskBounds.left;
            const float thumbWidth = (std::max)(32.0F, width * width / (width + TaskMaximum()));
            const float x = taskBounds.left + taskScroll_ / TaskMaximum() * (width - thumbWidth);
            canvas.Round(D2D1::RectF(x, taskBounds.bottom - 1.5F,
                x + thumbWidth, taskBounds.bottom), 1.0F, color);
        }
    }
    if(!narrow_)DrawScrollbar(canvas,theme,TaskBarPose());
    canvas.target.PopAxisAlignedClip();

    const auto viewport = DetailBounds();
    canvas.target.PushAxisAlignedClip(viewport, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    canvas.Round(viewport, theme.cornerRadius, theme.surface);
    const auto* task = Task();
    if (!task || !trader) {
        DrawEmptyState(canvas, theme,
            D2D1::RectF(viewport.left, viewport.top, viewport.right, viewport.top + 150.0F),
            viewport.right - 20.0F, Tr(TextKey::TasksNoResults), Tr(TextKey::TasksNoResultsHint));
    } else {
        const auto pose = SampleTabTransition(detailProgress_);
        ScopedContentTransition transition(canvas, D2D1::Point2F(0, 0), pose.incomingOpacity, 1.0F);
        D2D1_MATRIX_3X2_F transform; canvas.target.GetTransform(&transform);
        canvas.target.SetTransform(D2D1::Matrix3x2F::Translation(0.0F,
            5.0F * (1.0F - pose.incomingOpacity)) * transform);
        const float base = viewport.top - scroll_;
        const float innerLeft = viewport.left + 28.0F;
        const float innerRight = viewport.right - 28.0F;
        const float innerWidth = innerRight - innerLeft;
        const float gap = 26.0F;
        const float leftWidth = stackedDetails_ ? innerWidth : (innerWidth - gap) * 0.42F;
        const float rightColumn = stackedDetails_ ? innerLeft : innerLeft + leftWidth + gap;
        const float rightWidth = stackedDetails_ ? innerWidth : innerWidth - leftWidth - gap;
        const auto sectionTitle = [&](std::wstring_view title, float x, float y, float width) {
            canvas.Text(title, canvas.label, D2D1::RectF(x, y, x + width, y + 29.0F), theme.primaryText);
            canvas.Fill(D2D1::RectF(x, y + 31.0F, x + 38.0F, y + 33.0F), theme.accent);
        };
        const auto line = [&](std::wstring_view label, std::wstring_view value, float x, float y, float width) {
            canvas.Text(label, canvas.smallFormat, D2D1::RectF(x, y, x + 100.0F, y + 25.0F), theme.secondaryText);
            canvas.Text(value, canvas.body, D2D1::RectF(x + 102.0F, y, x + width, y + 25.0F), theme.primaryText);
        };
        const float informationTop = base + 28.0F;
        if(!taskHistory_.empty())DrawNavigationButton(canvas,theme,TaskBackBounds(),
            NavigationGlyph::Back,true,taskBackHovered_,taskBackPressed_);
        const float headerInset=taskHistory_.empty()?0.0F:40.0F;
        sectionTitle(Tr(TextKey::TasksInformation), innerLeft+headerInset, informationTop, leftWidth-headerInset);
        canvas.Text(Text(task->nameZh,task->nameEn), canvas.label, D2D1::RectF(innerLeft, informationTop + 47.0F,
            innerLeft + leftWidth, informationTop + 78.0F), theme.primaryText);
        canvas.Text(Text(trader->nameZh,trader->nameEn)+L" · "+std::wstring(task->faction.begin(),task->faction.end()), canvas.smallFormat, D2D1::RectF(innerLeft, informationTop + 79.0F,
            innerLeft + leftWidth, informationTop + 104.0F), theme.accent);
        float markerY=informationTop+111.0F;
        const auto markerLine=[&](TaskMarker marker,std::wstring_view label){
            DrawTaskMarker(canvas,D2D1::Point2F(innerLeft+8.0F,markerY+9.0F),marker,theme.accent);
            canvas.Text(label,canvas.smallFormat,D2D1::RectF(innerLeft+24.0F,markerY-2.0F,
                innerLeft+leftWidth,markerY+22.0F),theme.secondaryText);
            markerY+=24.0F;
        };
        if(task->kappaRequired)markerLine(TaskMarker::Check,Tr(TextKey::TasksMarkerKappa));
        if(task->lightkeeperRequired)markerLine(TaskMarker::Firework,Tr(TextKey::TasksMarkerLightkeeper));
        if(!task->kappaRequired&&!task->lightkeeperRequired)
            markerLine(TaskMarker::Blocked,Tr(TextKey::TasksMarkerUnrelated));
        const auto location=Text(task->locationZh,task->locationEn);
        line(Tr(TextKey::TasksLocation), location.empty()||location==L"—"
            ?Tr(TextKey::TasksAnyLocation):location, innerLeft, markerY, leftWidth);
        line(Tr(TextKey::TasksLevel), std::to_wstring(task->minimumLevel), innerLeft, markerY+28.0F, leftWidth);

        const float chainTop = informationTop + 184.0F
            +24.0F*static_cast<float>(MarkerRows(*task));
        sectionTitle(Tr(TextKey::TasksChain), innerLeft, chainTop, leftWidth);
        float chainY = chainTop + 47.0F;
        const auto taskName = [&](std::string_view id) {
            const auto* found=browser_->Task(mode_,id);
            return found?Text(found->nameZh,found->nameEn):std::wstring(id.begin(),id.end());
        };
        const auto chain = [&](std::wstring_view heading, const std::vector<std::string>& entries) {
            canvas.Text(heading, canvas.smallFormat, D2D1::RectF(innerLeft, chainY,
                innerLeft + leftWidth, chainY + 25.0F), theme.secondaryText);
            chainY += 27.0F;
            if (entries.empty()) {
                canvas.Text(L"—", canvas.body, D2D1::RectF(innerLeft, chainY,
                    innerLeft + leftWidth, chainY + 28.0F), theme.secondaryText);
                chainY += 34.0F;
            } else for (const auto id : entries) {
                canvas.Round(D2D1::RectF(innerLeft, chainY, innerLeft + leftWidth, chainY + 31.0F), 6.0F, theme.background);
                canvas.Text(taskName(id), canvas.smallFormat, D2D1::RectF(innerLeft + 12.0F, chainY + 4.0F,
                    innerLeft + leftWidth - 10.0F, chainY + 29.0F), theme.primaryText);
                chainY += 38.0F;
            }
        };
        chain(Tr(TextKey::TasksRequires), task->prerequisites);
        const auto unlocks=Unlocks();
        chain(Tr(TextKey::TasksUnlocks), unlocks);

        const float objectivesTop = stackedDetails_ ? chainY + 20.0F : informationTop;
        sectionTitle(Tr(TextKey::TasksObjectives), rightColumn, objectivesTop, rightWidth);
        float objectiveY = objectivesTop + 46.0F;
        for (const auto& objective : task->objectives) {
            const bool hasItem=!objective.itemIds.empty();
            const auto targets=MapLinks().Targets(task->id,objective.id);
            const bool linked=!targets.empty();
            const float rowHeight = hasItem ? 70.0F : 54.0F;
            canvas.Round(D2D1::RectF(rightColumn, objectiveY, rightColumn + rightWidth,
                objectiveY + rowHeight), 7.0F, linked&&hoveredMapPoint_==objective.id?theme.hover:theme.background);
            if(linked){const D2D1_POINT_2F center{rightColumn+rightWidth-18,objectiveY+20};
                canvas.brush.SetColor(theme.accent);canvas.target.DrawEllipse(D2D1::Ellipse(center,5,5),&canvas.brush,1.5F);
                canvas.target.DrawLine({center.x,center.y+5},{center.x,center.y+10},&canvas.brush,1.5F);
                canvas.Text(Tr("tasks.map.focus"),canvas.smallFormat,{rightColumn+rightWidth-98,objectiveY+34,rightColumn+rightWidth-12,objectiveY+rowHeight-5},theme.accent);}
            canvas.Circle(D2D1::Point2F(rightColumn + 18.0F, objectiveY + 20.0F), 7.0F, theme.accent);
            canvas.Circle(D2D1::Point2F(rightColumn + 18.0F, objectiveY + 20.0F), 4.0F, theme.background);
            float textLeft = rightColumn + 34.0F;
            if (hasItem) {
                canvas.Round(D2D1::RectF(textLeft, objectiveY + 10.0F, textLeft + 42.0F,
                    objectiveY + 52.0F), 5.0F, theme.surface);
                canvas.Text(L"◇", canvas.body, D2D1::RectF(textLeft + 11.0F, objectiveY + 19.0F,
                    textLeft + 34.0F, objectiveY + 43.0F), theme.secondaryText);
                textLeft += 54.0F;
            }
            canvas.Text(Text(objective.descriptionZh,objective.descriptionEn), canvas.body, D2D1::RectF(textLeft, objectiveY + 8.0F,
                rightColumn + rightWidth - (linked?32.0F:12.0F), objectiveY + 33.0F), theme.primaryText);
            std::wstring detail;
            if(hasItem){const auto* item=browser_->Item(objective.itemIds.front());detail=item?Text(item->nameZh,item->nameEn):std::wstring(objective.itemIds.front().begin(),objective.itemIds.front().end());if(objective.count>0){std::wostringstream out;out<<detail<<L" ×"<<objective.count;detail=out.str();}}
            canvas.Text(detail, canvas.smallFormat, D2D1::RectF(textLeft, objectiveY + 34.0F,
                rightColumn + rightWidth - (linked?104.0F:12.0F), objectiveY + rowHeight - 5.0F), theme.secondaryText);
            objectiveY += rowHeight + 8.0F;
        }

        const float rewardsTop = stackedDetails_ ? objectiveY + 24.0F
            : (std::max)(chainY, objectiveY) + 26.0F;
        sectionTitle(Tr(TextKey::TasksRewards), innerLeft, rewardsTop, innerWidth);
        const std::size_t columns = RewardColumns(innerWidth);
        const float rewardGap = 10.0F;
        const float rewardWidth = (innerWidth - rewardGap * static_cast<float>(columns - 1))
            / static_cast<float>(columns);
        const auto rewards=Rewards();
        float rewardY=rewardsTop+47.0F;
        const auto rewardGroup=[&](std::wstring_view heading,const std::vector<RewardEntry>& entries){
            if(entries.empty())return;
            canvas.Text(heading,canvas.body,D2D1::RectF(innerLeft,rewardY,innerRight,rewardY+26.0F),theme.secondaryText);
            rewardY+=30.0F;
            for(std::size_t i=0;i<entries.size();++i){
                const std::size_t row=i/columns,column=i%columns;
                const float x=innerLeft+column*(rewardWidth+rewardGap);
                const float y=rewardY+row*70.0F;
                canvas.Round(D2D1::RectF(x,y,x+rewardWidth,y+58.0F),7.0F,theme.background);
                float textLeft=x+13.0F;
                if(!entries[i].itemId.empty()){
                    const auto picture=D2D1::RectF(x+7.0F,y+7.0F,x+51.0F,y+51.0F);
                    canvas.Round(picture,5.0F,theme.surface);
                    if(const auto image=images.find(entries[i].itemId);image!=images.end()&&image->second){
                        const auto size=image->second->GetSize();
                        const float scale=(std::min)((picture.right-picture.left)/size.width,
                            (picture.bottom-picture.top)/size.height);
                        const float w=size.width*scale,h=size.height*scale;
                        const float cx=(picture.left+picture.right)*.5F,cy=(picture.top+picture.bottom)*.5F;
                        canvas.target.DrawBitmap(image->second.Get(),D2D1::RectF(cx-w*.5F,cy-h*.5F,cx+w*.5F,cy+h*.5F),
                            1,D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                    }
                    textLeft=x+61.0F;
                }
                canvas.Text(entries[i].text,canvas.smallFormat,D2D1::RectF(textLeft,y+16.0F,
                    x+rewardWidth-10.0F,y+44.0F),theme.primaryText);
            }
            rewardY+=70.0F*static_cast<float>((entries.size()+columns-1)/columns)+10.0F;
        };
        rewardGroup(Tr(TextKey::TasksDirectRewards),rewards.direct);
        rewardGroup(Tr(TextKey::TasksCraftUnlocks),rewards.crafts);
        rewardGroup(Tr(TextKey::TasksOfferUnlocks),rewards.offers);
    }
    canvas.target.PopAxisAlignedClip();
    DrawScrollbar(canvas, theme, SampleScrollbarTransition(detailBarFrom_,Bar(),taskBarProgress_));
    if(const auto layout=MapTargetsLayout()){
        canvas.target.PushAxisAlignedClip(DetailBounds(),D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        canvas.Round(layout->bounds,theme.cornerRadius,theme.surface);
        const auto targets=MapLinks().Targets(taskId_,*mapObjective_);
        const auto first=(std::min)(mapTargetOffset_,targets.size()-layout->rows.size())+1;
        const auto heading=Tr(TextKey::TasksObjectives)+L"  "+std::to_wstring(first)+L"–"
            +std::to_wstring(first+layout->rows.size()-1)+L" / "+std::to_wstring(targets.size());
        canvas.Text(heading,canvas.smallFormat,
            {layout->bounds.left+10,layout->bounds.top+8,layout->close.left-4,layout->bounds.top+32},theme.primaryText);
        canvas.Text(L"×",canvas.body,layout->close,theme.accent);
        for(const auto& row:layout->rows){
            const auto rect=row.bounds;
            canvas.Round(rect,5,hoveredTarget_==row.id?theme.hover:theme.background);
            canvas.Text(row.title,canvas.body,{rect.left+8,rect.top+5,rect.right-8,rect.top+30},theme.accent);
            canvas.Text(row.source,canvas.smallFormat,{rect.left+8,rect.top+32,rect.right-8,rect.bottom-4},theme.secondaryText);
        }
        canvas.target.PopAxisAlignedClip();
    }
}

} // namespace noven::ui
