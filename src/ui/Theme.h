#pragma once

#include <d2d1.h>

namespace noven::ui {

// 全部尺寸以 DIP 表示，由 Direct2D 和当前窗口 DPI 映射到物理像素。
// All sizes are DIPs; Direct2D and the window DPI map them to physical pixels.
struct UiTheme final {
    float sidebarWidth{256.0F};
    float contentPadding{48.0F};
    float navigationHeight{40.0F};
    float cornerRadius{10.0F};
    D2D1_COLOR_F background{0.070F, 0.066F, 0.060F, 1.0F};
    D2D1_COLOR_F sidebar{0.105F, 0.100F, 0.093F, 1.0F};
    D2D1_COLOR_F surface{0.125F, 0.120F, 0.110F, 1.0F};
    D2D1_COLOR_F selected{0.205F, 0.192F, 0.168F, 1.0F};
    D2D1_COLOR_F hover{0.158F, 0.150F, 0.137F, 1.0F};
    D2D1_COLOR_F accent{0.81F, 0.65F, 0.37F, 1.0F};
    D2D1_COLOR_F primaryText{0.94F, 0.92F, 0.87F, 1.0F};
    D2D1_COLOR_F secondaryText{0.62F, 0.61F, 0.56F, 1.0F};
    D2D1_COLOR_F divider{0.23F, 0.22F, 0.20F, 1.0F};
};

} // namespace noven::ui
