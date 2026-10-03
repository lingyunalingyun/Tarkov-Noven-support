#include "events/TarkovChangesEnricher.h"
#include "events/EventHtml.h"
#include <algorithm>
#include <array>
#include <iomanip>
#include <regex>
#include <sstream>

namespace noven::events {
namespace {
bool Id(std::string_view s){return !s.empty() && s.size()<=20 && s.front()!='0' && s.find_first_not_of("0123456789")==s.npos;}
std::string Url(std::string_view id){return "https://changes.tarkov-changes.com/view/"+std::string(id);}
std::optional<Timestamp> Date(std::string_view text) {
    static const std::regex pattern(R"(^Dated: [A-Za-z]+, ([0-9]{2}) ([A-Za-z]+) ([0-9]{4}) - ([0-9]{2}):([0-9]{2}) (AM|PM) (EDT|EST) \| Game Version )");
    std::match_results<std::string_view::const_iterator> match;
    if(!std::regex_search(text.begin(),text.end(),match,pattern))return {};
    constexpr std::array<std::string_view,12> months{"January","February","March","April","May","June","July","August","September","October","November","December"};
    const auto it=std::ranges::find(months,match[2].str());if(it==months.end())return {};
    int hour=std::stoi(match[4].str());if(hour<1 || hour>12)return {};hour=hour%12+(match[6].str()=="PM"?12:0);
    std::ostringstream iso;iso<<match[3].str()<<'-'<<std::setfill('0')<<std::setw(2)<<(it-months.begin()+1)<<'-'<<match[1].str()
        <<'T'<<std::setw(2)<<hour<<':'<<match[5].str()<<":00"<<(match[7].str()=="EDT"?"-04:00":"-05:00");
    return ParseTimestamp(iso.str());
}
}
bool TarkovChangesEnricher::Parse(const HttpResponse& response,std::string_view id,ChangeRecord& record,std::string& error) {
    if(!Id(id)){error="invalid change source identity";return false;}
    if(!ValidateHtml(response,error))return false;
    try {
        const std::string_view page=response.body;
        if(page.find("<meta property=\"og:site_name\" content=\"Tarkov Silent Changes\">")==page.npos)
            throw std::runtime_error("change source identity missing");
        ChangeRecord next;next.sourceRecordId=id;next.sourceUrl=Url(id);
        const auto date=page.find("<h2>Dated: "),dateEnd=page.find("</h2>",date);
        if(date==page.npos || dateEnd==page.npos)throw std::runtime_error("change publication structure missing");
        next.publishedAt=Date(html::Text(page.substr(date+4,dateEnd-date-4)));
        if(!next.publishedAt)throw std::runtime_error("change date contract unsupported");
        std::string file,pendingOld,currentKey;std::vector<std::pair<std::size_t,std::string>> path;
        std::size_t cursor=dateEnd;bool awaitingNew=false;
        while(cursor<page.size()) {
            const auto header=page.find("<h3>",cursor),line=page.find("<div class=\"diff-line",cursor);
            if(header==page.npos && line==page.npos)break;
            if(header<line) {
                const auto end=page.find("</h3>",header);if(end==page.npos)throw std::runtime_error("broken change file header");
                file=html::Text(page.substr(header+4,end-header-4));path.clear();cursor=end+5;continue;
            }
            const auto begin=page.find('>',line),end=page.find("</div>",begin);
            if(begin==page.npos || end==page.npos || file.empty())throw std::runtime_error("broken change line");
            auto raw=page.substr(begin+1,end-begin-1);const auto text=html::Text(raw);
            if(text.starts_with("['") && text.ends_with("']")) {
                if(awaitingNew)throw std::runtime_error("missing changed value");
                const auto indentation=raw.find_first_not_of(' ');
                while(!path.empty() && path.back().first>=indentation)path.pop_back();
                path.emplace_back(indentation,text.substr(2,text.size()-4));
                currentKey=file;for(const auto& part:path)currentKey+="/"+part.second;
            }else if(text.starts_with("-")) {
                const auto old=text.find("(Old) ");if(old==text.npos || awaitingNew || currentKey.empty())throw std::runtime_error("unsupported old change value");
                pendingOld=text.substr(old+6);awaitingNew=true;
            }else if(text.starts_with("+")) {
                const auto value=text.find("(New) ");if(value==text.npos || !awaitingNew)throw std::runtime_error("unsupported new change value");
                auto newValue=text.substr(value+6);const auto percent=newValue.find(" (");if(percent!=newValue.npos)newValue.resize(percent);
                if(currentKey.size()>1024 || pendingOld.size()>1024 || newValue.size()>1024 || next.changes.size()>=16)
                    throw std::runtime_error("change evidence exceeds capacity");
                next.changes.push_back({currentKey,pendingOld,std::move(newValue)});awaitingNew=false;
            }else if(!text.empty())throw std::runtime_error("unsupported diff syntax");
            cursor=end+6;
        }
        if(next.changes.empty() || awaitingNew)throw std::runtime_error("incomplete change evidence");
        record=std::move(next);return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
bool TarkovChangesEnricher::Fetch(std::string_view id,ChangeRecord& record,std::string& error) {
    if(!Id(id)){error="invalid change request identity";return false;}
    return Parse(http_.Get(L"changes.tarkov-changes.com",L"/view/"+std::wstring(id.begin(),id.end())),id,record,error);
}
bool TarkovChangesEnricher::Attach(EventCatalog& catalog,std::string_view id,const ChangeRecord& record,std::string& error) {
    const auto* event=catalog.FindEvent(id);bool explicitLink=false;
    if(event)for(const auto& e:event->sourceEvidence)if(e.sourceKind==SourceKind::OfficialTelegram
        && std::ranges::find(e.linkedChangeRecordIds,record.sourceRecordId)!=e.linkedChangeRecordIds.end())explicitLink=true;
    if(!explicitLink || !Id(record.sourceRecordId) || record.sourceUrl!=Url(record.sourceRecordId)
        || record.changes.empty() || record.changes.size()>16){error="change evidence lacks explicit official link";return false;}
    // 整批校验后发布；即使内容看似活动参数，也不能生成活动或改变其官方时间/状态。
    // Publish after batch validation; even event-like parameters cannot create events or alter official timing/status.
    auto next=catalog;for(const auto& change:record.changes) {
        EventEvidence evidence;evidence.sourceKind=SourceKind::TarkovChanges;evidence.type=EvidenceType::ConfigurationChange;
        evidence.sourceRecordId=record.sourceRecordId;evidence.sourceUrl=record.sourceUrl;evidence.publishedAt=record.publishedAt;
        // key 本身是稳定变更身份，避免数组下标身份；有长度上限。
        // The key is the stable change identity, not an array index; size remains bounded.
        evidence.evidenceId="changes:"+record.sourceRecordId+":"+change.key;
        evidence.changedKey=change.key;evidence.oldValue=change.oldValue;evidence.newValue=change.newValue;
        if(!next.AttachEvidence(id,std::move(evidence),error))return false;
    }catalog=std::move(next);return true;
}
}
