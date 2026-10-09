param([Parameter(Mandatory)][string]$Payload,[Parameter(Mandatory)][string]$Version)
$ErrorActionPreference = 'Stop'
$payloadRoot = (Resolve-Path -LiteralPath $Payload).Path
if ((Get-Item -LiteralPath $payloadRoot -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Unsafe payload root.' }
$required = @('NovenTarkovSupport.exe','NovenPluginHost.exe','onnxruntime.dll','noven-installed.layout','NOTICE.md','docs/MAP_ATTRIBUTION.md','docs/LICENSE.onnxruntime.txt','assets/maps/interchange/SOURCE.md','assets/maps/icons/SOURCE.md','assets/maps/icons/LICENSE.tarkov-dev.txt','assets/models/ppocrv5_mobile_det.onnx','assets/models/ppocrv5_mobile_rec.onnx','assets/models/ppocrv5_mobile_rec_dict.txt','assets/data/items_catalog.tsv','assets/data/task_tasks.tsv','assets/data/map_maps.tsv','assets/data/hideout_stations.tsv','assets/i18n/zh-CN.json','assets/i18n/en-US.json')
foreach ($name in $required) { if (-not (Test-Path -LiteralPath (Join-Path $payloadRoot $name) -PathType Leaf)) { throw "Missing runtime payload: $name" } }
$files = @(Get-ChildItem -LiteralPath $payloadRoot -Recurse -Force)
foreach ($file in $files) {
    if ($file.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Reparse payload: $($file.Name)" }
    $relative = $file.FullName.Substring($payloadRoot.Length + 1).Replace('\','/')
    if ($file.PSIsContainer) {
        if ($relative -notmatch '^(docs|assets|assets/(data|maps|models|i18n)(/[a-zA-Z0-9_.-]+)*)$') { throw "Unexpected payload directory: $relative" }
        continue
    }
    if ($file.Length -eq 0) { throw "Empty runtime file: $relative" }
    $allowed = $required -contains $relative
    $allowed = $allowed -or ($relative -match '^assets/data/[a-zA-Z0-9_./-]+\.(tsv|json)$') -or ($relative -match '^assets/maps/[a-zA-Z0-9_./-]+\.(png|svg|json|tiles)$') -or ($relative -match '^assets/i18n/[a-zA-Z-]+\.json$')
    if (-not $allowed -or $relative -match '(?i)(fixture|test[.-]registry|plugin-registry-review|marketplace[-.]cache|plugin-state|(^|/)\.git)') { throw "Unexpected payload file: $relative" }
}
foreach ($exe in @('NovenTarkovSupport.exe','NovenPluginHost.exe')) {
    $info = (Get-Item -LiteralPath (Join-Path $payloadRoot $exe)).VersionInfo
    if ($info.ProductVersion -ne $Version -or $info.FileVersion -ne "$Version.0" -or $info.ProductName -ne 'Noven Tarkov Support') { throw "Version metadata mismatch: $exe" }
}
Write-Output "Payload PASS: $($files.Where({-not $_.PSIsContainer}).Count) files; no tests, demo DLLs, caches, user data or build artifacts."
