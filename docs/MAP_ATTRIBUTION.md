# Map attribution evidence / 地图归属证据

核对日期：2026-10-02。范围为当前 `assets/maps/*/SOURCE.md` 的全部两份记录：`interchange/SOURCE.md`、`icons/SOURCE.md`，以及对应清单和上游官方文件。本文记录证据及缺口，不宣称已取得全部素材的发布授权。

## Asset boundaries / 素材边界

| 本地资源 | 来源及权利人 | 许可证据 / 状态 | 本地修改 |
| --- | --- | --- | --- |
| `interchange/Interchange.svg`、`Ground_Level` / `First_Floor` / `Second_Floor` 的 PNG 与 tiles | Shebuka；Tarkov.dev SVG maps | CC BY-NC-SA 4.0；上游另有禁止作弊用途声明 | 选取楼层组、栅格化、分块；未改变几何 |
| `First_Floor.overlay.*`、`Second_Floor.overlay.*` | 同一 Shebuka SVG | 同上；与卫星底图权利分开 | 仅导出对应上层，背景透明，无地面组 |
| `Satellite.png`、`Satellite.tiles` | Tarkov.dev 瓦片托管；游戏图像权利归 Battlestate Games 及各自权利人 | 未确认单独再分发授权及独立图像作者；不能套用 SVG CC 或仓库 MIT | zoom 4 瓦片拼接、旋转投影裁剪、预览及分块；缺失外缘透明 |
| `icons/*.png` 中仓库来源的 45 项 | the-hideout/tarkov-dev；仓库版权声明 Oskar Risberg，2019 | 仓库 MIT 原文须保留；不据此消除游戏图像或其他第三方权利 | RGBA PNG 转换及透明方形留白，未缩放重绘 |
| `icons/*.png` 中 handbook 来源的 18 项 | regular/items 分类元数据指定的 assets.tarkov.dev 图片；游戏及各自权利人 | URL/元数据证明来源，未证明单独的 MIT / CC 图像许可 | 同上 |

计数按 `icons/manifest.json` 中的输出条目统计，含共享源图的多个别名，不代表独立原图数量。SVG SHA-256：`31a44112cd5fe33484ce6be7b7dc80d13db6077cba2583a4425ed94c665f47c4`。图标来源提交：`ef62766bbd7ffb294c7184f9b8bfe8ed0f18320e`。完整逐文件 URL、输入/输出哈希、转换信息以清单为准。

## Official evidence / 官方证据

1. [Tarkov.dev maps.json](https://github.com/the-hideout/tarkov-dev/blob/main/src/data/maps.json)：Interchange 配置指定 Shebuka、SVG 地址和独立 `tilePath`。配置作者字段不单独证明卫星图作者或授权范围；本地快照为 `interchange/reference.json`。
2. [SVG maps LICENSE.md](https://github.com/the-hideout/tarkov-dev-svg-maps/blob/main/LICENSE.md)：已读取标准 CC BY-NC-SA 4.0 原文；[README](https://github.com/the-hideout/tarkov-dev-svg-maps#license--use-restrictions) 另外声明禁止作弊及不公平优势用途，并主张自动撤销许可。分开记录两者，不推断附加声明的法律效力。
3. [Creative Commons 正式条款](https://creativecommons.org/licenses/by-nc-sa/4.0/legalcode.en)：第 2、3 节规定非商业范围、署名及修改记录、适用的相同方式共享、不施加限制被许可权利的额外条件；第 5 节为免责。纯技术格式转换不自动成为改编；若有受版权保护的改编贡献，本项目对该 SVG 贡献采用相同 CC 许可。第三方未授予的权利及商标不因此取得授权。
4. [图标来源提交的 MIT LICENSE](https://github.com/the-hideout/tarkov-dev/blob/ef62766bbd7ffb294c7184f9b8bfe8ed0f18320e/LICENSE)：与本地 `assets/maps/icons/LICENSE.tarkov-dev.txt` 的 Oskar Risberg 2019 声明及 MIT 条款核对；该文件保留完整授权和免责原文。API 图片链接不等同于仓库软件授权。
5. [Battlestate Games 官网](https://www.battlestategames.com/) 确认 Escape from Tarkov 为其作品。尝试读取 [EFT 官方许可协议](https://www.escapefromtarkov.com/legals/license_agreement) 及旧版官方链接均失败，因此不引用未读取的条款，也未确认可再分发卫星或 handbook 图像的具体授权。继续保留游戏与各自权利人的版权、商标和其他权利；发布前补充权利人授权证据，或排除未获授权资源。

上游 `main` 链接为核对时的官方证据，可能变化；它们不代表原 SVG 下载时的固定提交。下载内容由现有清单哈希识别，不虚构未记录的许可版本、日期或授权。来源清单不替代许可。

## Integration and redistribution / 集成与再分发

设置页使用 `settings.attribution.title/body/link` 三键；`link` 是可翻译的链接标题，不是 URL。原生按钮打开软件目录的完整 `NOTICE.md`，构建同步打包通知及本文。按钮编译及自动回归覆盖与人工点击验收须分别记录。

`map.reference` 仅将 CC 署名限定为 SVG，以免在卫星视图中被理解为全部图像的统一许可。简短设置文案是入口，完整通知须随资源可访问；不能只显示作者名而丢失来源、许可、修改及免责信息。

公开分发时，保留 NOTICE、SOURCE、各类清单、MIT 原文及 CC 链接；保留原作和此前修改记录，不声称作者或游戏官方背书。判断 SVG 实际用途是否满足非商业及其他适用条件；免费或源码公开并非充分条件。卫星、handbook 及任何有未解决第三方权利的图片须独立核实授权或排除，不能由应用代码许可覆盖。新增地图、图标或重新生成资源时，应重新核对各自作者、来源、转换和许可范围。

## All-map extension / 全地图扩展

后续全地图管线的逐层来源及转换记录位于 `maps/all.manifest.json`、每层 `.manifest.json`。
默认配置署名：Shebuka（工厂、海关、森林、灯塔、海岸线、储备站、立交桥、街区、震中、终端），Tarkov.dev（实验室、迷宫），TarkovBOT.eu（破冰船）。只采默认配置，不把同地图的其他2D/3D布局作者误写为本次素材作者；别名复用图片不复用点位身份。布局作者字段依旧不等于卫星图片许可证明。新增 SVG 衍生图的裁剪、楼层组合、逻辑网格及栅格化过程在生成清单中记录；非 SVG 瓦片权利保留独立未知状态。
