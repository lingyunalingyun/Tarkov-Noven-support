# Windows installation / Windows 安装

## Current production installer / 当前 Phase 12 安装器

唯一版本源仍为 CMake `project VERSION`（当前 0.1.0）。所有四个 EXE 的
VERSIONINFO、Setup 版本/文件名及初始目录均由此派生。普通入口仅为 Launcher：

```text
%LOCALAPPDATA%/Programs/Noven Tarkov Support/
  NovenLauncher.exe
  NovenUpdater.exe
  current.json
  initial.json / last-good.json
  installer-releases/0.1.0.json
  versions/0.1.0/
    NovenTarkovSupport.exe
    NovenPluginHost.exe
    onnxruntime.dll
    assets/models/  assets/data/  assets/i18n/
    licenses/  docs/  NOTICE.md  noven-installed.layout
```

Start Menu、可选桌面快捷方式及安装后启动都指向 `NovenLauncher.exe`，
Launcher 只启动可信版本目录；Host 从该活动版本自身目录解析。
用户可变数据全部留在 `%LOCALAPPDATA%/Noven Tarkov Support/`。
Core 不含可选地图、Review 夹具、测试、Demo DLL 或私钥。
生产更新和资源下载端点均未配置；可选资源首次引导可选“稍后”，
Settings 资源管理及缺失地图提示仍可用，下载按钮不会假装可下载。

安装器将 Core 解包到固定 `installer-staging`，调用真实 Updater
`--install-bundle` 验证完整文件清单、大小、SHA-256，再提交版本目录和本地
安装收据。安装中断日志可恢复旧目录/收据；不从网络建立初始信任。
同版本修复可恢复缺失/损坏 Core，不重置 `current.json` 或用户状态。
较新 bundled Core 按现有 VersionStore 规则激活，保留上一版本并等待健康确认。
旧 Core 安装器不切换较新的活动版本；如果已装 bootstrap 版本更高，则在复制前
拒绝旧 Setup，要求使用相应新安装器修复。平铺 Phase 10 Main/Host 保留为恢复文件，
直到原安装清单正常卸载；它们不再是快捷方式入口。
新降级保护仅存在于 Phase 12 及后续安装器；已生成的 Phase 10/11 开发 Setup
不能追溯补丁，不应拿它们修复或回退较新的安装。

固定 AppId `70C934D2-C53B-4F49-A8C7-152E748A8E54` 不变。普通安装为当前用户，
不提权；微软 VC++ prerequisite 独立提示/同意后可能需要管理员。
所有运行标记要求正常关闭，不默认强杀。默认卸载只清理受信任 Program 版本、
bootstrap、快捷方式及注册项，保留整个 User Data（含下载资源）。
异常/未知 Program 清单会拒绝破坏性清理，需用原安装器修复后再卸载。

打包仍为 Release → clean CMake install → payload audit → 可选签四个 EXE →
本地 inventory → ISCC → 可选签 Setup → SHA-256。没有虚构 Publisher 或签名。
Authenticode 未配置时 SmartScreen 可能警告；更新清单签名不等于 Authenticode。
Launcher/Updater 是安装器所有的 bootstrap，应用差分更新只管理 versions 内容。

人工安装验收须用户执行，自动测试或 Review 更新通过不能替代：

1. 干净测试用户安装 0.1.0，检查无普通 UAC、Start Menu → Launcher → versioned Main。
2. 检查 OCR/F2、插件、首次资源“稍后”、Settings 及缺失地图状态；产生真实状态并备份。
3. 关闭应用，同版本 Setup 修复后确认状态/授权/资源不变。
4. 使用独立源码副本中唯一 CMake VERSION 改为 0.1.1 的受控安装器，保持同一 AppId，
   手工升级并确认版本/用户状态；不要发布此测试制品或改主仓库版本。
5. 运行旧 0.1.0 Setup，确认不静默降级（新版 bootstrap 会拒绝旧 Setup）。
6. 关闭、卸载，确认 Program/快捷方式消失、User Data 保留；用新 Setup 重装并确认原状态。

The current production installer uses the versioned layout and trusted local
bundle transaction above. It preserves user data and existing activation on
same/older-Core repair, blocks bootstrap downgrade, and retains the previous
trusted version on a newer bundled-Core activation. No optional maps or
production network source are enabled. Manual installer lifecycle acceptance
is still a separate user-run check; do not automatically install Setup.

## Historical Phase 10 notes / 历史 Phase 10 记录

以下无 Updater 等描述仅记录当时边界；当前行为以以上 Phase 12 与
[APP_UPDATES.md](APP_UPDATES.md) 为准，历史 AV 事件仍保留。

Phase 10 使用 Inno Setup EXE；没有 MSI、自动更新、Updater、包下载或 Muse。唯一版本源是 CMake `project(... VERSION ...)`；主程序标题、两个 EXE VERSIONINFO、安装器和输出文件名均由它派生。没有已有 ICO 素材，保留当前窗口品牌绘制，不虚构发布者或签名。
Phase 10 uses an Inno Setup EXE, not MSI/auto-update/Updater/package downloads/Muse. CMake project VERSION is the sole version source for the app title, both executables' VERSIONINFO, Setup and artifact filename. There is no existing ICO asset; retain current window branding without inventing a publisher/signature.

默认当前用户程序目录：`%LOCALAPPDATA%\Programs\Noven Tarkov Support\`。普通安装不请求管理员；缺少 VC++ Runtime 时，独立微软 prerequisite 需明确同意并可能要求管理员。安装器只使用有效 Microsoft 签名的 vc_redist.x64.exe，不复制零散 CRT DLL，不改 /MD 为 /MT。
Default per-user program directory: `%LOCALAPPDATA%\Programs\Noven Tarkov Support\`. Normal installation does not request admin rights. A missing VC++ Runtime is a separate explicitly approved Microsoft prerequisite and may require administrator rights. Packaging requires an authentic Microsoft-signed vc_redist.x64.exe; no loose CRT DLLs or /MD-to-/MT workaround.

用户根：`%LOCALAPPDATA%\Noven Tarkov Support\`。插件在 `plugins/`；设置、历史、插件状态/存储等在 `data/`；日志在 `logs/`；可选官方资源在 `resources/`，临时下载在 `downloads/cache/`。正常 Core 程序目录只安装主 EXE、Host、ONNX Runtime、OCR 模型/字典、语言、静态目录及版权通知，不包含可选地图图片或缩放包。
User root: `%LOCALAPPDATA%\Noven Tarkov Support\`. Plugins use plugins/, settings/history/plugin state/storage use data/, logs use logs/, optional resources use resources/, and temporary downloads use downloads/cache/. Normal Core payload contains Main/Host, ONNX Runtime, OCR models/dictionary, locales, catalogs and notices, but no optional map imagery or zoom packs.

AppPaths 用 install() 专属 noven-installed.layout 标记识别 Installed Mode；Known Folder 失败时停止启动，不回退到 CWD/程序目录写数据。无标记的 Development/Review 保留 EXE 相邻 data/plugins/debug-captures。测试显式注入临时 Test Root，不查询真实用户目录。不会自动搬迁/删除旧开发目录数据。
AppPaths recognizes Installed Mode through the install-only noven-installed.layout marker. Known Folder failures stop startup rather than writing into CWD/program files. Marker-free Development/Review retains exe-relative data/plugins/debug-captures. Tests inject temporary roots without real user-data writes. No automatic migration/deletion of old development data.

固定 AppId：`70C934D2-C53B-4F49-A8C7-152E748A8E54`，后续版本必须复用。App/Host 持有稳定运行标记，交互安装要求正常关闭，不强杀。升级只替换安装清单中的 Program 文件，不修改插件授权/启用意图。默认卸载删除 Program、快捷方式和卸载项，保留整个 User Root；重新安装会重读原数据。没有“删除所有数据”选项或隐藏清理。
Stable AppId: 70C934D2-C53B-4F49-A8C7-152E748A8E54; future releases must reuse it. Stable App/Host mutexes require normal shutdown without forced termination. Upgrade only replaces installed program files, never grants/enabled intent. Uninstall removes program/shortcuts/uninstall entry but retains the entire user root; reinstall reads it again. No delete-all-data option or hidden cleanup.

## Packaging

使用 MSVC x64 Release；先跑完整测试，再执行（工具路径按本机提供）：
Use MSVC x64 Release and complete tests first, then invoke with your local tool paths:

```powershell
./scripts/package_release.ps1 -BuildDirectory E:/build/Release -OutputDirectory E:/release/Noven `
  -VcRedist 'C:/path/to/Microsoft/vc_redist.x64.exe' -CMake cmake -ISCC ISCC
```

脚本只清空精确的 Output/runtime-staging，构建两个目标、CMake install、Core 白名单审计、ISCC，再输出大小/SHA-256。禁止 Review、测试/Demo DLL、PDB、源码、缓存、用户数据及 assets/maps。地图来源/许可文本保留在 docs/maps，静态目录表保留。不能复制整个 Release 文件夹。
The script clears only Output/runtime-staging, builds Main/Host, runs CMake install, Core audit and ISCC, then reports size/SHA-256. Review/test/demo/build/cache/user artifacts and assets/maps are rejected. Map provenance/license text is retained in docs/maps; static catalogs remain. Never copy the entire Release directory.

当前 ONNX Runtime 为 1.30.0.20260909.8.f2c39fe；`licenses/onnxruntime` 打包对应固定提交的 MIT 和完整 ThirdPartyNotices。OCR 模型附 Apache-2.0 原文和精确来源/哈希；完整组件清单及未解决的地图授权见 [licenses/README.md](licenses/README.md)。补齐法律文件不等于所有地图获准公开再分发；本地安装验收不能替代授权审定。
ONNX Runtime 1.30.0.20260909.8.f2c39fe ships its matching fixed-commit MIT and complete notices under licenses/onnxruntime. OCR models include Apache-2.0 and precise source/hash attribution. The inventory records unresolved map rights; notice inclusion and local installation testing are not blanket redistribution clearance.

可选 -SigningScript 接受单个 artifact 路径，依次签主 EXE、Host、Setup，并检查有效签名。证书、PFX、密码和私钥必须由外部发布环境管理，不入库。未签名版本可能出现 SmartScreen 警告；SHA-256 只是制品信息，不是 Signed Update Trust。
Optional SigningScript receives one artifact path and signs main/Host/Setup with valid-signature checks. Certificates/PFX/passwords/private keys belong to external release infrastructure, never this repository. Unsigned builds may show SmartScreen warnings. SHA-256 is artifact information, not signed-update trust.

## Manual acceptance

自动测试不是人工安装验收。请实际执行：全新安装（检查当前用户、Start Menu、OCR 和插件）→ 产生真实用户数据 → 正常关闭 → 下一版本 Setup 覆盖升级（检查版本和所有数据/原授权保留）→ 卸载（Program 消失、User Root 保留）→ 再安装（数据恢复可见）。运行中安装也应要求正常关闭。未经用户确认，不标记人工 PASS；不要使用自动化测试向真实 LocalAppData 灌入夹具。
Automated tests are not manual acceptance. Actually install, verify per-user/Start Menu/OCR/plugins, create real user data, close normally, upgrade with the next-version Setup and verify version/data/grants, uninstall and verify Program removal/User Root retention, reinstall and verify data. Running apps must require normal closure. Manual PASS requires user confirmation; tests must not inject fixtures into real LocalAppData.

### Exact acceptance sequence / 实际验收步骤

仅供用户本机功能验收，未获地图授权前不公开分发。先备份已有 User Root；如果已有真实数据，不删除它来伪装全新安装，改用干净的 Windows 测试用户。
For user-side functional testing only, not public distribution before map clearance. Back up an existing User Root; use a clean Windows test account rather than deleting real data to simulate a fresh install.

1. **A–C**：手工运行 `NovenTarkovSupport-Setup-0.1.0.exe`，检查默认当前用户目录及普通安装无 UAC（独立 VC prerequisite 除外）；从 Start Menu 启动，核对标题/EXE ProductVersion 为 0.1.0。
2. **D–F**：手工 F2 验证 Scanner/OCR，检查 Plugin Center 和真实用户插件；保存 settings、扫描、对局历史、插件启用/授权及 storage（有对应数据时检查 Events/Marketplace cache）。确认它们在 `%LOCALAPPDATA%\Noven Tarkov Support\` 而非 Program Root；记录预期值并备份。
3. **G–H**：正常关闭 Noven 及其 Host。为受控升级测试，从同一提交导出一个独立、非发布用源码副本，只将该副本 CMake `project VERSION` 改为 0.1.1；不要修改 AppId 或目录规则。使用独立 Release build 和输出目录，按上述脚本构建/审计 `Setup-0.1.1.exe`，手工覆盖安装到同一个目录。测试副本/制品不发布、不入库；不在主仓库建立第二版本源。保留 0.1.0 Setup 用于初装，0.1.1 Setup 用于升级和重装。
4. **I**：再次从 Start Menu 启动，版本须为 0.1.1；核对先前数据、授权、启用意图及 storage 不变，不再次 Grant/Enable 来掩盖丢失；地图显示/放大正常。
5. **J–L**：正常关闭，从 Windows Installed apps/Programs and Features 手工卸载；确认 Program Root 与快捷方式/卸载项移除，User Root 和已记录数据仍在。不手工清理 User Root。
6. **M–N**：用同一 0.1.1 Setup 重装，Start Menu 启动后核对原数据重新可见，原授权/状态保留。记录每步实际结果；未执行步骤不记 PASS。

The controlled upgrade uses an isolated source copy with only its single CMake product version changed to 0.1.1, the same AppId, independent build/output folders, and no automatic installation. Verify new version, unchanged state/grants, uninstall retention and reinstall visibility. No secondary version source, updater, or published test release is introduced.

## Historical full-map payload dependency boundary / 原完整地图安装依赖记录

Phase 10B supersedes the full-map installation rules below. Normal Core excludes all optional map content; Installed Mode does not run legacy map recomposition/update networking. ResourceService supplies verified user-root generations, and missing maps show a native unavailable state. Production delivery stays unconfigured pending approved origin and redistribution rights. The following source/runtime findings are retained as historical provenance, not current Core install requirements. See [RESOURCE_DELIVERY.md](RESOURCE_DELIVERY.md) for the offline review fixture and first-run/Settings flow. No application updater is implemented.

| Resource | Consumer | Production requirement |
| --- | --- | --- |
| Floor preview PNG, icon PNG | `MapPage` / `LocalImage`, catalog `map_floors.tsv` | Direct display; keep all existing previews/icons and both PvP/PvE references. |
| `.tiles` packs | `LocalImage` zoom rendering | Prebuilt full-resolution blocks; no source-tile regeneration needed to zoom. |
| `maps/sources/*.png` referenced by `map_compositions.tsv` / `map_update_assets.tsv` | `MapAssetUpdater` → `MapAssetComposer` → `MapAssetStore` | Runtime update recomposition uses updated user-cache sources with installed sources as fallback. Keep this dependency closure. |
| Interchange `composition_sources` | Composition recipes | Bounded derived SVG-overlay tiles supplement the selected satellite inputs; keep them. They do not replace all raw satellite sources. |
| Raw source PNG referenced only by provenance/generator | `generate_all_images.py` build/reuse inputs; `all.manifest.json` ledger | Not a runtime display or recomposition input; exclude from install, preserve repository source and provenance. |
| TSV/JSON, SOURCE/NOTICE/license records | Catalog, update recipes, attribution | Keep metadata and source attribution; a ledger reference alone does not make the raw input a runtime dependency. |

现有管线在首次显示使用已打包预览/tiles；启动更新有真实变化才重拼受影响输出，不因 cache 缺失而重建所有地图。因此不能简单只删源图来保持已有运行时更新：部分源图仍是重拼 fallback。构建期产物-only 的可选未来方案可省下全部源 PNG，但须另行处理现有地图更新兼容性，本任务不改写地图架构。
Initial display uses packaged previews/packs. Startup updates recompose affected outputs only after actual source changes, not merely because a cache is absent. Removing all raw inputs would break recomposition fallback. A future prebuilt-only model could omit all raw PNGs but must address existing map-update compatibility; no such architecture rewrite is included here.

安装选择从生产 TSV 的完整路径字段确定并排序/去重；CMake 缺失输入失败，payload 审计拒绝多余 source PNG、缺失重拼输入/预览/zoom pack。来源账本全部保留。字节相同的透明 PNG 仍保留逻辑路径，不改为硬链接/符号链接。
Install selection is deterministic from whole production-TSV path fields. CMake fails on missing inputs; payload audit rejects surplus source PNGs and missing composition/preview/zoom dependencies. Provenance remains intact. Transparent duplicate tiles retain logical paths, without hardlinks/symlinks.

当前资产快照中，未引用的 962 个源 PNG 包含 928 个 Interchange zoom-5 输入和 34 个已转换图标的原图。Interchange manifest 明确记录 zoom-5 缺失内部瓦片 `main/5/32/3.png`，实际选用 zoom-4；生成器保留已取得的高层输入及来源账本，但生产配方/更新表不引用它们。图标通过已生成的 `maps/icons/*.png` 读取，不读取 URL-hash 原图。未来重新生成若选用新输入，安装选择跟随实际生产表变化，不硬编码排除这 962 个路径。
The current snapshot's 962 unused raw PNGs are 928 Interchange zoom-5 inputs and 34 originals of converted icons. The Interchange manifest records a missing interior zoom-5 tile (main/5/32/3.png) and selected zoom 4. The generator retains successful higher-level inputs/provenance, but production recipes/update tables do not use them. Icons load generated maps/icons files, not URL-hash originals. Future generation selects from actual production tables rather than hardcoding these 962 exclusions.

逻辑分类仅用于资产审查：`core`（EXE/运行 DLL/许可）、`ocr`（两个模型/字典）、`catalog`（生成目录）、`localization`（语言）、`maps.<stableMapId>`（通过 MapCatalog floors/references 归属）、`maps.shared`（多地图共用源图/图标/来源账本）。别名使用已有稳定身份，可一文件多归属；不靠显示名猜身份。这不是更新清单或更新器。
Logical review groups are core, OCR, catalog, localization, stable-map-ID components and shared map inputs/provenance. Alias ownership can be multi-valued and comes from existing catalog references, never display names. This is not an update manifest or updater.

## Historical antivirus incident / 历史杀毒事件

2026-10-09，Kaspersky 曾检测并删除 Release 的测试专用 `NovenAppPathsTests.exe`：`VHO:Trojan-Banker.Win32.ClipBanker.gen`。该 EXE 从未进入安装清单。后续未修改源代码的 clean Release 重建/完整 CTest 为 90/90，通过时未观察到 Main/Host/Setup 检测；这不证明历史事件是误报，也不构成永久安全保证。保留原检测记录，不禁用保护、不加排除、不修改代码规避。如果出货制品出现检测，停止发布验收并报告。
Kaspersky historically detected/deleted the Release-only AppPaths test executable with the named heuristic. It was never shipped. A later unchanged-source clean rebuild passed 90/90; no shipped Main/Host/Setup detection was observed. Neither fact proves a false positive or permanent safety. Preserve history, keep protection active, add no exclusions, and stop acceptance if a shipped artifact is detected.
