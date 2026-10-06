#include "ui/BuiltinPages.h"
#include "ui/SidebarLayout.h"
#include <cstdlib>
#include <iostream>
using namespace noven::ui;
void Check(bool ok,const char* text){if(!ok){std::cerr<<text<<'\n';std::exit(1);}}
int main(){
    auto registry=MakeBuiltinPageRegistry();UiTheme theme;
    const auto layout=BuildSidebarLayout(registry,720,theme);
    const float accepted[]{186,229,272,315,358,439,482,525,568,620,663};
    Check(layout.rows.size()==11,"all rows computed");
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
            if(row.visible){const float top=row.page.section==PageSection::Bottom?row.rect.top:(std::max)(row.rect.top,compact.navigationViewport.top);
                const float bottom=row.page.section==PageSection::Bottom?row.rect.bottom:(std::min)(row.rect.bottom,compact.navigationViewport.bottom);
                Check(compact.HitTest(30,(top+bottom)/2)==row.page.id,"small-height exact clipped hits");}
        }
    }
    for(int i=0;i<24;++i)Check(registry.Register({PageId{"plugin.com.example.test.page-"+std::to_string(i)},PageSection::Secondary,"","",PageIcon::GenericPlugin,1000,PageSource::Plugin,UiExtensionPolicy::Extensible,"Hello Plugin"}),"runtime growth");
    for(float height:{360.0F,700.0F,900.0F}){
        const auto start=BuildSidebarLayout(registry,height,theme);Check(start.maximum>0,"overflow has bounded scroll range");
        for(const auto& page:registry.Pages())if(page.source==PageSource::Plugin){const auto row=start.Find(page.id);
            const auto scrolled=BuildSidebarLayout(registry,height,theme,row->rect.bottom-start.navigationViewport.bottom);
            const auto reached=scrolled.Find(page.id);Check(reached->visible&&reached->rect.top>=scrolled.navigationViewport.top&&reached->rect.bottom<=scrolled.navigationViewport.bottom,"every plugin is reachable");
            Check(scrolled.HitTest(30,(reached->rect.top+reached->rect.bottom)/2)==page.id,"scroll draw geometry is exact hit geometry");
            for(const auto id:{BuiltinPageId::Plugins,BuiltinPageId::Settings})Check(scrolled.Find(id)->rect.top==start.Find(id)->rect.top,"management anchors never scroll");
        }
        const auto end=BuildSidebarLayout(registry,height,theme,start.maximum);
        Check(!end.HitTest(30,159)&&!end.HitTest(30,end.navigationViewport.bottom+2),"clipped content has no invisible clickable rows");
    }
    std::cout<<"Registry-driven sidebar geometry PASS\n";
}
