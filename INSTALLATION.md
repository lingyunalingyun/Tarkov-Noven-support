# Windows installation / Windows 安装

Phase 10 使用 Inno Setup EXE；没有 MSI、自动更新、Updater、包下载或 Muse。唯一版本源是 CMake `project(... VERSION ...)`；主程序标题、两个 EXE VERSIONINFO、安装器和输出文件名均由它派生。没有已有 ICO 素材，保留当前窗口品牌绘制，不虚构发布者或签名。
Phase 10 uses an Inno Setup EXE, not MSI/auto-update/Updater/package downloads/Muse. CMake project VERSION is the sole version source for the app title, both executables' VERSIONINFO, Setup and artifact filename. There is no existing ICO asset; retain current window branding without inventing a publisher/signature.

默认当前用户程序目录：`%LOCALAPPDATA%\Programs\Noven Tarkov Support\`。普通安装不请求管理员；缺少 VC++ Runtime 时，独立微软 prerequisite 需明确同意并可能要求管理员。安装器只使用有效 Microsoft 签名的 vc_redist.x64.exe，不复制零散 CRT DLL，不改 /MD 为 /MT。
Default per-user program directory: `%LOCALAPPDATA%\Programs\Noven Tarkov Support\`. Normal installation does not request admin rights. A missing VC++ Runtime is a separate explicitly approved Microsoft prerequisite and may require administrator rights. Packaging requires an authentic Microsoft-signed vc_redist.x64.exe; no loose CRT DLLs or /MD-to-/MT workaround.

用户根：`%LOCALAPPDATA%\Noven Tarkov Support\`。插件在 `plugins/`，settings、plugin-state、plugin-storage、Recent Scans、Raid History、Events、Marketplace、物价/图像/地图缓存在 `data/`，诊断日志和扫描调试输出在 `logs/`。程序目录仅安装 Runtime：主 EXE、Host、ONNX Runtime、OCR 模型、语言、静态目录与地图、版权通知。
User root: `%LOCALAPPDATA%\Noven Tarkov Support\`. Plugins use plugins/; settings/plugin-state/plugin-storage/Recent Scans/Raid History/Events/Marketplace/economy/image/map caches use data/; diagnostic logs and scan debug output use logs/. Program payload contains executables, ONNX Runtime, OCR models, locales, static catalogs/maps and attribution only.

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

脚本只清空精确的 Output/runtime-staging，构建两个目标、cmake --install --component Runtime、白名单审计、ISCC，再输出字节大小/SHA-256 和 artifact.json。禁止 Review payload、测试/Demo DLL、PDB/OBJ/LIB、源码、缓存和用户数据。包含实际地图 .tiles、PNG/SVG/JSON、生成 TSV 及现有地图署名/许可说明，不能复制整个 Release 目录。
The script clears only Output/runtime-staging, builds both executables, runs cmake install Runtime, allowlist audit and ISCC, then emits bytes/SHA-256 and artifact.json. Review payloads/tests/demo DLLs/PDB/OBJ/LIB/source/cache/user data are excluded. Actual maps/catalogs and existing map attribution/license notices are included; the Release directory is never copied wholesale.

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

## Map payload dependency boundary / 地图安装依赖边界

| Resource | Consumer | Production requirement |
| --- | --- | --- |
| Floor preview PNG, icon PNG | `MapPage` / `LocalImage`, catalog `map_floors.tsv` | Direct display; keep all existing previews/icons and both PvP/PvE references. |
| `.tiles` packs | `LocalImage` zoom rendering | Prebuilt full-resolution blocks; no source-tile regeneration needed to zoom. |
| `maps/sources/*.png` referenced by `map_compositions.tsv` / `map_update_assets.tsv` | `MapAssetUpdater` → `MapAssetComposer` → `MapAssetStore` | Runtime update recomposition uses updated user-cache sources with installed sources as fallback. Keep this dependency closure. |
| Interchange `composition_sources` | Composition recipes | Cropped derived inputs replace raw atlas tiles for these recipes; keep them. |
| Raw source PNG referenced only by provenance/generator | `generate_all_images.py` build/reuse inputs; `all.manifest.json` ledger | Not a runtime display or recomposition input; exclude from install, preserve repository source and provenance. |
| TSV/JSON, SOURCE/NOTICE/license records | Catalog, update recipes, attribution | Keep metadata and source attribution; a ledger reference alone does not make the raw input a runtime dependency. |

现有管线在首次显示使用已打包预览/tiles；启动更新有真实变化才重拼受影响输出，不因 cache 缺失而重建所有地图。因此不能简单只删源图来保持已有运行时更新：部分源图仍是重拼 fallback。构建期产物-only 的可选未来方案可省下全部源 PNG，但须另行处理现有地图更新兼容性，本任务不改写地图架构。
Initial display uses packaged previews/packs. Startup updates recompose affected outputs only after actual source changes, not merely because a cache is absent. Removing all raw inputs would break recomposition fallback. A future prebuilt-only model could omit all raw PNGs but must address existing map-update compatibility; no such architecture rewrite is included here.

安装选择从生产 TSV 的完整路径字段确定并排序/去重；CMake 缺失输入失败，payload 审计拒绝多余 source PNG、缺失重拼输入/预览/zoom pack。来源账本全部保留。字节相同的透明 PNG 仍保留逻辑路径，不改为硬链接/符号链接。
Install selection is deterministic from whole production-TSV path fields. CMake fails on missing inputs; payload audit rejects surplus source PNGs and missing composition/preview/zoom dependencies. Provenance remains intact. Transparent duplicate tiles retain logical paths, without hardlinks/symlinks.

逻辑分类仅用于资产审查：`core`（EXE/运行 DLL/许可）、`ocr`（两个模型/字典）、`catalog`（生成目录）、`localization`（语言）、`maps.<stableMapId>`（通过 MapCatalog floors/references 归属）、`maps.shared`（多地图共用源图/图标/来源账本）。别名使用已有稳定身份，可一文件多归属；不靠显示名猜身份。这不是更新清单或更新器。
Logical review groups are core, OCR, catalog, localization, stable-map-ID components and shared map inputs/provenance. Alias ownership can be multi-valued and comes from existing catalog references, never display names. This is not an update manifest or updater.

## Historical antivirus incident / 历史杀毒事件

2026-10-09，Kaspersky 曾检测并删除 Release 的测试专用 `NovenAppPathsTests.exe`：`VHO:Trojan-Banker.Win32.ClipBanker.gen`。该 EXE 从未进入安装清单。后续未修改源代码的 clean Release 重建/完整 CTest 为 90/90，通过时未观察到 Main/Host/Setup 检测；这不证明历史事件是误报，也不构成永久安全保证。保留原检测记录，不禁用保护、不加排除、不修改代码规避。如果出货制品出现检测，停止发布验收并报告。
Kaspersky historically detected/deleted the Release-only AppPaths test executable with the named heuristic. It was never shipped. A later unchanged-source clean rebuild passed 90/90; no shipped Main/Host/Setup detection was observed. Neither fact proves a false positive or permanent safety. Preserve history, keep protection active, add no exclusions, and stop acceptance if a shipped artifact is detected.
