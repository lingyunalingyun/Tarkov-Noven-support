# Noven Tarkov Support

Noven Tarkov Support is a lightweight native Windows companion application
for Escape from Tarkov. The initial version is only a project skeleton with a
minimal Unicode Win32 window; game features will be added in later phases.

## Technology stack

- C++23
- Native Win32 APIs
- CMake
- MSVC on Windows x64
- Future OCR: ONNX Runtime
- Future screen capture: Windows Graphics Capture and/or DXGI Desktop Duplication

The project intentionally has no Electron, Tauri, WebView, .NET, Python,
PyTorch, or large third-party dependency at this stage.

## Product and safety boundary

The application will only process information already visible to the player,
local public logs, or public external data. It will not:

- read game memory;
- inject into Escape from Tarkov;
- inspect network packets;
- access hidden game information; or
- perform automatic gameplay actions.

## Current status

The current phase adds only the native capture trigger foundation:

- global F2 hotkey;
- current cursor anchor in physical virtual-screen coordinates;
- configurable 800 x 600 ROI calculation with desktop-boundary clipping;
- one capture per hotkey press; and
- one debug BMP written under `debug-captures` next to the executable.

OCR, price APIs, maps, log parsing, account synchronization, overlay result
cards, and game-specific logic are not implemented yet.

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
The backend remains behind `ICaptureBackend` so Windows Graphics Capture can
be benchmarked or substituted later. On HDR outputs or when DXGI has no new
frame available, the backend uses a one-shot native GDI compatibility capture
so a static desktop still produces the requested debug image.

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
