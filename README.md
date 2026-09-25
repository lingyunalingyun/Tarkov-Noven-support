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
metadata. When a compatible upstream payload exposes `sellFor` or
`traderPrices`, valid non-flea offers are reduced to the highest trader value;
the current static items endpoint does not consistently expose those fields,
so missing trader values remain unknown rather than using `basePrice` as a
substitute. `lastLowPrice` is retained as the current useful flea-market value
reported by the upstream snapshot.

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

- Scanner
- Prices
- Raid History
- Squad
- Map
- Tasks
- Hideout
- Events
- Recent Scans
- Settings

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
