#include "events/EventCatalog.h"
#include <iostream>
#include <stdexcept>
using namespace noven::events;
void Check(bool ok) {if(!ok)throw std::runtime_error("event catalog assertion");}
int main() {
    try {
        Check(ParseTimestamp("2026-10-04T01:00:00+03:00")==ParseTimestamp("2026-10-03T22:00:00Z"));
        Check(!ParseTimestamp("2026-02-30T00:00:00Z"));Check(!ParseTimestamp("2026-10-04T00:00:00"));
        EventCatalog c;std::string error;
        OfficialAnnouncement a; a.sourceRecordId="10";a.sourceUrl="https://t.me/escapefromtarkovEN/10";
        a.title="An in-game event has started";a.summary=a.title;a.status=EventStatus::Active;
        a.publishedAt=ParseTimestamp("2026-10-03T22:00:00Z");
        Check(c.Apply({&a,1},error));Check(c.Apply({&a,1},error));Check(c.Events().size()==1);
        const auto id=c.Events()[0].eventId;Check(!c.Events()[0].startsAt && !c.Events()[0].endsAt);
        Check(c.ActiveEvents(*a.publishedAt).size()==1);
        auto update=a;update.sourceRecordId="11";update.sourceUrl="https://t.me/escapefromtarkovEN/11";
        update.updatesRecordId="10";update.publishedAt=*a.publishedAt+1;update.endsAt=*a.publishedAt+100;
        Check(c.Apply({&update,1},error));Check(c.Events().size()==1 && c.Events()[0].sourceEvidence.size()==2);
        Check(c.Events()[0].eventId==id && c.Events()[0].endsAt==update.endsAt);
        Check(StatusAt(c.Events()[0],*update.endsAt)==EventStatus::Ended);
        update.status=EventStatus::Ended;Check(c.Apply({&update,1},error));Check(c.Apply({&a,1},error));
        Check(c.Events()[0].sourceStatus==EventStatus::Ended);
        update.updatesRecordId="99";Check(c.Apply({&update,1},error));Check(c.UnresolvedUpdates().size()==1);
        auto invalid=a;invalid.sourceUrl="https://example.com/10";Check(!c.Apply({&invalid,1},error));
        Check(c.Events().size()==1);
        auto timed=c.Events()[0];timed.sourceStatus=EventStatus::Unknown;timed.startsAt=100;timed.endsAt=200;
        Check(StatusAt(timed,99)==EventStatus::Upcoming);Check(StatusAt(timed,100)==EventStatus::Active);
        Check(StatusAt(timed,200)==EventStatus::Ended);timed.endsAt.reset();Check(StatusAt(timed,1000)==EventStatus::Active);
        EventEvidence diff;diff.sourceKind=SourceKind::TarkovChanges;diff.type=EvidenceType::ConfigurationChange;
        diff.evidenceId="diff:1";diff.sourceRecordId="1";diff.sourceUrl="https://changes.tarkov-changes.com/view/1";
        EventCatalog empty;Check(!empty.AttachEvidence(id,diff,error));Check(empty.Events().empty());
        Check(c.AttachEvidence(id,diff,error));Check(c.Events().size()==1);
        std::cout<<"Event catalog contracts PASS\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
