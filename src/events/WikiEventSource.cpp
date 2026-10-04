#include "events/WikiEventSource.h"
#include "events/EventHtml.h"
#include "raid/RaidJson.h"
#include <algorithm>
#include <cctype>
#include <set>

namespace noven::events {
namespace {
std::string_view Trim(std::string_view s) {
    const auto a=s.find_first_not_of(" \t\r\n");
    return a==s.npos?std::string_view{}:s.substr(a,s.find_last_not_of(" \t\r\n")-a+1);
}
std::vector<std::string_view> Lines(std::string_view text) {
    std::vector<std::string_view> lines;
    while(!text.empty()) {auto end=text.find('\n');lines.push_back(Trim(text.substr(0,end)));
        if(end==text.npos)break;text.remove_prefix(end+1);}
    return lines;
}
std::string Plain(std::string_view text) {
    std::string out;
    while(!text.empty()) {
        const auto start=text.find("[[");out+=text.substr(0,start);if(start==text.npos)break;
        text.remove_prefix(start+2);const auto end=text.find("]]");
        if(end==text.npos)throw std::runtime_error("broken wiki link");
        auto link=text.substr(0,end);const auto pipe=link.rfind('|');
        if(pipe!=link.npos)link.remove_prefix(pipe+1);
        if(link.starts_with("File:") || link.starts_with("Image:"))throw std::runtime_error("unexpected wiki media");
        out+=link;text.remove_prefix(end+2);
    }
    if(out.find("{{")!=out.npos)throw std::runtime_error("unsupported wiki template");
    return html::Text(out);
}
std::string Anchor(std::string_view heading) {
    std::string out;constexpr char hex[]="0123456789ABCDEF";
    for(unsigned char c:heading) {
        if(c==' ')out+='_';
        else if((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_' || c=='-' || c=='.')out+=c;
        else {out+='%';out+=hex[c>>4];out+=hex[c&15];}
    }return out;
}
struct Section {std::string heading;std::string_view text;};
std::vector<Section> Sections(std::string_view article) {
    std::vector<Section> result;std::size_t cursor{};
    while(cursor<article.size()) {
        const auto end=article.find('\n',cursor);auto line=Trim(article.substr(cursor,end==article.npos?article.size()-cursor:end-cursor));
        if(line.starts_with("==") && !line.starts_with("===") && line.ends_with("==") && line.size()>4) {
            if(!result.empty())result.back().text=article.substr(result.back().text.data()-article.data(),cursor-(result.back().text.data()-article.data()));
            result.push_back({std::string(Trim(line.substr(2,line.size()-4))),article.substr(end==article.npos?article.size():end+1)});
        }if(end==article.npos)break;cursor=end+1;
    }return result;
}
}
CommunitySourceResult WikiEventSource::Parse(const HttpResponse& response) {
    CommunitySourceResult result;
    try {
        auto type=response.contentType;std::ranges::transform(type,type.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
        if(!response.error.empty())throw std::runtime_error(response.error);
        if(response.status!=200 || (!type.starts_with("application/json;") && type!="application/json")
            || response.body.empty() || response.body.size()>kMaximumResponseBytes)throw std::runtime_error("invalid wiki response");
        const auto root=raid::json::Parser(response.body).Parse();
        const auto& pages=root.At("query").At("pages").Array();if(pages.size()!=2)throw std::runtime_error("wiki page contract changed");
        std::string current,article,revision;std::optional<Timestamp> revised;std::set<std::int64_t> ids;
        for(const auto& page:pages) {
            const auto id=page.At("pageid").Int();const auto& title=page.At("title").String();
            if(!ids.insert(id).second || !((id==3552 && title=="Escape from Tarkov Wiki/Section 3") || (id==26936 && title=="Events")))
                throw std::runtime_error("wiki identity mismatch");
            const auto& revisions=page.At("revisions").Array();if(revisions.size()!=1)throw std::runtime_error("wiki revision missing");
            const auto& rev=revisions.front();if(rev.At("revid").Int()<=0)throw std::runtime_error("invalid wiki revision");
            const auto time=ParseTimestamp(rev.At("timestamp").String());if(!time)throw std::runtime_error("invalid wiki revision time");
            const auto& slot=rev.At("slots").At("main");
            if(slot.At("contentmodel").String()!="wikitext" || slot.At("contentformat").String()!="text/x-wiki")throw std::runtime_error("wiki format changed");
            if(id==3552)current=slot.At("content").String();
            else {article=slot.At("content").String();revision=std::to_string(rev.At("revid").Int());revised=time;}
        }
        // 当前 Tarkov 表格是状态证据；文章章节日期与修订时间都不是开始/结束时间。
        // The current Tarkov table supports status only; section dates and revision times are not start/end times.
        if(current.find("id=\"fpevents\"")==current.npos || current.find("Current Events")==current.npos)throw std::runtime_error("wiki current identity missing");
        const auto begin=current.find("====Tarkov===="),end=current.find("====Arena====",begin);
        if(begin==current.npos || end==current.npos)throw std::runtime_error("wiki current sections changed");
        const auto block=std::string_view(current).substr(begin,end-begin);
        if(Trim(block.substr(14))=="• Currently none") {result.success=true;return result;}
        const auto table=block.find("{|"),finish=block.find("|}",table);
        if(table==block.npos || finish==block.npos)throw std::runtime_error("wiki current table missing");
        std::vector<std::pair<std::string,std::string>> headers;std::vector<std::string_view> cells;bool descriptions=false;
        for(auto line:Lines(block.substr(table,finish-table))) {
            if(line=="|-"){if(descriptions)throw std::runtime_error("unexpected wiki rows");descriptions=true;continue;}
            if(!line.starts_with('|') || line.starts_with("{|"))continue;
            if(descriptions)cells.push_back(line.substr(1));
            else {
                if(!line.starts_with("|[[File:") || !line.ends_with("]]"))throw std::runtime_error("wiki header changed");
                const auto imageEnd=line.find('|',8),caption=line.rfind('|');
                if(imageEnd==line.npos || caption==imageEnd)throw std::runtime_error("wiki image identity missing");
                headers.emplace_back(line.substr(8,imageEnd-8),line.substr(caption+1,line.size()-caption-3));
            }
        }
        if(headers.empty() || headers.size()>16 || cells.size()!=headers.size())throw std::runtime_error("wiki table shape changed");
        const auto sections=Sections(article);std::set<std::string> seen;
        for(std::size_t i=0;i<headers.size();++i) {
            const Section* match{};const auto image="File:"+headers[i].first;
            for(const auto& section:sections)for(auto line:Lines(section.text))if(line==image) {
                if(match)throw std::runtime_error("ambiguous wiki section");match=&section;
            }
            if(!match)throw std::runtime_error("wiki current section unresolved");
            CommunityAnnouncement a;a.sourceRecordId="26936:"+Anchor(match->heading);a.sourceUrl=WikiEventUrl(a.sourceRecordId);
            if(a.sourceUrl.empty() || !seen.insert(a.sourceRecordId).second)throw std::runtime_error("invalid wiki event identity");
            a.sourceRevision=revision;a.revisionAt=revised;a.title=headers[i].second;a.summary=Plain(cells[i]);
            if(a.summary.find("Only available in the seasonal game mode")!=a.summary.npos)a.modes={EventMode::Seasonal};
            for(auto line:Lines(match->text))if(line.starts_with("* ")) {a.summary+='\n';a.summary+=Plain(line.substr(2));}
            if(a.summary.empty() || a.summary.size()>16384 || a.title.empty() || a.title.size()>2048)throw std::runtime_error("wiki event content exceeds capacity");
            // 只有明确原公告 URL 才允许关联；时间相近、标题相似均不是身份。
            // Only an explicit original-announcement URL permits correlation; timing/title similarity is not identity.
            constexpr std::string_view prefix="https://t.me/escapefromtarkovEN/";std::size_t p{};bool ambiguous=false;
            while((p=match->text.find(prefix,p))!=match->text.npos) {
                p+=prefix.size();const auto stop=match->text.find_first_not_of("0123456789",p);auto id=match->text.substr(p,stop==match->text.npos?match->text.size()-p:stop-p);
                if(!id.empty() && id.size()<=20 && id.front()!='0' && (stop==match->text.npos
                    || std::string_view(" \t\r\n]<").find(match->text[stop])!=std::string_view::npos)) {
                    if(a.officialRecordId && *a.officialRecordId!=id)ambiguous=true;else a.officialRecordId=id;
                }
            }if(ambiguous)a.officialRecordId.reset();
            result.announcements.push_back(std::move(a));
        }result.success=true;
    }catch(const std::exception& e){result.announcements.clear();result.error=e.what();}
    return result;
}
CommunitySourceResult WikiEventSource::Fetch(std::stop_token stop) {
    if(stop.stop_requested())return {false,{},"wiki refresh stopped"};
    return Parse(http_.Get(L"escapefromtarkov.fandom.com",kWikiEventPath));
}
}
