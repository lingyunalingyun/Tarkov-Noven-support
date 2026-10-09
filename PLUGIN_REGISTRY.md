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
