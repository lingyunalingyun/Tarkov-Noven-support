# Native Plugin API v1

第三方 DLL 仅在独立的 `NovenPluginHost.exe` 中运行，每个插件一个 Host。主程序拥有安全 UI、授权、注册表、文档渲染与状态存储；不把原生 UI/Core 指针传给插件。
Third-party DLLs run only in a dedicated `NovenPluginHost.exe`, one per plugin. Noven owns security UI, consent, registry, rendering and state; no native UI/core pointers cross the boundary.

**PluginHost 不是 OS 安全沙箱。** 它隔离崩溃与地址空间，并限制 Noven API，但原生插件仍可自行以当前用户身份调用 Windows API。仅启用你信任的本地代码；不存在签名、审核或“安全/已验证”保证。
**PluginHost is not an OS sandbox.** It isolates crashes/address spaces and controls Noven APIs, but native code can independently call Windows APIs as your user. Enable only code you trust. There are no signature/review/safety guarantees.

## Manifest and consent

[Manifest contract](PLUGIN_MANIFEST.md)：V1 永久仅元数据，未知的 `runtime/entry/dll/exe/command` 不执行。V2 保留公共字段并要求：
[Manifest contract](PLUGIN_MANIFEST.md): V1 permanently remains metadata-only, including unknown executable-looking fields. V2 retains common fields and requires:

```json
{
  "manifestVersion": 2,
  "id": "com.example.noven-hello",
  "name": "Hello Plugin",
  "version": "1.0.0",
  "apiVersion": 1,
  "permissions": ["ui.page.register"],
  "runtime": {"kind": "native-dll", "entry": "noven-hello.dll"}
}
```

入口仅限插件目录直接子文件名、最多 128 UTF-8 字节、小写 `.dll` 扩展；拒绝路径、`..`、环境变量、驱动器、Windows 保留文件名及重解析/硬链接/越界路径。主程序与 Host 都检查真实路径和 x64 DLL PE 标志；文件句柄禁止会话期间替换/写入。只有 Host 使用绝对路径 `LoadLibraryExW`，搜索限 DLL 目录和 System32，不依赖 CWD/PATH。
Entry is a direct-child filename, at most 128 UTF-8 bytes, ending in lowercase `.dll`. Paths, traversal, variables, drives, reserved names and reparse/hardlink/escaping files are rejected. Both processes validate canonical containment and x64 DLL PE flags, retaining handles against replacement/writes. Only Host calls `LoadLibraryExW`, with DLL-directory/System32 search, not CWD/PATH.

新插件默认 Disabled；发现和 Refresh 不启动新会话。受保护的 `builtin.plugins` 内含 Marketplace / My Plugins；市场只显示未连接状态，无网络、假目录、安装或下载。V1 不提供运行按钮。
New plugins default Disabled; discovery/Refresh never start new sessions. Protected `builtin.plugins` contains Marketplace / My Plugins. Marketplace is an offline empty state, without networking, fake catalogs, installation or downloads. V1 has no runtime control.

Enable 必须明确确认插件身份、版本、作者、请求权限及原生代码/非沙箱警告。当前唯一支持的权限是 `ui.page.register`；未声明/未授予即拒绝页面 API。日志无需此权限；其他请求（包括 `ui.decorate`、产品数据、网络与存储）不支持且阻止启用。
Enable requires explicit confirmation of identity/version/author, requested permissions and native-code/non-sandbox warnings. Only `ui.page.register` is supported; undeclared/ungranted page API calls are denied. Logging needs no page grant. Other requests, including decoration/product data/network/storage, are unsupported and block Enable.

授权与运行意图单独保存在 `<exe>/data/plugin-state.json`，有界解析、原子替换、损坏时默认禁用；不包含清单、代码、会话密钥或管道名。权限增加撤销旧授权；权限减少不增加特权。已保存且仍有效的授权可在下次启动恢复；崩溃/失败会禁用，不无限重启。禁用先撤销 UI/输入，再有界关闭 Host；普通应用退出保留授权意图。
Intent/grants live separately in bounded, atomically replaced `<exe>/data/plugin-state.json`, fail-closed on corruption, without manifests/code/session secrets/pipe names. Permission expansion revokes consent; removal adds no privilege. Valid saved intent may resume next launch. Faults disable intent without restart loops. Disable revokes UI/input before bounded Host shutdown; normal app exit preserves approved intent.

## C ABI

公共头文件：[sdk/noven_plugin_abi_v1.h](sdk/noven_plugin_abi_v1.h)。四个独立版本：Manifest V1/V2、transportProtocolVersion=1、apiVersion=1、nativePluginAbiVersion=1。不要混用。
Public header: [sdk/noven_plugin_abi_v1.h](sdk/noven_plugin_abi_v1.h). Manifest, transport, API and native ABI versions are independent.

固定导出：`NovenPlugin_GetAbiVersion()` 和 `NovenPlugin_Initialize(host, instance)`；使用 C ABI / `__cdecl`、固定宽度整数、显式 `struct_size`、UTF-8 切片，无 STL/COM/HWND/D2D/Core 对象。
Fixed exports use C ABI / `__cdecl`, fixed-width integers, explicit sizes and UTF-8 slices, with no STL/COM/HWND/D2D/core objects.

Host 表提供 `log`、`register_page(local_id,title)`、`publish_page(local_id,document_json)`。返回实例必须提供 `shutdown`、`on_ui_action(local_page_id,action_id)`。Host 初始化实例 size/ABI；插件验证后填写成员并返回 `NOVEN_OK`。尺寸、导出、版本或回调错误拒绝初始化。
Host table offers only logging, page registration and document publication. Instances must provide shutdown and action callbacks. Host initializes size/ABI; plugins validate/fill and return `NOVEN_OK`. Invalid sizes/exports/versions/callbacks reject initialization.

切片仅在调用期间有效；接收方复制。Host 表在 shutdown 返回前有效；插件拥有自己的实例 context，成功初始化后的 shutdown 负责释放，初始化失败必须自行清理。异常不能穿过 ABI。API 仅在 Host 回调线程同步调用，无后台插件任务。进程异常终止时不保证 shutdown 调用。
Slices are borrowed for the call only; receivers copy. Host table lives through shutdown. Plugins own instance context and release it in shutdown after successful init; failed init cleans itself up. No exceptions across ABI. Calls are synchronous on the Host callback thread, not background plugin tasks. Forced/crash termination cannot guarantee shutdown.

日志最多 1024 UTF-8 字节/条、每秒 16 条；Host/Noven 自动附加插件身份，文本不作格式串，插件不能选择日志文件或伪装其他身份。API 状态码见头文件；越界、无权限、非法状态与限流都是明确失败，不隐式授权。
Logs are bounded to 1024 bytes and 16 per second; Host/Noven attach identity, never interpret text as format strings, and expose no log path/spoofed identity. API failure codes are in the header; bounds/permissions/state/rate failures never imply grants.

## UI Document v1

局部 page/action ID：1..64 字节，小写 ASCII `a-z/0-9/-`，首尾字母或数字，无点、路径或内置选择器。Noven 生成 `plugin.<plugin-id>.<local-page-id>`，例如 `plugin.com.example.noven-hello.dashboard`。最多 8 页，Secondary 内置页面之后、Bottom 管理页之前，Source=Plugin / Extensible，不能 Protected 或覆盖内置页。
Local page/action IDs: 1..64 lowercase ASCII alphanumeric/hyphen characters with alphanumeric ends, no dots/paths/built-in selectors. Noven generates global namespace and controls placement after built-in Secondary, before Bottom. Up to 8 pages, Plugin/Extensible, never Protected or built-in overrides.

```json
{
  "schemaVersion": 1,
  "blocks": [
    {"type": "heading", "text": "Hello"},
    {"type": "text", "text": "Rendered by Noven"},
    {"type": "keyValue", "key": "Clicks", "value": "0"},
    {"type": "badge", "text": "Example"},
    {"type": "separator"},
    {"type": "button", "id": "increment", "label": "Increment"}
  ]
}
```

文档最多 24 KiB、64 块，每秒 32 次发布；帧独立最多 64 KiB（JSON 转义可能膨胀）。严格 JSON/UTF-8，拒绝未知字段/类型；text 最多 4096 字节（可多行），heading/badge/key/button label 最多 256（单行），value 最多 1024（单行）。按钮 ID 在当前文档内唯一。无 HTML/JS/Markdown/图片/字体/颜色/绝对布局/内置目标。
Documents: 24 KiB, 64 blocks, 32 publications/second; wire frames independently 64 KiB after escaping. Strict JSON/UTF-8, unknown fields/types rejected. Text up to 4096 bytes (multiline); headings/badges/keys/button labels 256, values 1024 (single-line). Button IDs are unique per current document. No HTML/JS/Markdown/images/custom style/absolute layout/built-in targets.

Noven 拥有绘制和命中几何。按钮只向所属认证会话发送当前页面/动作，校验代次、权限、Running 状态及当前文档；失效/跨插件动作拒绝。更新文档不更改全局身份。停止/崩溃清理页面与文档，正在查看者回到插件中心。
Noven owns drawing/hit geometry. Buttons target only their authenticated owner, validating generation, grants, Running state and current document. Stale/cross-plugin actions reject. Stop/crash removes pages/documents and returns active viewers to Plugin Center.

## Lifecycle and example

[Wire protocol](PLUGIN_PROTOCOL.md) 使用认证 Named Pipes 和 Job 所有权；只有完成加载与初始化后才 Running。初始化 5 秒、动作 3 秒，关闭确认 2 秒加退出 2 秒，失败由父进程收束自有 Host。DLL 初始化/回调崩溃或死锁只影响所属 Host；不是捕获所有插件错误，也不是 OS 沙箱。
[Wire protocol](PLUGIN_PROTOCOL.md) uses authenticated Named Pipes and owned Jobs. Running follows successful load/init only. Init timeout 5 s, action 3 s, shutdown ACK 2 s plus exit 2 s; failures clean up the owned Host. Native crashes/deadlocks affect that Host, not every plugin/core service; this is not an OS sandbox.

纯 C 第一方样例源码在 [examples/hello-plugin](examples/hello-plugin)，仅日志、页面和计数按钮，无游戏、网络、任意文件或进程操作。默认不构建、不安装；MSVC x64 配置 `-DNOVEN_BUILD_PLUGIN_EXAMPLES=ON`，构建 `NovenHelloPlugin`。
The first-party C example logs, registers a page and updates a counter, without game/network/file/process access. It is neither built nor installed by default. Configure MSVC x64 with `-DNOVEN_BUILD_PLUGIN_EXAMPLES=ON`, build `NovenHelloPlugin`.

手动将生成的 `noven-hello.dll` 和样例 manifest.json 复制到 `<exe>/plugins/com.example.noven-hello/`。Refresh 后应 Disabled；明确 Enable 并确认后才运行。打开 Hello Plugin 页，按钮更新计数；Disable 撤销页面并关闭 Host。不要提交编译 DLL 或本地运行目录。
Copy built `noven-hello.dll` and sample manifest into the executable-relative plugin directory. Refresh shows Disabled; explicit Enable/approval starts it. Its button updates the counter; Disable removes page/Host. Do not commit compiled DLLs/runtime folders.

Phase 3 不提供产品数据、游戏访问、内置 UI 修改、网络市场、Muse/GitHub、签名或热重载。插件中心左侧为插件按钮列表，右侧仅展示选中插件的详情；启用/禁用操作固定在详情底栏，正文与滚动条不进入操作区。搜索只过滤本地列表，不改变授权或运行状态。市场保持离线空列表。侧栏普通导航区可滚动，新注册的插件页面会显露，Plugins/Settings 管理区保持底部锚定。保存失败会阻止新启用并提示关闭状态可能无法持久化。
Phase 3 has no product/game APIs, built-in UI modification, online marketplace, Muse/GitHub, signatures or hot reload. Plugin Center shows plugin buttons on the left and only the selected plugin's detail on the right; Enable/Disable stay in a fixed detail footer excluded from text and scrollbar bounds. Search filters the local list without changing grants/runtime state. Marketplace remains an offline empty list. Ordinary sidebar navigation scrolls and reveals newly registered plugin pages while Plugins/Settings stay bottom anchored. Save failure blocks new Enable and warns stopped state may not persist.
