param([Parameter(Mandatory)][string]$Payload,[Parameter(Mandatory)][string]$Version)
$ErrorActionPreference = 'Stop'
$payloadRoot = (Resolve-Path -LiteralPath $Payload).Path
if ((Get-Item -LiteralPath $payloadRoot -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Unsafe payload root.' }
$required = @('NovenTarkovSupport.exe','NovenPluginHost.exe','onnxruntime.dll','noven-installed.layout','NOTICE.md','docs/MAP_ATTRIBUTION.md','docs/LICENSE.onnxruntime.txt','assets/maps/interchange/SOURCE.md','assets/maps/icons/SOURCE.md','assets/maps/icons/LICENSE.tarkov-dev.txt','assets/models/ppocrv5_mobile_det.onnx','assets/models/ppocrv5_mobile_rec.onnx','assets/models/ppocrv5_mobile_rec_dict.txt','assets/data/items_catalog.tsv','assets/data/task_tasks.tsv','assets/data/map_maps.tsv','assets/data/hideout_stations.tsv','assets/i18n/zh-CN.json','assets/i18n/en-US.json')
foreach ($name in $required) { if (-not (Test-Path -LiteralPath (Join-Path $payloadRoot $name) -PathType Leaf)) { throw "Missing runtime payload: $name" } }
$legal = @('licenses/README.md','licenses/onnxruntime/LICENSE','licenses/onnxruntime/ThirdPartyNotices.txt','licenses/paddleocr/LICENSE-2.0.txt')
foreach ($name in $legal) { if (-not (Test-Path -LiteralPath (Join-Path $payloadRoot $name) -PathType Leaf)) { throw "Missing legal file: $name" } }
$required += $legal
# 原始源图只保留生产表引用；同时确认所有重拼输入和缩放包存在。
# Raw sources must be production-table dependencies; verify composition inputs and zoom packs too.
$sourcePaths = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
foreach ($table in @('map_compositions.tsv','map_update_assets.tsv','map_floors.tsv','pve/map_floors.tsv')) {
    $path=Join-Path $payloadRoot "assets/data/$table"
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing runtime map table: $table" }
    foreach ($row in (Import-Csv -LiteralPath $path -Delimiter "`t" -Encoding UTF8)) {
        $paths = switch ($table) {
            'map_compositions.tsv' { @($row.sourcePath,$row.outputPath) }
            'map_update_assets.tsv' { @($row.relativePath) }
            default { @($row.abstractPath,$row.satellitePath) }
        }
        foreach ($relative in $paths) {
            if (-not $relative) { continue }
            if ($relative -notmatch '^maps/[a-zA-Z0-9_./-]+\.png$' -or $relative.Contains('..')) { throw "Unsafe runtime map path: $relative" }
            if (-not (Test-Path -LiteralPath (Join-Path $payloadRoot "assets/$relative") -PathType Leaf)) { throw "Missing runtime map dependency: $relative" }
            if ($relative.StartsWith('maps/sources/',[StringComparison]::Ordinal)) { [void]$sourcePaths.Add("assets/$relative") }
        }
        if ($table -match 'map_floors.tsv$') {
            foreach ($relative in @($row.abstractPath,$row.satellitePath)) {
                if ($relative -and -not (Test-Path -LiteralPath (Join-Path $payloadRoot ('assets/' + [IO.Path]::ChangeExtension($relative,'.tiles'))) -PathType Leaf)) { throw "Missing map zoom pack: $relative" }
            }
        }
    }
}
$files = @(Get-ChildItem -LiteralPath $payloadRoot -Recurse -Force)
foreach ($file in $files) {
    if ($file.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Reparse payload: $($file.Name)" }
    $relative = $file.FullName.Substring($payloadRoot.Length + 1).Replace('\','/')
    if ($file.PSIsContainer) {
        if ($relative -notmatch '^(docs|licenses|licenses/(onnxruntime|paddleocr)|assets|assets/(data|maps|models|i18n)(/[a-zA-Z0-9_.-]+)*)$') { throw "Unexpected payload directory: $relative" }
        continue
    }
    if ($file.Length -eq 0) { throw "Empty runtime file: $relative" }
    if ($relative.StartsWith('assets/maps/sources/',[StringComparison]::Ordinal) -and -not $sourcePaths.Contains($relative)) { throw "Unreferenced runtime source: $relative" }
    $allowed = $required -contains $relative
    $allowed = $allowed -or ($relative -match '^assets/data/[a-zA-Z0-9_./-]+\.(tsv|json)$') -or ($relative -match '^assets/maps/[a-zA-Z0-9_./-]+\.(png|svg|json|tiles)$') -or ($relative -match '^assets/i18n/[a-zA-Z-]+\.json$')
    if (-not $allowed -or $relative -match '(?i)(fixture|test[.-]registry|plugin-registry-review|marketplace[-.]cache|plugin-state|(^|/)\.git)') { throw "Unexpected payload file: $relative" }
}
foreach ($exe in @('NovenTarkovSupport.exe','NovenPluginHost.exe')) {
    $info = (Get-Item -LiteralPath (Join-Path $payloadRoot $exe)).VersionInfo
    if ($info.ProductVersion -ne $Version -or $info.FileVersion -ne "$Version.0" -or $info.ProductName -ne 'Noven Tarkov Support') { throw "Version metadata mismatch: $exe" }
}
Write-Output "Payload PASS: $($files.Where({-not $_.PSIsContainer}).Count) files; no tests, demo DLLs, caches, user data or build artifacts."
