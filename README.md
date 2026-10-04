# Noven Tarkov Support

Noven Tarkov Support is a lightweight native Windows companion application
for Escape from Tarkov. The initial version is only a project skeleton with a
minimal Unicode Win32 window; game features will be added in later phases.

## Technology stack

- C++23
- Native Win32 APIs
- CMake
- MSVC on Windows x64
- Current text detection: ONNX Runtime CPUExecutionProvider with
  PaddleOCR PP-OCRv5 mobile detection
- Future screen capture: Windows Graphics Capture and/or DXGI Desktop Duplication

The project intentionally has no Electron, Tauri, WebView, .NET, Python,
PyTorch, or Python OCR runtime. ONNX Runtime is used for the current text
detection and text-recognition phases.

## Product and safety boundary

The application will only process information already visible to the player,
local public logs, or public external data. It will not:

- read game memory;
- inject into Escape from Tarkov;
- inspect network packets;
- access hidden game information; or
- perform automatic gameplay actions.

## Current status

The current phase adds the native capture trigger and OCR baseline:

- global F2 hotkey;
- current cursor anchor in physical virtual-screen coordinates;
- configurable 800 x 600 ROI calculation with desktop-boundary clipping;
- one capture per hotkey press; and
- one debug BMP with detected boxes written under `debug-captures` next to the
  executable; and
- independent recognition of each detected box, kept in memory and logged for
  debugging.

Price APIs, maps, log parsing, account synchronization, overlay result cards,
and game-specific logic are not implemented yet.

## Text detection phase

The current OCR module performs text detection. It accepts the in-memory
BGRA8 `CaptureResult`, converts and resizes the ROI to an RGB NCHW tensor, runs
one PP-OCRv5 mobile detection model on ONNX Runtime's CPU execution provider,
and returns boxes relative to the captured ROI. Recognition and offline catalog
matching are separate per-box stages; spatial candidate selection then ranks
the independently matched boxes.

Model:

- `PP-OCRv5_mobile_det_onnx`, `assets/models/ppocrv5_mobile_det.onnx`
- Source: PaddlePaddle's official
  [PaddleOCR model](https://huggingface.co/PaddlePaddle/PP-OCRv5_mobile_det_onnx)
- Size: 4,826,518 bytes (the checked-in model file)
- Input: float32 `[1, 3, 640, 640]`, RGB, values normalized from `[0, 1]` to
  `[-1, 1]`
- License: Apache-2.0

## Text recognition phase

Each detected box is cropped from the in-memory capture, resized to the
recognizer's 48-pixel input height with aspect ratio preserved and right
padding, then recognized independently on ONNX Runtime's CPU execution
provider. The result keeps the original box, UTF-8 text, and CTC confidence;
recognized boxes are never concatenated into one ROI-wide string.

Model and dictionary:

- `PP-OCRv5_mobile_rec`, `assets/models/ppocrv5_mobile_rec.onnx`
- Source: PaddlePaddle's official
  [PP-OCRv5 mobile recognition ONNX model](https://huggingface.co/PaddlePaddle/PP-OCRv5_mobile_rec_onnx)
- Size: 16,534,782 bytes
- SHA-256: `DA72DC72CA4DC220DF0DFDE68C1DEDC31C58D3E76A25871122E5056227D50092`
- Input: float32 `[1, 3, 48, 320]`, BGR, normalized from `[0, 1]` to `[-1, 1]`
- Languages: Simplified Chinese and English, with the model dictionary also
  covering the official model's additional characters
- License: Apache-2.0
- Dictionary: `assets/models/ppocrv5_mobile_rec_dict.txt`, extracted from the
  model's official `inference.yml`, UTF-8, 18,383 entries; one entry per line.
  The model and dictionary are local assets and are not downloaded at runtime.

The recognizer is initialized once at startup and warmed once. Recognition
runs sequentially on the existing OCR worker thread. Debug output logs each
recognized box and writes the captured and detected images under
`debug-captures`.

ONNX Runtime is loaded from the official Windows x64 CPU distribution at
runtime. Prepare the local development dependency before configuring CMake:

1. Download `onnxruntime-win-x64-1.30.0.zip` from the
  [ONNX Runtime v1.30.0 release](https://github.com/microsoft/onnxruntime/releases/tag/v1.30.0).
2. Extract its `include` directory to `third_party/onnxruntime/include`.
3. Copy `lib/onnxruntime.dll` to `third_party/onnxruntime/bin/onnxruntime.dll`.

The build copies the DLL and detector model beside the executable under
`assets/models`. The application does not download either at runtime. The
detector session is initialized once at startup and one zero-input warm-up is
performed; no OCR work runs while the application is idle. Debug captures and
detector rectangles are written under `debug-captures` after a hotkey trigger.

## Offline item catalog phase

The application loads `assets/data/items_catalog.tsv` once at startup. It is a
read-only, offline catalog keyed by the stable Tarkov template ID. Each record
can expose Simplified Chinese and English full-name and short-name aliases;
  matching never replaces a localized field with an alias used for lookup.

The generated snapshot contains 5,441 game-template items and 21,762 localized
alias fields. `tools/catalog_generator/generate.py` uses
[`json.tarkov.dev/regular/items`](https://json.tarkov.dev/endpoints) as the
canonical stable-ID, dimensions, types, and caliber source. The companion
`items_en` and `items_zh` documents supply names by the same ID; a missing
Chinese field may be filled from [SPT `global/ch.json`](https://github.com/sp-tarkov/server-csharp/tree/main/Libraries/SPTarkov.Server.Assets/SPT_Data/database/locales/global)
or an optional legacy catalog. Missing Chinese never removes a canonical item.
One synthetic non-hex API ID (`customdogtags12345678910`) is reported in
metadata but excluded from the game's 24-hex template-ID catalog.

Regenerate with `python tools/catalog_generator/generate.py --output-tsv
assets/data/items_catalog.tsv --output-meta assets/data/items_catalog.meta.json`.
This is a development-only Python standard-library tool; the native executable
does not run Python or fetch catalog data while scanning. For reproducible
offline generation, pass `--canonical-json`, `--english-json`,
`--chinese-json`, and `--spt-json` local snapshots. `generated_at` is the newest
canonical item update timestamp, making unchanged inputs byte-for-byte stable.
The generator reports coverage, source/economy ID differences, and alias
collisions. `items_catalog.meta.json` is packaged with the TSV and logged at
startup. SPT's repository [license is CC BY-NC-SA 4.0](https://github.com/sp-tarkov/server-csharp/blob/main/LICENSE);
check all upstream data terms before redistributing refreshed snapshots.

Matching preserves the original UTF-8 OCR text and uses a conservative
normalization pass: full-width ASCII and spaces, common Chinese/Unicode
punctuation, whitespace collapsing, and English case folding. It then tries
raw exact aliases, normalized exact aliases, and finally code-point edit
distance with English token overlap. Chinese aliases are not rejected for
being one or two characters. Each recognized OCR box is matched independently;
  spatial candidate selection uses independent OCR boxes, configurable scanner
  profiles, directional priority, and expanding distance rings. Matching is
  completed before selection, so the selector never concatenates OCR text.

## Spatial scanner phase

The default Inventory profile anchors on the cursor and searches each ring in
the order upper-right, right, lower-right, down, lower-left, left, upper-left,
up. A RaidPickup profile anchors on the virtual-screen center and prioritizes
downward directions. Only boxes with an accepted catalog match are considered;
catalog confidence, OCR confidence, direction, ring distance, and spatial score
remain separate in debug output. The F2 worker reuses the existing captured ROI
for all directions and writes an annotated BMP with the anchor, rings, and
candidate boxes. Profile selection is available through the scanner API; the
application currently keeps Inventory as the default.

## Offline item economy phase

The economy layer maps internal modes to the current static JSON API at
`https://json.tarkov.dev`:

- PvP → `/regular/items`
- PvE → `/pve/items`
- Seasonal → `/pvp-season/items`

The application consumes each item's stable `id`, `width`, `height`,
`lastLowPrice`, `types`, and `updated` fields, plus the root `fleaMarket`
metadata. Current `/{{gameMode}}/items` snapshots expose `sellToTrader`:
each offer's `trader` ID and `priceRUB` represent a trader purchasing the
item, with non-RUB offers already converted to roubles. A background refresh
also loads `/{{gameMode}}/traders_en` to resolve trader nicknames by ID. The
highest valid purchasing offer is cached separately from `lastLowPrice`;
items without a valid offer remain unknown rather than substituting
`basePrice` or a trader's sale price. Both documents must validate before a
mode cache is replaced, and F2 still uses only the local in-memory cache.
Legacy `sellFor` / `traderPrices` fields remain parseable when present.

`ItemEconomyStore` keeps independent PVP, PVE, and Seasonal maps. It loads
normalized cache files from `data/economy-cache/{regular,pve,pvp-season}.json`
before starting the background refresh service. A refresh parses and validates
the complete response, writes a temporary file, atomically replaces the mode's
cache, and leaves the previous valid cache in place if download, parsing, or
validation fails. F2 lookups only read the in-memory mode map and never make a
network request. Refresh is performed once at startup and can be requested
manually through `DataRefreshService`; periodic refresh is not enabled.

The JSON parser is a small in-tree parser used only by the economy store, so no
browser, Python runtime, or additional package manager dependency is required.
The upstream endpoint catalog and mode names are documented by
[tarkov.dev's endpoint list](https://json.tarkov.dev/endpoints). The public
data is community-maintained; the application stores only the normalized
fields needed for offline lookup.

## Capture architecture

The flow is separated into:

```text
GlobalHotkey -> ScanTrigger -> cursor/ROI calculation -> ICaptureBackend
```

The first backend is DXGI Desktop Duplication. Windows Graphics Capture is
excellent for window/display capture, but arbitrary small desktop rectangles
would require additional Windows Runtime and Direct3D interop plumbing. DXGI
already exposes each monitor in physical virtual-screen coordinates and lets
the backend crop the requested ROI without coupling the scanner to DirectX.
The DXGI device, output duplication sessions, and BGRA8 staging textures are
initialized before the hotkey becomes active and reused for later captures.
If desktop duplication access is lost, the backend tears down and rebuilds the
session before retrying once.
Each active output also keeps a persistent full-frame GPU cache. A hotkey
capture crops from that cache when Desktop Duplication has no new frame, so a
static screen does not normally require GDI.
The backend remains behind `ICaptureBackend` so Windows Graphics Capture can
be benchmarked or substituted later. Supported non-BGRA8 frame formats use a
cached ROI-sized DirectX conversion pipeline to BGRA8. GDI is retained only as
an emergency fallback for unsupported formats or when DXGI has no new frame
available, so a static desktop still produces the requested debug image.

The application uses `SetProcessDpiAwarenessContext` with
`DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2`. Cursor and monitor coordinates
are therefore handled as physical virtual-screen coordinates, including
negative coordinates on monitors positioned to the left or above the primary
display.

Capture timing and output paths are sent to `OutputDebugStringW` and the
executable-local `debug-captures\capture.log`. No capture work is performed
while idle.

## Planned modules

Hideout provides a horizontally scrolling station selector with artwork and viewed levels,
upgrade requirements, mode-specific flea estimates (incomplete costs are explicitly marked),
and read-only crafting outputs, ingredients, tools and source durations. No player progress or profit calculation.
Regenerate structure with `python tools/hideout_generator/generate.py`.

Tasks loads generated local Tarkov.dev data for trader/task navigation, localized objectives,
task chains and completion rewards. Regular and PvE structures remain distinct; the UI is
read-only and does not infer player progression. Regenerate with `python tools/tasks_generator/generate.py`.
The current UI defaults to PvP without a Tasks mode selector.

- Scanner
- Prices (local catalog/economy browser, expandable flea-price history from tarkov.dev; rolling 30-day local cache, older ranges memory-only)
- Raid History
- Squad
- Map
- Tasks (local generated task browser)
- Hideout
- Events
- Recent Scans (local history implemented)
- Settings

Resolved Inventory scans are appended to `<exe>/data/recent-scans.json` as
UTF-8, versioned, scan-time price snapshots. The newest 200 entries are kept;
the Recent Scans page loads them once at startup and updates after each result.
OCR-only feedback and RaidPickup scans are not added. File writing runs off
the F2 result path and atomically replaces the previous valid file.

Recent Scans thumbnails use stable-ID icon assets from `assets.tarkov.dev`.
Download, disk reads and Windows WIC decoding run on a separate worker; cached
WebP files live in `<exe>/data/item-images/`. Offline/missing/undecodable images
keep a placeholder. Windows WebP codec support is required; no codec is installed
automatically. Images are cosmetic and never enter recognition or price resolution.

The source tree reserves directories for capture, scanning, OCR, matching,
data, overlay, UI, logs, and common utilities.

## Scanner design (planned)

### Inventory and stash

When the user presses a hotkey, the scanner will use the mouse position as an
anchor and search text regions clockwise:

1. upper-right
2. right
3. lower-right
4. down
5. lower-left
6. left
7. upper-left
8. up

If an entire ring fails, the capture radius will expand. Text detection comes
first, text recognition second, and catalog matching decides the final item.

### In-raid pickup

The scanner will prioritize the area below the crosshair for ground pickup
labels.

### Default item overlay fields

- item name
- flea market price
- highest trader buy price and trader
- price per slot
- flea-market eligibility

Item images will be optional and disabled by default.

## Data refresh design (planned)

- update once when the application starts;
- provide a manual refresh;
- optionally refresh on a configurable N-minute interval; and
- use local cached data for gameplay lookups so network requests do not block
  the lookup path.

## Local raid sessions — backend Phase 1 / 本地对局会话后端

Create `<exe>/data/eft-log-root.txt` with one absolute UTF-8 path to your EFT
`build/Logs` directory (or one client log directory). No configuration means
no log reading. Noven does not discover installations by scanning disks,
registry entries or game processes. Restart Noven after changing this file.

在可执行文件旁的 `data/eft-log-root.txt` 中填写一行 UTF-8 绝对日志目录路径。
未配置时不读取日志；更改配置后重启软件。不会扫描磁盘、注册表或游戏进程。

The native pipeline is `EftLogReader → RaidEventParser → RaidSessionDetector
→ RaidSessionStore`. A background worker merges timestamped application and
backend events, reads appended bytes and waits on Windows directory notifications
when idle. It ignores output/AI/error logs. Keep Noven's data directory outside
the watched EFT directory. Reader memory is bounded (16 KiB chunks, 64 KiB line
limit); oversized/invalid UTF-8 lines are skipped. Sources and history have
capacity limits and fail explicitly rather than silently deleting older history.

后台线程合并有时间戳的 application/backend 事件，只增量读取；空闲时等待目录通知。
不读取 output/AI/error 日志，不在渲染帧执行日志工作。Noven 数据目录必须放在被监视目录之外。
过长或非法 UTF-8 行跳过；达到容量限制时报告失败，不静默删除旧历史。

`GameStarted` activates a raid; `/client/match/local/end` completes it.
Loading alone creates no completed history. Positive Scav evidence may update
the role, but **outcome stays Unknown**. PMC, Practice/Offline, reconnects and
other results are not inferred. Only the verified `RezervBase` / Reserve aliases
currently resolve to an existing MapCatalog ID; other locations remain unresolved.
Cross-file late arrivals/reconnect recovery have not been live-EFT validated.

`GameStarted` 才激活对局，结束请求才完成。加载本身不生成完成记录。
Scav 正面证据只解析角色，结果始终保持 Unknown；不猜测 PMC、练习/离线模式、重连或生还结果。
当前仅已验证的储备站别名解析为现有地图 ID，其他地图保留未解析。
跨日志延迟写入和重连恢复尚未完成真实游戏验证。

`<exe>/data/raid-history.json` stores structured sessions, completeness,
parser/schema versions and file-identity/complete-line cursors atomically.
It never stores raw log text, copied EFT logs, server addresses or profile/account
IDs. Log timestamps are local wall-clock milliseconds, not UTC; missing times
and durations remain null. Corrupt/unsupported stores are left untouched and
disable writing; do not replace them with empty history. Only one Noven process
can own the store. These local files are excluded from Git. Raid History browses
completed session snapshots locally, with mode/role/map/local-date filters and
map/identity search. Unsupported role/outcome fields remain Unknown.

仅保存结构化会话、完整性、版本和完整行游标，不保存原文、日志副本、服务器或账户信息。
时间为日志本地墙钟毫秒，不冒充 UTC；缺失时间/时长用 null 表示。
文件损坏或版本不支持时保留原文件并停止写入；只允许单进程写入。
对局历史页在本地浏览已完成会话，支持模式/角色/地图/本地日期筛选及地图/身份搜索。
不支持的角色或结果仍保持未知，不计算生还率或对局收益。

New finalized scans optionally store the active Noven session ID. Existing scans
remain unassociated; no timestamp matching or historical backfilling is performed.
Raid details show exact-linked scan-time price snapshots retained in Recent Scans
(at most 200 records across all scans). Known scanned subtotals exclude missing
prices and are not total loot or raid profit. Saved raids remain browsable without
an EFT log path; reading new logs still requires explicit configuration.

新扫描仅在最终确认时附带可选的活动会话 ID，旧扫描不按时间补猜归属。
详情只显示最近扫描中保留的精确关联快照（所有扫描合计最多 200 条），价格为扫描时值。
已知扫描小计排除未知价格，不代表全部战利品或对局收益。
未配置日志路径也能浏览保存的对局；新增日志读取仍需显式配置。

## Events / 活动

启动时立即读取 `<exe>/data/events/event-catalog.json`，再由原生 WinHTTP 后台检查一次官方
英语 Telegram 公开页；不需要账号、令牌或游戏进程访问，没有永久轮询。
Startup loads this local cache immediately, then checks the official English Telegram public page once
using native WinHTTP. No account, token, game-process access or permanent polling is required.

原生活动页支持状态筛选和本地搜索，将官方信息、关联游戏内容与社区变更证据分别展示。
缺失的时间和范围仍为未知，原文不会自动翻译。地图、任务和物品使用已解析的精确身份跳转，
返回后保留选中、搜索、筛选及滚动位置。刷新失败继续显示有效缓存，不等同于“没有活动”。
可手动刷新，但不会并行重复检查。仅页面可见时按分钟检查本地状态时间，不触发网络轮询。
The native Events page provides local search and status filters, separating official facts, related content
and community change evidence. Missing timing/scope remains unknown; source content is not auto-translated.
Resolved map/task/item links navigate by exact identity and preserve selection, search, filters and scrolling
on return. Failed refreshes keep valid cached data, not a claim that no events exist. Manual refresh cannot
start parallel checks. A visible-page minute clock updates local status only, never polls the network.

活动身份采用官方消息 ID；公告发布时间不等于活动开始时间。只有明确的游戏内活动措辞进入
目录，维护、营销和普通新闻不作为活动。没有正式标题时保存原文首段摘录，不生成猜测名称。
目前只支持有限英语措辞以及完整 ISO 8601 时间；缺少年份/明确时区的自然语言日期保持未知。
相似标题、相近发布时间和配置变化均不能用来猜测活动存在或合并活动。
Identity uses official message IDs. Publication does not imply start time. Only supported explicit in-game
event wording creates records; news, maintenance and marketing do not. Without an official title, an original
leading excerpt is retained, not an invented name. The current adapter supports limited English wording and
complete ISO 8601 timestamps; incomplete natural-language dates remain unknown. Similar titles, nearby
timestamps and configuration changes never establish event existence or correlation.

DEV 补充层只复用本地已生成的物品/任务/地图及 mob 身份，完整名称精确匹配，歧义保持未解析。
Tarkov-Changes 仅在官方公告明确链接公开 `/view/<id>` 记录时补充键/旧值/新值；不从差异生成活动。
每次最多获取 4 个相关变更页，每页最多保留 16 项受支持变化；不自动回溯频道历史。
DEV enrichment reuses generated item/task/map/mob identities with exact full-name matching; ambiguity stays
unresolved. Tarkov-Changes adds supported key/old/new evidence only for explicitly linked public view records;
diffs cannot create events. At most four related change pages are fetched per refresh, with at most sixteen
supported changes per record. Channel history is not automatically backfilled.

目录最多保留最新 256 个活动，每个最多 32 项证据；单次响应最多 2 MiB，缓存最多 8 MiB。
原子缓存同时保存目录和来源游标，不保存原始 HTML、完整网页或私有数据。刷新失败不会清空旧
目录；损坏/不支持的缓存保留原文件并停止覆盖写入。HTML 来源结构可能改变，解析失败安全停止。
The catalog retains at most 256 latest events and 32 evidence entries per event, with 2 MiB response and
8 MiB cache limits. Atomic storage commits catalog and source state together, never raw HTML, full pages or
private data. Failed refreshes preserve valid data; corrupt/unsupported caches are left untouched and cannot
be overwritten. Public HTML structures may change; unsupported responses fail closed.

## Build

Configure and build from an x64 Native Tools command prompt or Developer
PowerShell with the MSVC environment initialized. Ninja uses the active MSVC
environment:

```powershell
cmake -S . -B build/debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/debug --config Debug

cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/release --config Release
```

The executable is generated at:

- `build/debug/NovenTarkovSupport.exe`
- `build/release/NovenTarkovSupport.exe`
