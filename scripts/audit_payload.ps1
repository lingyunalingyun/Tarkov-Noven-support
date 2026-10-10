param([Parameter(Mandatory)][string]$Payload,[Parameter(Mandatory)][string]$Version)
$ErrorActionPreference = 'Stop'
$payloadRoot = (Resolve-Path -LiteralPath $Payload).Path
if ((Get-Item -LiteralPath $payloadRoot -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Unsafe payload root.' }
$required = @('NovenTarkovSupport.exe','NovenPluginHost.exe','onnxruntime.dll','noven-installed.layout','NOTICE.md','docs/MAP_ATTRIBUTION.md','docs/LICENSE.onnxruntime.txt','docs/maps/interchange/SOURCE.md','docs/maps/icons/SOURCE.md','docs/maps/icons/LICENSE.tarkov-dev.txt','assets/models/ppocrv5_mobile_det.onnx','assets/models/ppocrv5_mobile_rec.onnx','assets/models/ppocrv5_mobile_rec_dict.txt','assets/data/items_catalog.tsv','assets/data/task_tasks.tsv','assets/data/map_maps.tsv','assets/data/hideout_stations.tsv','assets/i18n/zh-CN.json','assets/i18n/en-US.json')
foreach ($name in $required) { if (-not (Test-Path -LiteralPath (Join-Path $payloadRoot $name) -PathType Leaf)) { throw "Missing runtime payload: $name" } }
$legal = @('licenses/README.md','licenses/onnxruntime/LICENSE','licenses/onnxruntime/ThirdPartyNotices.txt','licenses/paddleocr/LICENSE-2.0.txt')
foreach ($name in $legal) { if (-not (Test-Path -LiteralPath (Join-Path $payloadRoot $name) -PathType Leaf)) { throw "Missing legal file: $name" } }
$required += $legal
# Core 不含任何可选地图目录，静态身份表仍必须存在。
# Core has no optional map directory; static identity tables remain required.
foreach ($table in @('map_compositions.tsv','map_update_assets.tsv','map_floors.tsv','pve/map_floors.tsv')) {
    if (-not (Test-Path -LiteralPath (Join-Path $payloadRoot "assets/data/$table") -PathType Leaf)) { throw "Missing map catalog table: $table" }
}
$files = @(Get-ChildItem -LiteralPath $payloadRoot -Recurse -Force)
foreach ($file in $files) {
    if ($file.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Reparse payload: $($file.Name)" }
    $relative = $file.FullName.Substring($payloadRoot.Length + 1).Replace('\','/')
    if ($file.PSIsContainer) {
        if ($relative -notmatch '^(docs|docs/maps|docs/maps/(icons|interchange)|licenses|licenses/(onnxruntime|paddleocr)|assets|assets/(data|models|i18n)(/[a-zA-Z0-9_.-]+)*)$') { throw "Unexpected payload directory: $relative" }
        continue
    }
    if ($file.Length -eq 0) { throw "Empty runtime file: $relative" }
    $allowed = $required -contains $relative
    $allowed = $allowed -or ($relative -match '^assets/data/[a-zA-Z0-9_./-]+\.(tsv|json)$') -or ($relative -match '^assets/i18n/[a-zA-Z-]+\.json$')
    if (-not $allowed -or $relative -match '(?i)(fixture|test[.-]registry|plugin-registry-review|marketplace[-.]cache|plugin-state|(^|/)\.git)') { throw "Unexpected payload file: $relative" }
}
foreach ($exe in @('NovenTarkovSupport.exe','NovenPluginHost.exe')) {
    $info = (Get-Item -LiteralPath (Join-Path $payloadRoot $exe)).VersionInfo
    if ($info.ProductVersion -ne $Version -or $info.FileVersion -ne "$Version.0" -or $info.ProductName -ne 'Noven Tarkov Support') { throw "Version metadata mismatch: $exe" }
}
Write-Output "Payload PASS: $($files.Where({-not $_.PSIsContainer}).Count) files; no tests, demo DLLs, caches, user data or build artifacts."
