param([string]$SourceRoot,[string]$MainExe,[string]$HostExe,[string]$LauncherExe,[string]$UpdaterExe)
$ErrorActionPreference = 'Stop'
function Check([bool]$Condition,[string]$Message) { if (-not $Condition) { throw $Message } }
$iss = Get-Content -LiteralPath (Join-Path $SourceRoot 'installer/Noven.iss') -Encoding UTF8 -Raw
Check ($iss -match 'LanguageDetectionMethod=uilanguage' -and $iss -match 'ShowLanguageDialog=yes') 'Detect Windows UI language and allow explicit selection'
Check ($iss -match 'Name: "english"; MessagesFile: "compiler:Default.isl"' -and $iss -match 'Name: "chinesesimplified"; MessagesFile: "compiler:Languages\\ChineseSimplified.isl"') 'English and Simplified Chinese wizard translations'
$englishMessages=@{}; $chineseMessages=@{}
foreach ($line in ($iss -split "`n")) {
    if ($line -match '^english\.([^=]+)=(.*)') { $englishMessages[$Matches[1]]=$Matches[2] }
    if ($line -match '^chinesesimplified\.([^=]+)=(.*)') { $chineseMessages[$Matches[1]]=$Matches[2] }
}
Check ($englishMessages.Count -gt 0 -and $englishMessages.Count -eq $chineseMessages.Count) 'Custom installer language key counts'
foreach ($key in $englishMessages.Keys) {
    Check ($chineseMessages.ContainsKey($key) -and $chineseMessages[$key] -match '[\u4e00-\u9fff]') "Chinese installer translation: $key"
    $englishPlaceholders=([regex]::Matches($englishMessages[$key],'%\d+') | ForEach-Object Value) -join ','
    $chinesePlaceholders=([regex]::Matches($chineseMessages[$key],'%\d+') | ForEach-Object Value) -join ','
    Check ($englishPlaceholders -ceq $chinesePlaceholders) "Installer placeholder parity: $key"
}
foreach ($match in [regex]::Matches($iss,"CustomMessage\('([^']+)'\)")) { Check ($englishMessages.ContainsKey($match.Groups[1].Value)) 'Every custom prompt has both translations' }
Check ($iss -notmatch "(?:Result :=|RaiseException\()\s*'[^']+" -and $iss -match '\{cm:CreateDesktopIcon\}' -and $iss -match '\{cm:LaunchProgram,Noven Tarkov Support\}') 'No hardcoded installer prompts or shortcut labels'
Check ($iss -match 'AppId=\{\{70C934D2-C53B-4F49-A8C7-152E748A8E54\}' -and $iss -match 'PrivilegesRequired=lowest') 'Stable AppId / per-user contract'
Check ($iss -match 'CloseApplications=no' -and $iss -match 'AppMutex=Local\\NovenTarkovSupport.App' -and $iss -notmatch '\[UninstallDelete\]') 'No forced close or user-data cleanup'
Check ($iss -notmatch '(?i)(downloadtemporaryfile|urldownload|taskkill|powershell|cmd\.exe)' -and $iss -match 'Program and user data directories must not overlap') 'No updater/network/commands; no overlapping root'
Check ($iss -match 'DestDir: "\{app\}\\installer-staging' -and $iss -match "'--install-bundle'.*ewWaitUntilTerminated" -and $iss -match 'if Code <> 0 then RaiseException') 'Installer must verify staged Core before activation'
Check ($iss -match 'A newer Noven bootstrap is installed' -and $iss -match 'Flags: replacesameversion' -and $iss -notmatch 'current.json.*DestDir') 'Bootstrap downgrade protection; preserve activation metadata'
foreach($line in ($iss -split "`n" | Where-Object {$_ -match '^Name:.*Filename:'})) {
    Check ($line -match 'Filename: "\{app\}\\NovenLauncher.exe"') 'All shortcuts target Launcher only'
}
Check ($iss -match '(?m)^Filename: "\{app\}\\NovenLauncher.exe";.*postinstall' -and $iss -notmatch '(?m)^AppPublisher=') 'Launcher post-install entry; no invented publisher'
$runtime=Get-Content -LiteralPath (Join-Path $SourceRoot 'src/plugins/PluginRuntimeManager.cpp') -Raw
Check ($runtime -match 'host\(paths.programRoot/L"NovenPluginHost.exe"\)') 'PluginHost must resolve from active runtime directory'
foreach ($name in @('package_release.ps1','audit_payload.ps1','prepare_update_review.ps1')) {
    $errors=$null;[System.Management.Automation.Language.Parser]::ParseFile((Join-Path $SourceRoot "scripts/$name"),[ref]$null,[ref]$errors)|Out-Null
    Check (-not $errors) 'Script syntax'
}
foreach ($name in @('package_release.ps1','prepare_update_review.ps1')) {
    $script=Get-Content -LiteralPath (Join-Path $SourceRoot "scripts/$name") -Raw
    Check ($script -match '(?m)Start-Process[^\r\n]*NovenUpdater.exe[^\r\n]*--seed[^\r\n]*-Wait[^\r\n]*-PassThru' -and $script -match 'seedProcess.ExitCode -ne 0') 'Win32 inventory completion must precede audit'
}
$root = Join-Path ([IO.Path]::GetTempPath()) ('noven-payload-test-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root | Out-Null
try {
    Copy-Item -LiteralPath $MainExe -Destination (Join-Path $root 'NovenTarkovSupport.exe')
    Copy-Item -LiteralPath $HostExe -Destination (Join-Path $root 'NovenPluginHost.exe')
    $version=(Get-Item -LiteralPath $MainExe).VersionInfo.ProductVersion
    foreach ($file in @('onnxruntime.dll','noven-installed.layout','NOTICE.md','docs/MAP_ATTRIBUTION.md','docs/LICENSE.onnxruntime.txt','docs/maps/interchange/SOURCE.md','docs/maps/icons/SOURCE.md','docs/maps/icons/LICENSE.tarkov-dev.txt','assets/models/ppocrv5_mobile_det.onnx','assets/models/ppocrv5_mobile_rec.onnx','assets/models/ppocrv5_mobile_rec_dict.txt','assets/data/items_catalog.tsv','assets/data/task_tasks.tsv','assets/data/map_maps.tsv','assets/data/hideout_stations.tsv','assets/i18n/zh-CN.json','assets/i18n/en-US.json')) {
        $path=Join-Path $root $file;New-Item -ItemType Directory -Path (Split-Path -Parent $path) -Force | Out-Null;[IO.File]::WriteAllText($path,'bounded test placeholder')
    }
    foreach ($file in @('licenses/README.md','licenses/onnxruntime/LICENSE','licenses/onnxruntime/ThirdPartyNotices.txt','licenses/paddleocr/LICENSE-2.0.txt')) {
        $path=Join-Path $root $file;New-Item -ItemType Directory -Path (Split-Path -Parent $path) -Force | Out-Null
        $source=if($file.StartsWith('licenses/onnxruntime/')) { Join-Path $SourceRoot ('third_party/onnxruntime/' + (Split-Path -Leaf $file)) } else { Join-Path $SourceRoot $file }
        Copy-Item -LiteralPath $source -Destination $path
    }
    Check ((Get-FileHash -LiteralPath (Join-Path $root 'licenses/onnxruntime/ThirdPartyNotices.txt')).Hash -eq '143764B952FDB1A7C69CE653BFBA74A7744D6A8A573BFB73E235FBA356C83DE3') 'Exact ONNX v1.30.0 notice'
    Check ((Get-FileHash -LiteralPath (Join-Path $root 'licenses/paddleocr/LICENSE-2.0.txt')).Hash -eq 'CFC7749B96F63BD31C3C42B5C471BF756814053E847C10F3EB003417BC523D30') 'Complete official Apache 2.0 license'
    # Core 保留身份表而不含地图图片；测试不复制地图、不写真实 User Data。
    # Core retains identity tables without imagery; tests never copy maps or write real User Data.
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
    foreach ($bad in @('demo.dll','extra.pdb','test.exe','CMakeCache.txt','plugins/test/manifest.json','data/settings.json','logs/run.log','assets/data/plugin-registry-review.json','assets/maps/sources/unreferenced.png','assets/maps/test/floor.png','assets/maps/test/floor.tiles','assets/maps/map.json')) {
        $path=Join-Path $root $bad;New-Item -ItemType Directory -Path (Split-Path -Parent $path) -Force | Out-Null;[IO.File]::WriteAllText($path,'unexpected');$denied=$false
        try { & (Join-Path $SourceRoot 'scripts/audit_payload.ps1') -Payload $root -Version $version | Out-Null } catch { $denied=$true }
        Check $denied "Audit accepted $bad";Remove-Item -LiteralPath $path
        if ($bad -match '^(plugins|data|logs)/') { Remove-Item -LiteralPath (Join-Path $root $Matches[1]) -Recurse -Force }
        if ($bad.StartsWith('assets/maps/')) { Remove-Item -LiteralPath (Join-Path $root 'assets/maps') -Recurse -Force }
        & (Join-Path $SourceRoot 'scripts/audit_payload.ps1') -Payload $root -Version $version | Out-Null
    }
    foreach ($file in @('assets/models/ppocrv5_mobile_det.onnx','assets/models/ppocrv5_mobile_rec.onnx','assets/models/ppocrv5_mobile_rec_dict.txt','assets/data/map_floors.tsv')) {
        $path=Join-Path $root $file;Rename-Item -LiteralPath $path -NewName 'dependency.saved';$denied=$false
        try { & (Join-Path $SourceRoot 'scripts/audit_payload.ps1') -Payload $root -Version $version | Out-Null } catch { $denied=$true }
        Check $denied "Missing dependency rejected: $file"
        Rename-Item -LiteralPath (Join-Path (Split-Path -Parent $path) 'dependency.saved') -NewName (Split-Path -Leaf $path)
    }
    $children=@(Get-ChildItem -LiteralPath $root -Force)
    $bootstrap=Join-Path $root 'versioned'
    $payload=Join-Path $bootstrap "versions/$version"
    New-Item -ItemType Directory -Path $payload -Force | Out-Null
    $children | Copy-Item -Destination $payload -Recurse
    Copy-Item -LiteralPath $LauncherExe -Destination (Join-Path $bootstrap 'NovenLauncher.exe')
    Copy-Item -LiteralPath $UpdaterExe -Destination (Join-Path $bootstrap 'NovenUpdater.exe')
    $inventory=@(Get-ChildItem -LiteralPath $payload -File -Recurse | ForEach-Object {
        @{path=$_.FullName.Substring($payload.Length+1).Replace('\','/');size=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName).Hash.ToLowerInvariant()}
    })
    $initial=@{schemaVersion=1;version=$version;files=$inventory} | ConvertTo-Json -Depth 5
    $current=@{schemaVersion=1;active=$version;previous='';pending=$false;attempts=0;token=''} | ConvertTo-Json
    [IO.File]::WriteAllText((Join-Path $bootstrap 'initial.json'),$initial)
    [IO.File]::WriteAllText((Join-Path $bootstrap 'current.json'),$current)
    [IO.File]::WriteAllText((Join-Path $bootstrap 'last-good.json'),(@{version=$version} | ConvertTo-Json))
    & (Join-Path $SourceRoot 'scripts/audit_payload.ps1') -Payload $bootstrap -Version $version
    [IO.File]::WriteAllText((Join-Path $bootstrap 'current.json'),$current.Replace($version,'../escape'))
    $denied=$false;try { & (Join-Path $SourceRoot 'scripts/audit_payload.ps1') -Payload $bootstrap -Version $version | Out-Null } catch {$denied=$true}
    Check $denied 'Unsafe activation identity rejected'
    [IO.File]::WriteAllText((Join-Path $bootstrap 'current.json'),$current)
    [IO.File]::WriteAllText((Join-Path $payload 'NOTICE.md'),'tampered')
    $denied=$false;try { & (Join-Path $SourceRoot 'scripts/audit_payload.ps1') -Payload $bootstrap -Version $version | Out-Null } catch {$denied=$true}
    Check $denied 'Initial inventory hash mismatch rejected'
    $stage=Join-Path $bootstrap "installer-staging/$version"
    New-Item -ItemType Directory -Path $stage -Force | Out-Null
    $children | Copy-Item -Destination $stage -Recurse
    [IO.File]::WriteAllText((Join-Path $bootstrap 'installer-staging/inventory.json'),$initial)
    $repair=Start-Process -FilePath (Join-Path $bootstrap 'NovenUpdater.exe') -ArgumentList '--install-bundle' -WindowStyle Hidden -PassThru
    Check ($repair.WaitForExit(15000) -and $repair.ExitCode -eq 0) 'Real production Updater must commit the installer bundle'
    Check ((Get-Content -LiteralPath (Join-Path $bootstrap 'current.json') -Raw) -ceq $current) 'Native repair preserves activation bytes'
    Check ((Get-Item -LiteralPath (Join-Path $payload 'NovenTarkovSupport.exe')).VersionInfo.ProductVersion -eq $version -and (Test-Path -LiteralPath (Join-Path $payload 'NovenPluginHost.exe'))) 'Native installer restores Main and version-local Host'
    Check (-not(Test-Path -LiteralPath (Join-Path $bootstrap 'installer-staging')) -and (Test-Path -LiteralPath (Join-Path $bootstrap "installer-releases/$version.json"))) 'Native transaction cleanup and trusted receipt'
    $uninstall=Start-Process -FilePath (Join-Path $bootstrap 'NovenUpdater.exe') -ArgumentList '--remove-versions' -WindowStyle Hidden -PassThru
    Check ($uninstall.WaitForExit(15000) -and $uninstall.ExitCode -eq 0) 'Real production Updater uninstall cleanup'
    Check (-not(Test-Path -LiteralPath $payload) -and -not(Test-Path -LiteralPath (Join-Path $bootstrap 'current.json'))) 'Native uninstall removes versions and activation only'
    Write-Output 'Packaging contract / versioned bootstrap / activation integrity / artifact rejection PASS'
} finally {
    # 测试创建的 GUID 临时子目录，不触碰真实 Known Folder。
    # Only the GUID temp child created by this test, never real Known Folders.
    if ([IO.Path]::GetFullPath($root).StartsWith([IO.Path]::GetFullPath([IO.Path]::GetTempPath()),[StringComparison]::OrdinalIgnoreCase)) { Remove-Item -LiteralPath $root -Recurse -Force }
}
