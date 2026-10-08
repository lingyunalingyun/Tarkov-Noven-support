# Host 传输 v1 / Host transport v1

Phase 2 建立的第一方进程边界继续保留；Phase 3 仅向明确授权的 Manifest V2 会话扩展原生加载与声明式页面。V1 永久仅元数据，未知运行字段无效；发现和 Refresh 不启动新插件，启动时只恢复有效的已保存授权。
The Phase 2 boundary is preserved. Phase 3 adds native loading/declarative pages only for approved V2 sessions. V1 stays permanently metadata-only; unknown runtime fields remain harmless. Discovery/Refresh never start new plugins; startup resumes valid saved consent only. See [Native API](PLUGIN_API.md).

## 所有权 / Ownership

App 持有 PluginRuntimeManager 和授权控制器；生产原生会话只由显式授权或有效已保存授权启动。Start 仍仅供第一方 V1 传输测试，不加载插件。每个身份一个独立 Host/管道/Job。
生产 Host 路径固定为当前可执行文件目录中的 `NovenPluginHost.exe`，不从清单、插件目录或工作目录解析。
通过 CreateProcessW 挂起创建、不继承句柄、无控制台、不提权；加入 KILL_ON_JOB_CLOSE Job 后才恢复执行。
App owns manager/consent controller; native sessions require explicit or valid persisted consent. Start remains a first-party V1 transport test without DLL loading. One Host/pipe/Job per plugin identity.
The host is executable-relative, created suspended without inherited handles/console/elevation, assigned to its Job, then resumed.

这提供崩溃隔离、地址空间分离和受控 IPC，并不是 OS 安全沙箱。
只有 Host 可以加载已授权 V2 的 DLL；没有插件 EXE/子运行时/网络/EFT API/内置 UI 修改或原生 UI/Core 指针传输。
Process isolation is not an OS security sandbox. Only Host loads approved V2 DLLs; there are no plugin EXEs, child runtimes, network/EFT APIs, built-in UI modifications or native UI/core pointers.

## 帧与握手 / Frames and handshake

每帧为 4 字节小端无符号长度，随后严格 UTF-8 JSON；长度须为 1..65536 字节。
支持分片和连续帧，拒绝非法 UTF-8/JSON、重复键、未知类型、缺失/额外字段、非法长度与半帧断连。
Each frame is uint32 little-endian length followed by strict UTF-8 JSON (1..65536 bytes).
Partial/coalesced reads are supported; malformed data, duplicate keys, unknown types and missing/extra fields reject.

传输版本 `protocolVersion: 1` 独立于 `manifestVersion` 和 `apiVersion`。
握手消息为 hello / helloAck，双方包含以下字段：
Transport version is separate from manifest/API versions. Both handshake messages contain:

```json
{"type":"hello","protocolVersion":1,"pluginId":"com.example.loot-route","session":"<64 lowercase hex characters>"}
```

helloAck 仅将 type 改为 helloAck。只有匹配版本、预期 ID、会话密钥及所创建进程 PID 后才进入 Ready。
密钥与管道名分别由 BCryptGenRandom 生成 256 位随机值，不落盘、不进入日志/UI/快照。
管道 DACL 仅允许当前登录 SID，拒绝远程连接；Host 同样核对服务端父 PID，且仅使用 identification impersonation level。
Ready requires matching version/ID/secret and the exact launched client PID; Host also checks server parent PID.
Independent 256-bit CNG secrets name/authenticate sessions, never persisted/logged/displayed. Pipes restrict access to the logon SID and reject remote clients.

同一登录会话内仍不防御拥有相应进程/文件访问权限的恶意主体；启动参数可被此类主体观察。
Host 完整性/签名、信任评审和 OS 沙箱不在本阶段范围内。
This does not defend against same-logon actors with appropriate process/file access; launch arguments may be observable.
Host signing/integrity, trust review and OS sandboxing are outside this phase.

## 消息与生命周期 / Messages and lifecycle

### Phase 3 Host 扩展 / Phase 3 Host extensions

传输版本仍为 1；以下消息只属于显式启用的 Manifest V2 会话。V1 未知字段不产生加载请求。
Transport remains v1; runtime messages belong only to explicitly enabled V2 sessions. V1 unknown fields never produce a load request.

```json
{"type":"loadPlugin","manifestVersion":2,"apiVersion":1,"abiVersion":1,"directory":"<validated absolute plugin directory>","entry":"plugin.dll","pagePermission":true}
{"type":"loadPluginResult","result":0}
{"type":"uiRegisterPage","pageId":"dashboard","title":"Example"}
{"type":"uiPublishPage","pageId":"dashboard","document":"<UI Document v1 JSON>"}
{"type":"uiAction","pageId":"dashboard","actionId":"refresh"}
{"type":"uiActionResult","result":0}
{"type":"log","text":"Example initialized"}
```

加载结果：0 成功，1 路径不安全/文件无效，2 系统加载失败，3 缺少导出，4 ABI 不兼容，5 实例不合法，6 初始化失败。
动作结果仅使用 0 成功 / 6 失败。字段精确匹配，不支持任意命令或远程方法。
Load results: 0 success, 1 unsafe/invalid file, 2 loader failure, 3 missing export, 4 incompatible ABI, 5 invalid instance, 6 initialization failure.
Actions use 0 success / 6 failure. Schemas match exactly, with no arbitrary commands or methods.

pageId 和 actionId 都是有界局部身份，不能包含点或选择内置页面；全局身份由 Noven 构造。
日志每秒最多 16 条、每条最多 1024 字节；页面最多 8 个，文档最多 24 KiB / 64 块，每秒最多 32 次发布。
权限同时由 Noven 会话边界和 Host API 校验；消息不携带可由插件伪装的全局插件身份。
Page/action IDs are bounded local identities without dots or built-in selectors; Noven constructs global identities.
Logging is capped at 16 messages/second and 1024 bytes each. Pages are capped at 8; documents at 24 KiB/64 blocks and 32 publications/second.
Session and Host API boundaries both enforce permission; messages cannot spoof a global plugin identity.

只有 Host 加载插件 DLL，使用绝对路径及 DLL_LOAD_DIR / SYSTEM32 搜索策略，不依赖 CWD 或 PATH。
进程隔离仍不是 OS 沙箱。此扩展不提供网络、产品数据或内置 UI 修改 API。
Only Host loads DLLs, using absolute paths with DLL_LOAD_DIR / SYSTEM32, never CWD/PATH search.
Process isolation is still not an OS sandbox. These extensions expose no network/product data/built-in UI modification APIs.

以下基础控制消息仅包含 `type`：ping、pong、shutdown、shutdownAck、protocolError；加载/UI 消息遵循上面的独立 schema。
Ready 后重复 hello/helloAck、未请求的 pong 或其他非法消息会收束会话。无任意方法调用。
握手期间允许 shutdown 并返回 shutdownAck；关闭交错的 ping/pong 不能伪装关闭确认。
Base control messages contain only type; runtime/UI messages use their schemas above. Unexpected messages terminate the session; there is no arbitrary method invocation.
Shutdown is accepted during handshake; queued ping/pong cannot substitute for shutdownAck.

状态：Stopped、Starting、Connecting、Handshaking、Ready、Loading、Running、Stopping、Exited、Crashed、ProtocolError。Loading 最多 5 秒，UI 动作最多 3 秒。
管道连接/握手各最多 5 秒；半帧收到首字节后最多 5 秒；发送最多 2 秒；ping 响应最多 3 秒。
关闭发送与确认共用 2 秒期限，再最多等待进程退出 2 秒；超时由父 Job 终止，仅针对自己拥有的进程。
每会话使用可中断 I/O 的受管理 jthread，UI 不阻塞等待；取消 I/O 后收割内核完成，避免 OVERLAPPED 生命周期错误。
没有后台轮询、无限重启或 detached 线程。每管理器最多保留 16 个身份；终态快照可由同一身份的再次明确启用替换，无热重载。
States and errors are stable, nonlocalized values. Connection/handshake/partial frames are bounded (5 s), writes (2 s), ping (3 s).
Shutdown shares a 2 s send/ack deadline plus 2 s exit wait, followed by owned-Job cleanup on failure.
Managed workers use interruptible I/O and drain kernel cancellation safely, never blocking the UI on protocol waits.
No polling/restart loops/detached threads; at most 16 retained identities per manager. Explicit re-enable may replace a terminal session; no hot reload.

Windows 行为参考：[Named pipe security](https://learn.microsoft.com/en-us/windows/win32/ipc/named-pipe-security-and-access-rights)、
[Job objects](https://learn.microsoft.com/en-us/windows/win32/procthread/job-objects)、
[CancelIoEx](https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-cancelioex)。

## Catalog message extension (transport v1)

旧消息 schema 和传输版本保持不变。目录通道不接受插件身份字段；所有请求/结果归属于认证会话。以下 JSON 都使用既有 length-prefixed UTF-8 帧。
Existing message schemas and transport version remain unchanged. Catalog messages cannot supply a plugin identity; requests/results belong to their authenticated session. All use existing length-prefixed UTF-8 frames.

- Core -> Host: `{"type":"catalogAccess","mask":7}`，仅加载前配置；位 1/2/4 为 Items/Tasks/Maps，Core 每次请求仍独立验证声明/授权。Configuration only before load; bits 1/2/4 are Items/Tasks/Maps, independently reauthorized by Core per request.
- Host -> Core: `{"type":"dataRequest","requestId":1,"catalog":"items","operation":"list","stableId":"","offset":0,"limit":32}`。get 使用稳定 ID、offset/limit=0。Get uses a stable ID and zero offset/limit.
- Core -> Host: `{"type":"dataResult","requestId":1,"status":0,"payload":"<catalog JSON>"}`。状态数值依次 0..6：ok/permissionDenied/notFound/invalidRequest/unavailable/tooLarge/limited，内部 schemaVersion/requestId/status 必须一致。Status codes 0..6 match the names in that order; embedded schema/request/status must agree.
- Host -> Core: `{"type":"dataResultAck","requestId":1}`。插件结果回调返回后发送，防止回调挂起占据会话。Sent after the result callback returns, bounding hung plugin callbacks.

请求/结果字段数、UTF-8、整数范围、目录/操作及响应 JSON 都严格验证；请求 ID 为 1..INT64_MAX，JSON 响应最多 24 KiB、帧仍最多 64 KiB。详见 [Catalog schema](PLUGIN_API.md#catalog-projection-v1)。
Exact fields, UTF-8, integer bounds, kinds/operations and result JSON are validated. Request IDs are 1..INT64_MAX, JSON results max 24 KiB and frames still max 64 KiB. See the catalog schema.

Core 仅在认证会话 Running 后处理目录请求，逐次检查会话冻结的声明及有效授权副本，结果在所属会话工作线程发送。Host 入队限 16/秒、16 未完成；Core 接收窗口限 32/秒（容纳相邻发送窗口），仍最多 16 未确认结果。重复/洪泛协议请求关闭所属会话；正常 API 调用立即返回状态/额度错误。
Core handles catalog requests only after authenticated Running and checks the session's copied declaration/valid grants per request, returning on that session worker. Host permits 16/second and 16 outstanding; Core allows 32/second to tolerate adjacent sender windows, still max 16 unacknowledged results. Duplicate/flooded protocol requests close their session; normal API calls immediately return state/limit errors.

每个结果回调确认期限为 3 秒；挂起/崩溃仅收束所属 Host。Stop 撤销待处理状态，不再发送新的结果，排空旧目录消息直到 shutdownAck；新一代会话不复用旧管道、请求或授权。UI 不做跨进程等待；没有目录轮询线程。
Each result callback has a 3 s acknowledgement deadline; hangs/crashes terminate only its Host. Stop invalidates pending state, sends no new results and drains old catalog messages until shutdownAck. Replacement generations never reuse pipes, requests or grants. UI never waits on cross-process data; no catalog polling thread exists.
