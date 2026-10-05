# 第一方 Host 传输 v1 / First-party Host transport v1

Phase 2 仅提供第一方进程边界，不执行第三方代码，也不改变已接受的 [Manifest V1](PLUGIN_MANIFEST.md)。
软件启动、插件发现和 Refresh 均只读取元数据；请求权限不等于授权，`entry/dll/exe/command` 等未知字段无执行效果。
Phase 2 provides a first-party process boundary only. Discovery/startup/Refresh read metadata without launching hosts.
Requested permissions are not grants; unknown executable-looking manifest fields have no execution effect.

## 所有权 / Ownership

App 持有空闲 PluginRuntimeManager。只有第一方测试显式调用 Start；每个插件身份一个独立 Host/管道/Job。
生产 Host 路径固定为当前可执行文件目录中的 `NovenPluginHost.exe`，不从清单、插件目录或工作目录解析。
通过 CreateProcessW 挂起创建、不继承句柄、无控制台、不提权；加入 KILL_ON_JOB_CLOSE Job 后才恢复执行。
App owns an idle manager. Only first-party tests explicitly start sessions, one Host/pipe/Job per plugin identity.
The host is executable-relative, created suspended without inherited handles/console/elevation, assigned to its Job, then resumed.

这提供崩溃隔离、地址空间分离和受控 IPC，并不是 OS 安全沙箱。
没有插件 DLL/EXE 加载、子运行时、网络、EFT 访问、UI 扩展消息或原生 UI/Core 指针传输。
Process isolation is not an OS security sandbox. No third-party code, child runtime, network, EFT access,
UI extension messages or raw native UI/core pointers exist in this protocol.

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

除握手外，每条消息仅包含 `type`：ping、pong、shutdown、shutdownAck、protocolError。
Ready 后重复 hello/helloAck、未请求的 pong 或其他非法消息会收束会话。无任意方法调用。
握手期间允许 shutdown 并返回 shutdownAck；关闭交错的 ping/pong 不能伪装关闭确认。
Non-handshake messages contain only type. Unexpected messages terminate the session; there is no arbitrary method invocation.
Shutdown is accepted during handshake; queued ping/pong cannot substitute for shutdownAck.

状态：Stopped、Starting、Connecting、Handshaking、Ready、Stopping、Exited、Crashed、ProtocolError。
管道连接/握手各最多 5 秒；半帧收到首字节后最多 5 秒；发送最多 2 秒；ping 响应最多 3 秒。
关闭发送与确认共用 2 秒期限，再最多等待进程退出 2 秒；超时由父 Job 终止，仅针对自己拥有的进程。
每会话使用可中断 I/O 的受管理 jthread，UI 不阻塞等待；取消 I/O 后收割内核完成，避免 OVERLAPPED 生命周期错误。
没有后台轮询、无限重启或 detached 线程。每管理器最多保留 16 个会话，终态快照保留至管理器销毁；本阶段不提供重启/热重载。
States and errors are stable, nonlocalized values. Connection/handshake/partial frames are bounded (5 s), writes (2 s), ping (3 s).
Shutdown shares a 2 s send/ack deadline plus 2 s exit wait, followed by owned-Job cleanup on failure.
Managed workers use interruptible I/O and drain kernel cancellation safely, never blocking the UI on protocol waits.
No polling/restart loops/detached threads; at most 16 retained sessions per manager, without restart/hot reload in this phase.

Windows 行为参考：[Named pipe security](https://learn.microsoft.com/en-us/windows/win32/ipc/named-pipe-security-and-access-rights)、
[Job objects](https://learn.microsoft.com/en-us/windows/win32/procthread/job-objects)、
[CancelIoEx](https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-cancelioex)。
