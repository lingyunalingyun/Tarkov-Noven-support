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

当前 vendored ONNX Runtime 为 1.30.0.20260909.8.f2c39fe；包内保留微软 MIT 许可。正式公开发布前仍需核对该二进制原始发行包的完整 ThirdPartyNotices；本地构建和安装验收不代表第三方素材再分发权利已全部审定。
The vendored ONNX Runtime is 1.30.0.20260909.8.f2c39fe; Microsoft's MIT license is retained. Before public distribution, verify the complete ThirdPartyNotices from this binary's original distribution. Build/installation acceptance is not a complete third-party redistribution rights review.

可选 -SigningScript 接受单个 artifact 路径，依次签主 EXE、Host、Setup，并检查有效签名。证书、PFX、密码和私钥必须由外部发布环境管理，不入库。未签名版本可能出现 SmartScreen 警告；SHA-256 只是制品信息，不是 Signed Update Trust。
Optional SigningScript receives one artifact path and signs main/Host/Setup with valid-signature checks. Certificates/PFX/passwords/private keys belong to external release infrastructure, never this repository. Unsigned builds may show SmartScreen warnings. SHA-256 is artifact information, not signed-update trust.

## Manual acceptance

自动测试不是人工安装验收。请实际执行：全新安装（检查当前用户、Start Menu、OCR 和插件）→ 产生真实用户数据 → 正常关闭 → 下一版本 Setup 覆盖升级（检查版本和所有数据/原授权保留）→ 卸载（Program 消失、User Root 保留）→ 再安装（数据恢复可见）。运行中安装也应要求正常关闭。未经用户确认，不标记人工 PASS；不要使用自动化测试向真实 LocalAppData 灌入夹具。
Automated tests are not manual acceptance. Actually install, verify per-user/Start Menu/OCR/plugins, create real user data, close normally, upgrade with the next-version Setup and verify version/data/grants, uninstall and verify Program removal/User Root retention, reinstall and verify data. Running apps must require normal closure. Manual PASS requires user confirmation; tests must not inject fixtures into real LocalAppData.
