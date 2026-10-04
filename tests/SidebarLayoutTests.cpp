#include "ui/BuiltinPages.h"
#include "ui/SidebarLayout.h"
#include <cstdlib>
#include <iostream>
using namespace noven::ui;
void Check(bool ok,const char* text){if(!ok){std::cerr<<text<<'\n';std::exit(1);}}
int main(){
    auto registry=MakeBuiltinPageRegistry();UiTheme theme;
    const auto layout=BuildSidebarLayout(registry,720,theme);
    const float accepted[]{186,229,272,315,358,439,482,525,568,663};
    Check(layout.rows.size()==10,"all rows computed");
    for(std::size_t i=0;i<layout.rows.size();++i){const auto& row=layout.rows[i];
        Check(row.visible&&std::abs(row.rect.top-accepted[i])<.01F&&row.rect.bottom>row.rect.top,"accepted geometry parity");
        Check(layout.HitTest((row.rect.left+row.rect.right)/2,(row.rect.top+row.rect.bottom)/2)==row.page.id,"shared exact hit geometry");
    }
    Check(layout.rows.back().page.id==BuiltinPageId::Settings&&layout.rows.back().rect.bottom==703,"bottom anchor");
    const PageId synthetic{"plugin.com.example.loot-route"};
    Check(registry.Register({synthetic,PageSection::Secondary,"nav.events","desc.events",PageIcon::GenericPlugin,-1,PageSource::Plugin}),"synthetic metadata only");
    const auto expanded=BuildSidebarLayout(registry,900,theme);
    Check(expanded.Find(synthetic)->rect.top==439&&expanded.Find(BuiltinPageId::RaidHistory)->rect.top==482,"registration shifts geometry");
    Check(expanded.HitTest(30,450)==synthetic,"new ID navigable without enum");
    Check(registry.Unregister(synthetic),"remove synthetic");
    Check(BuildSidebarLayout(registry,720,theme).Find(BuiltinPageId::RaidHistory)->rect.top==439,"removal restores layout");
    for(float height:{0.0F,20.0F,200.0F,400.0F,620.0F,720.0F}) {
        const auto compact=BuildSidebarLayout(registry,height,theme);
        for(const auto& row:compact.rows) {
            Check(std::isfinite(row.rect.top)&&row.rect.bottom>=row.rect.top&&row.rect.right>=row.rect.left,"bounded small-height geometry");
            if(row.visible){Check(compact.HitTest(30,(row.rect.top+row.rect.bottom)/2)==row.page.id,"small-height exact hits");
                if(row.page.section!=PageSection::Bottom)Check(row.rect.bottom<=compact.Find(BuiltinPageId::Settings)->rect.top-14,"visible rows never overlap bottom section");}
        }
    }
    std::cout<<"Registry-driven sidebar geometry PASS\n";
}
