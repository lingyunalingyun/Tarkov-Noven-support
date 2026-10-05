# Plugin Manifest V1

本阶段仅发现和显示本地元数据，不执行插件、不授予权限、不扩展主界面。
This phase discovers and displays local metadata only: no execution, permission grants or main-UI extensions.

## Location and discovery

将 UTF-8 JSON 清单放在可执行文件旁的 `plugins/<directory>/manifest.json`。
Place a UTF-8 JSON manifest at `<exe>/plugins/<directory>/manifest.json`.
目录名无需等于插件 ID；身份以清单中的 `id` 为准。
The directory name need not match the ID; manifest `id` defines identity.

启动和插件页的“刷新”读取一次；缺失/空根目录是正常空状态，不自动创建目录、不轮询。
Discovery runs at startup and on Plugins-page Refresh. Missing/empty roots are healthy empty states; no directories are created and no polling occurs.

仅扫描直接子目录，按目录路径确定性排序。最多 128 个子目录、1024 个根目录条目，每个清单最多 256 KiB；整体超限显示诊断，不选择部分“赢家”。
Only direct child directories are inspected, sorted deterministically by path. Limits: 128 child directories, 1024 root entries, 256 KiB per manifest. Capacity failure is explicit, never an arbitrary partial winner.

拒绝重解析点、硬链接清单和规范化后越界路径；不递归搜索、不读取运行文件、不展开环境变量、不访问网络、不修改插件文件。
Reparse points, hardlinked manifests and normalized out-of-root paths are rejected. Discovery does not recurse, inspect runtime binaries, expand environment variables, access networks or mutate plugin files.

## Schema

```json
{
  "manifestVersion": 1,
  "id": "com.example.loot-route",
  "name": "Loot Route",
  "version": "1.0.0",
  "apiVersion": 1,
  "description": "Local metadata example",
  "author": "Example author",
  "homepage": "https://example.com/",
  "source": "https://example.com/source",
  "license": "MIT",
  "permissions": ["catalog.maps.read", "ui.page.register"]
}
```

必填字段是 `manifestVersion`、`id`、`name`、`version`、`apiVersion`；其他示例字段可省略。
The first five fields are required; all other example fields are optional.
前者定义清单格式，后者仅表示未来 API 兼容代际；均支持正整数 `1`，并不意味着已经存在可执行 API。
`manifestVersion` defines the file schema; `apiVersion` declares future API compatibility only. Both support positive integer `1`, not an implemented executable API.
未来清单版本标记格式不兼容且不解释其字段；未来 API 版本保留有效元数据并标记 API 不兼容。
Future schema versions are marked incompatible without interpreting their fields; future API versions retain otherwise-valid metadata and are marked API-incompatible.

| String field | Maximum UTF-8 bytes |
| --- | ---: |
| id / version / license | 128 |
| name / author | 256 |
| description | 4096 |
| homepage / source | 2048 |

必填字符串不可为空/纯空白。拒绝 ASCII 控制字符和 DEL；仅 description 允许换行、回车、制表符。URL 为空或仅使用 http/https，不自动打开。
Required strings must not be empty/whitespace-only. ASCII controls and DEL are rejected, except newline, carriage return and tab in description. URLs are empty or http/https presentation metadata and never open automatically.

ID 为至少两段点分小写 ASCII 名称；每段仅 `a-z`、`0-9`、`-`，不可为空或以连字符开头/结尾。总长至多 128 字节，禁止路径分隔符、空白、前后点和 `builtin.` / `plugin.` 前缀。
IDs contain at least two dot-separated lowercase ASCII segments, each using only `a-z`, `0-9`, `-`, with no empty segment or leading/trailing hyphen. Maximum 128 bytes; path separators, whitespace, leading/trailing dots and `builtin.` / `plugin.` prefixes are forbidden.

version 符合 SemVer：三个无前导零整数，可带预发布和构建标识，例如 `1.2.3-beta.1+build.05`；纯数字预发布标识不可有前导零。版本和显示名称不是身份。
Versions use SemVer: three integers without leading zeros, optional prerelease/build identifiers (e.g. `1.2.3-beta.1+build.05`); numeric prerelease identifiers cannot have leading zeros. Version and display name do not define identity.

permissions 必须为字符串数组，最多 64 项，每项最多 128 字节，使用上述点分段语法（无 ID 保留前缀限制）。未知未来权限保留；重复项按首次声明顺序去重。这只是“请求”，没有授予效果。
Permissions are an array of at most 64 strings, each at most 128 bytes using the same segment syntax (without ID namespace reservations). Future names are preserved; duplicates are deduplicated in first-declaration order. These are requests only, not grants.

JSON 字段顺序无关，拒绝重复键、错误类型、非法 UTF-8 和畸形 JSON。未知字段可忽略；未定义 dll/exe/entry/command/process 或任何运行入口。
Member order is irrelevant. Duplicate keys, wrong types, invalid UTF-8 and malformed JSON are rejected. Unknown fields are ignored; no dll/exe/entry/command/process or execution entry is defined.

重复 ID 的所有已解析记录均标记冲突，包括 API 不兼容的记录，不按目录顺序选胜者。无效或不兼容目录保留可读状态和诊断，不能静默遮盖其他插件。
All parsed records sharing an ID are conflicted, including API-incompatible records; directory order never chooses a winner. Invalid/incompatible directories remain visible with diagnostics and cannot silently shadow neighbors.

`builtin.plugins` 是受保护的内置管理页，唯一动作是刷新。即使请求 `ui.page.register`，清单也不能注册页面或执行代码。
`builtin.plugins` is a protected built-in management page. Refresh is its only action; even `ui.page.register` cannot register a page or execute code.
