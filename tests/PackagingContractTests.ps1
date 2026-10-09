param([string]$SourceRoot,[string]$MainExe,[string]$HostExe)
$ErrorActionPreference = 'Stop'
function Check([bool]$Condition,[string]$Message) { if (-not $Condition) { throw $Message } }
$iss = Get-Content -LiteralPath (Join-Path $SourceRoot 'installer/Noven.iss') -Raw
Check ($iss -match 'AppId=\{\{70C934D2-C53B-4F49-A8C7-152E748A8E54\}' -and $iss -match 'PrivilegesRequired=lowest') 'Stable AppId / per-user contract'
Check ($iss -match 'CloseApplications=no' -and $iss -match 'AppMutex=Local\\NovenTarkovSupport.App' -and $iss -notmatch '\[UninstallDelete\]') 'No forced close or user-data cleanup'
Check ($iss -notmatch '(?i)(downloadtemporaryfile|urldownload|taskkill|powershell|cmd\.exe)' -and $iss -match 'Program and user data directories must not overlap') 'No updater/network/commands; no overlapping root'
foreach ($name in @('package_release.ps1','audit_payload.ps1')) {
    $errors=$null;[System.Management.Automation.Language.Parser]::ParseFile((Join-Path $SourceRoot "scripts/$name"),[ref]$null,[ref]$errors)|Out-Null
    Check (-not $errors) 'Script syntax'
}
$root = Join-Path ([IO.Path]::GetTempPath()) ('noven-payload-test-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root | Out-Null
try {
    Copy-Item -LiteralPath $MainExe -Destination (Join-Path $root 'NovenTarkovSupport.exe')
    Copy-Item -LiteralPath $HostExe -Destination (Join-Path $root 'NovenPluginHost.exe')
    $version=(Get-Item -LiteralPath $MainExe).VersionInfo.ProductVersion
    foreach ($file in @('onnxruntime.dll','noven-installed.layout','NOTICE.md','docs/MAP_ATTRIBUTION.md','docs/LICENSE.onnxruntime.txt','assets/maps/interchange/SOURCE.md','assets/maps/icons/SOURCE.md','assets/maps/icons/LICENSE.tarkov-dev.txt','assets/models/ppocrv5_mobile_det.onnx','assets/models/ppocrv5_mobile_rec.onnx','assets/models/ppocrv5_mobile_rec_dict.txt','assets/data/items_catalog.tsv','assets/data/task_tasks.tsv','assets/data/map_maps.tsv','assets/data/hideout_stations.tsv','assets/i18n/zh-CN.json','assets/i18n/en-US.json')) {
        $path=Join-Path $root $file;New-Item -ItemType Directory -Path (Split-Path -Parent $path) -Force | Out-Null;[IO.File]::WriteAllText($path,'bounded test placeholder')
    }
    foreach ($file in @('licenses/README.md','licenses/onnxruntime/LICENSE','licenses/onnxruntime/ThirdPartyNotices.txt','licenses/paddleocr/LICENSE-2.0.txt')) {
        $path=Join-Path $root $file;New-Item -ItemType Directory -Path (Split-Path -Parent $path) -Force | Out-Null
        $source=if($file.StartsWith('licenses/onnxruntime/')) { Join-Path $SourceRoot ('third_party/onnxruntime/' + (Split-Path -Leaf $file)) } else { Join-Path $SourceRoot $file }
        Copy-Item -LiteralPath $source -Destination $path
    }
    Check ((Get-FileHash -LiteralPath (Join-Path $root 'licenses/onnxruntime/ThirdPartyNotices.txt')).Hash -eq '143764B952FDB1A7C69CE653BFBA74A7744D6A8A573BFB73E235FBA356C83DE3') 'Exact ONNX v1.30.0 notice'
    Check ((Get-FileHash -LiteralPath (Join-Path $root 'licenses/paddleocr/LICENSE-2.0.txt')).Hash -eq 'CFC7749B96F63BD31C3C42B5C471BF756814053E847C10F3EB003417BC523D30') 'Complete official Apache 2.0 license'
    # 小型依赖闭包夹具，不复制全地图、不写真实 User Data。
    # A small dependency-closure fixture, not all maps or real user data.
    foreach ($file in @('assets/maps/sources/aa.png','assets/maps/test/floor.png','assets/maps/test/floor.tiles')) {
        $path=Join-Path $root $file;New-Item -ItemType Directory -Path (Split-Path -Parent $path) -Force | Out-Null;[IO.File]::WriteAllText($path,'runtime fixture')
    }
    [IO.File]::WriteAllText((Join-Path $root 'assets/data/map_compositions.tsv'),"outputPath`tsourcePath`nmaps/test/floor.png`tmaps/sources/aa.png`n")
    [IO.File]::WriteAllText((Join-Path $root 'assets/data/map_update_assets.tsv'),"relativePath`turl`tsha256`nmaps/sources/aa.png`thttps://assets.tarkov.dev/maps/test.png`t`n")
    foreach ($table in @('map_floors.tsv','pve/map_floors.tsv')) {
        $path=Join-Path $root "assets/data/$table";New-Item -ItemType Directory -Path (Split-Path -Parent $path) -Force | Out-Null
        [IO.File]::WriteAllText($path,"mapId`tfloorId`tabstractPath`tsatellitePath`nmap1`tfloor`tmaps/test/floor.png`t`n")
    }
    & (Join-Path $SourceRoot 'scripts/audit_payload.ps1') -Payload $root -Version $version
    $notice=Join-Path $root 'licenses/onnxruntime/ThirdPartyNotices.txt'
    Rename-Item -LiteralPath $notice -NewName 'notice.saved'
    $denied=$false
    try { & (Join-Path $SourceRoot 'scripts/audit_payload.ps1') -Payload $root -Version $version | Out-Null } catch { $denied=$true }
    Check $denied 'Missing legal notice rejected'
    Rename-Item -LiteralPath (Join-Path $root 'licenses/onnxruntime/notice.saved') -NewName 'ThirdPartyNotices.txt'
    foreach ($bad in @('demo.dll','extra.pdb','test.exe','CMakeCache.txt','plugins/test/manifest.json','data/settings.json','logs/run.log','assets/data/plugin-registry-review.json','assets/maps/sources/unreferenced.png')) {
        $path=Join-Path $root $bad;New-Item -ItemType Directory -Path (Split-Path -Parent $path) -Force | Out-Null;[IO.File]::WriteAllText($path,'unexpected');$denied=$false
        try { & (Join-Path $SourceRoot 'scripts/audit_payload.ps1') -Payload $root -Version $version | Out-Null } catch { $denied=$true }
        Check $denied "Audit accepted $bad";Remove-Item -LiteralPath $path
        if ($bad -match '^(plugins|data|logs)/') { Remove-Item -LiteralPath (Join-Path $root $Matches[1]) -Recurse -Force }
        & (Join-Path $SourceRoot 'scripts/audit_payload.ps1') -Payload $root -Version $version | Out-Null
    }
    foreach ($file in @('assets/maps/sources/aa.png','assets/maps/test/floor.png','assets/maps/test/floor.tiles')) {
        $path=Join-Path $root $file;Rename-Item -LiteralPath $path -NewName 'dependency.saved';$denied=$false
        try { & (Join-Path $SourceRoot 'scripts/audit_payload.ps1') -Payload $root -Version $version | Out-Null } catch { $denied=$true }
        Check $denied "Missing dependency rejected: $file"
        Rename-Item -LiteralPath (Join-Path (Split-Path -Parent $path) 'dependency.saved') -NewName (Split-Path -Leaf $path)
    }
    Write-Output 'Packaging contract / metadata / artifact rejection PASS'
} finally {
    # 测试创建的 GUID 临时子目录，不触碰真实 Known Folder。
    # Only the GUID temp child created by this test, never real Known Folders.
    if ([IO.Path]::GetFullPath($root).StartsWith([IO.Path]::GetFullPath([IO.Path]::GetTempPath()),[StringComparison]::OrdinalIgnoreCase)) { Remove-Item -LiteralPath $root -Recurse -Force }
}
