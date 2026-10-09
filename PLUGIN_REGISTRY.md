# Noven Plugin Registry v1

GitHub 托管的第一方 Registry 是市场机器可读数据唯一来源，Muse 不是来源。客户端不维护硬编码插件目录。Phase 9 仅浏览元数据，不安装、下载包、执行代码、授权或修改本地插件。测试夹具仅在 tests/fixtures，不能作为生产条目。
The first-party GitHub-hosted Registry is the marketplace's machine-readable source of truth, not Muse. The client has no hardcoded plugin catalog. Phase 9 browses metadata only: no installation/package download/execution/grants/local-plugin mutation. Test fixtures live only under tests/fixtures and must never become production listings.

有界单文件 `registry.json`：UTF-8、最多 1 MiB、最多 1000 插件。每个插件只发布一个 current 推荐版本，后续可版本化扩展，避免当前多文件抓取不一致。字段顺序无关；重复 JSON 键/插件 ID、非法 UTF-8、过深/错误类型拒绝，未知可选字段作为数据忽略，包括 entry/dll/exe/command。不读取 HTML、远程图片或执行引用。
Bounded single-file `registry.json`: UTF-8, at most 1 MiB and 1000 plugins. Each entry publishes one recommended current version; future schemas may extend this without inconsistent multi-file fetching today. Field order is irrelevant. Duplicate JSON keys/plugin IDs, invalid UTF-8, excessive nesting/wrong types reject. Unknown optional fields are ignored as data, including entry/dll/exe/command. No HTML/remote images/reference execution.

```json
{
  "registryVersion": 1,
  "plugins": [{
    "id": "com.author.example",
    "name": "Example",
    "author": "Author",
    "summary": "Short summary",
    "description": "Plain text description",
    "source": "https://github.com/author/example",
    "homepage": "https://example.org",
    "license": "MIT",
    "categories": ["Utilities"],
    "tags": ["routes"],
    "status": "unreviewed",
    "current": {
      "version": "1.0.0",
      "manifestVersion": 2,
      "apiVersion": 1,
      "permissions": ["ui.page.register"],
      "sourceRef": "v1.0.0",
      "releaseUrl": "https://github.com/author/example/releases/tag/v1.0.0"
    }
  }]
}
```

必需顶层 registryVersion=1、plugins；条目必需 id/name/author/summary/source/current。ID 复用本地清单规则（小写 ASCII 反向域名，最多 128 字节，builtin./plugin. 保留）。name/author/summary/description 上限 128/256/512/8192 UTF-8 字节；license 128、sourceRef 256；URLs 最多 2048，严格 HTTPS DNS URL，无 userinfo。categories 最多 16、tags 最多 32，每项 64 字节，不重复，不猜测分类。
Required root: registryVersion=1 and plugins. Required entry: id/name/author/summary/source/current. IDs reuse local manifest rules (lowercase ASCII reverse-domain, 128 bytes, reserved builtin./plugin.). Name/author/summary/description limits are 128/256/512/8192 UTF-8 bytes; license 128, sourceRef 256. URLs are at most 2048 bytes, strict HTTPS DNS URLs without userinfo. At most 16 categories/32 tags, 64 bytes each, no duplicates or inferred categories.

current 必需严格 SemVer version 和正整数 manifestVersion/apiVersion；可选 permissions（最多 64，每项 128），未知权限保留但不支持。network.http 必须提供 network.origins，复用 Phase 8 精确 HTTPS 来源及规范化规则。当前已知 Manifest 1/2 + API 1 + 支持权限判为 Compatible；API/权限不支持为 Incompatible；未来清单格式为 Unknown。Compatible 不是代码运行保证，V1 仍仅元数据。本项目还没有最低产品版本的可靠兼容契约，因此不定义 minimumNovenVersion，也不猜测。
current requires strict SemVer version and positive manifestVersion/apiVersion. Optional permissions: 64 names of 128 bytes; unknown permissions remain metadata but unsupported. network.http requires network.origins using Phase 8 exact HTTPS canonicalization. Known Manifest 1/2 + API 1 + supported permissions is Compatible; unsupported API/permissions is Incompatible; future manifest formats are Unknown. Compatible is not an execution guarantee; V1 remains metadata-only. There is no reliable minimum product-version compatibility contract yet, so minimumNovenVersion is not defined or guessed.

可选 package 为 {url,asset,sha256,size}，URL 规则同上，asset 最多 256 字节，sha256 为 64 个小写十六进制字符，size 为 1..1 GiB 字节。它们只显示，不拉取或校验包，SHA-256 不等于真实性或签名保证。sourceRef/releaseUrl 也是展示引用，不自动打开。
Optional package is {url,asset,sha256,size}: same URL policy, asset at most 256 bytes, SHA-256 exactly 64 lowercase hexadecimal characters, size 1..1 GiB bytes. Display only: no fetching/package verification; a hash is not authenticity or a signature guarantee. sourceRef/releaseUrl are display references, never opened automatically.

status 默认 unreviewed，允许 reviewed/deprecated/blocked。reviewed 仅表示 Registry 维护者声明已审阅发布资料，不是恶意软件保证、OS 沙箱、长期反作弊保证或签名验证。blocked 显示警告但不自动禁用/删除本地插件。Registry 上架不使代码安全；权限预览不授予权限。
status defaults to unreviewed and permits reviewed/deprecated/blocked. Reviewed means Registry maintainers declare review of publication materials, not malware-proofing, an OS sandbox, lasting anti-cheat compliance or signature verification. Blocked shows a warning without automatically disabling/deleting local plugins. Listing does not make code safe; permission preview grants nothing.

## Client snapshots and cache

服务在托管工作线程获取 Registry，UI 接收不可变快照。构造只载入缓存，不联网；进入市场可请求获取，显式刷新至少间隔 5 秒，自动进入刷新至少间隔 30 秒且 Live 快照 15 分钟内复用。没有后台轮询。状态区分 Unconfigured/Empty/Loading/Live/Cached/Error；Cached 总是标注缓存，fetch 时间随快照保存。
The managed worker fetches Registry data and publishes immutable snapshots. Construction loads cache only, without networking. Entry may request refresh; explicit refresh has a five-second cooldown, entry refresh a thirty-second cooldown and reuses Live snapshots for fifteen minutes. No background polling. States distinguish Unconfigured/Empty/Loading/Live/Cached/Error; Cached is always labeled and preserves its fetch timestamp.

第一方数据区缓存绑定配置来源，cacheVersion=1，最多 6 MiB + 4 KiB（原始 1 MiB JSON 的最坏转义大小）。载入重新验证 Registry；损坏或不同来源缓存忽略。仅完整成功数据经过唯一临时文件、FlushFileBuffers、原子替换落盘，网络/解析/替换失败保留旧缓存；不写 plugins/ 或插件存储。
The first-party data cache binds its configured source, uses cacheVersion=1 and is bounded to 6 MiB + 4 KiB (worst-case escaping of the original 1 MiB JSON). Reload revalidates Registry data; corrupt or different-source caches are ignored. Only fully validated data is persisted through a unique temporary file, FlushFileBuffers and atomic replacement. Network/parse/replacement failures retain the previous cache. Nothing writes plugins/ or plugin storage.

生产来源仅由第一方构建配置 `NOVEN_PLUGIN_REGISTRY_URL` 指定，默认空（尚无已确认公开端点），普通 UI 不提供 URL 设置。当前只接受 `https://raw.githubusercontent.com/<owner>/<repo>/<ref>/registry.json`，HTTPS 443，不允许 query、遍历或动态引用。第一方 WinHTTP GET 与插件 network.http 分离；默认 TLS 校验、总期限 20 秒、阶段超时 5 秒、正文 1 MiB、头部 16 KiB。零重定向，不使用代理/PAC、自动认证或 Cookie，不请求包、图片、HTML。
Production source is first-party build configuration `NOVEN_PLUGIN_REGISTRY_URL`, empty by default until a public endpoint is confirmed. Normal UI exposes no URL setting. Currently only `https://raw.githubusercontent.com/<owner>/<repo>/<ref>/registry.json` is accepted: HTTPS 443, no query/traversal/dynamic references. First-party WinHTTP GET is separate from plugin network.http: normal TLS validation, twenty-second total deadline, five-second phase timeouts, 1 MiB body and 16 KiB headers. Zero redirects, no proxy/PAC/automatic authentication/cookies; no packages/images/HTML fetches.

市场复用原生插件中心列表/详情、搜索与下拉组件。搜索只在当前快照匹配名称、ID、作者、分类和标签；分类来自 Registry，不生成虚构分类；可筛选兼容及审阅状态，按名称/ID 确定性排序。分类菜单超高时用滚轮滚动。两个标签独立保存搜索/选择/滚动。详情显示权限支持情况、精确来源、引用与包元数据，绝不自动打开 URL 或授予权限。
Marketplace reuses native Plugin Center list/details, search and dropdowns. Search locally matches name/ID/author/categories/tags against the snapshot; categories come only from Registry. Compatibility and review-state filters are available, with deterministic name/ID sorting. Overflow category menus scroll with the mouse wheel. Tabs independently preserve search/selection/scroll. Details preview permission support, exact origins, references and package metadata; URLs are never automatically opened and permissions never granted.

本地匹配只按插件 ID，再按严格 SemVer 显示同版/旧版/更新本地版；重复/无效本地记录或 V1 元数据明确标注。blocked 记录仍可查看，警告也显示于匹配的本地详情，但不执行自动禁用或删除。
Local matching uses plugin ID only, then strict SemVer for same/older/newer-local versions. Invalid/conflicting local records and V1 metadata are labeled explicitly. Blocked records remain browsable and warnings also appear in matching local details, without automatic disable/deletion.

## Offline manual review

尚无已确认公开 Registry 地址时，正常 Release 保持未配置。单独构建 `-DBUILD_TESTING=ON -DNOVEN_MARKETPLACE_REVIEW_FIXTURE=ON`，且不能同时配置生产 URL，可注入 tests/fixtures/plugin-registry-review.json。UI 始终标注“测试夹具 — 非生产目录”，使用独立 marketplace-review-cache.json，刷新仅重读此固定夹具。可暂时移走该夹具模拟失败并查看缓存，之后恢复；正常构建不含夹具路径/后端。此模式不代表真实远程 Marketplace 验收通过。
Without a confirmed public Registry endpoint, normal Release remains unconfigured. A separate build with `-DBUILD_TESTING=ON -DNOVEN_MARKETPLACE_REVIEW_FIXTURE=ON` (incompatible with a production URL) injects tests/fixtures/plugin-registry-review.json. UI always labels TEST FIXTURE — not production, uses a separate marketplace-review-cache.json, and refresh only rereads that fixed fixture. Temporarily moving the fixture simulates failure/cached fallback; restore it afterwards. Production builds contain no fixture path/backend. This is not real remote Marketplace acceptance.
