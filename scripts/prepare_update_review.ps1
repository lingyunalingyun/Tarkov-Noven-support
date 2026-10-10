param(
    [Parameter(Mandatory)][string]$CorePayload,
    [Parameter(Mandatory)][string]$ResourceFixture,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [Parameter(Mandatory)][string]$ReviewPublicKey,
    [Parameter(Mandatory)][string]$ReviewPrivateKey,
    [string]$CMake='cmake'
)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
$output=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path -LiteralPath $output) {throw 'Offline review output must be a new directory.'}
$core=(Resolve-Path -LiteralPath $CorePayload).Path
& (Join-Path $PSScriptRoot 'audit_payload.ps1') -Payload $core -Version '0.1.0'
if(Test-Path -LiteralPath (Join-Path $core 'versions')) { $core=Join-Path $core 'versions/0.1.0' }
$fixture=(Resolve-Path -LiteralPath $ResourceFixture).Path
foreach($file in Get-ChildItem -LiteralPath $fixture -Recurse -Force) {
    if($file.PSIsContainer -or ($file.Attributes -band [IO.FileAttributes]::ReparsePoint) -or $file.Length -gt 1MB -or $file.Extension -notin @('.json','.nvr')) {throw 'Only small synthetic resource fixtures are accepted.'}
}
$public=[IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $ReviewPublicKey).Path)
if($public.Length -ne 72) {throw 'Expected a review-only CNG P-256 public blob.'}
$hex=($public | ForEach-Object {$_.ToString('x2')}) -join ''
$private=(Resolve-Path -LiteralPath $ReviewPrivateKey).Path
if($private.StartsWith($repo+'\',[StringComparison]::OrdinalIgnoreCase)) {throw 'Review private key must remain outside the repository.'}
New-Item -ItemType Directory -Path $output | Out-Null
$program=Join-Path $output 'program'
foreach($version in @('0.1.0','0.1.1')) {
    $build=Join-Path $output "build-$version"
    & $CMake -S $repo -B $build -G Ninja '-DCMAKE_BUILD_TYPE=Release' '-DBUILD_TESTING=ON' '-DNOVEN_UPDATE_REVIEW_FIXTURE=ON' '-DNOVEN_RESOURCE_REVIEW_FIXTURE=ON' "-DNOVEN_REVIEW_APP_VERSION=$version" '-DNOVEN_UPDATE_KEY_ID=review-only-p256' "-DNOVEN_UPDATE_PUBLIC_KEY_HEX=$hex"
    if($LASTEXITCODE -ne 0) {throw 'Review configure failed.'}
    & $CMake --build $build --target NovenTarkovSupport NovenPluginHost NovenLauncher NovenUpdater NovenReleaseTool
    if($LASTEXITCODE -ne 0) {throw 'Review build failed.'}
    $destination=if($version -eq '0.1.0') {Join-Path $program 'versions/0.1.0'} else {Join-Path $output 'target-0.1.1'}
    New-Item -ItemType Directory -Path $destination -Force | Out-Null
    Get-ChildItem -LiteralPath $core -Force | Copy-Item -Destination $destination -Recurse
    foreach($name in @('NovenTarkovSupport.exe','NovenPluginHost.exe')) {Copy-Item -LiteralPath (Join-Path $build $name) -Destination (Join-Path $destination $name) -Force}
    Copy-Item -LiteralPath $fixture -Destination (Join-Path $destination 'resource-review-fixture') -Recurse
    # 合成文件验证真实分块差异，不使用任何第三方地图图像。
    # Synthetic files exercise real chunk deltas without third-party map imagery.
    $large=[Text.Encoding]::ASCII.GetBytes(('a' * 12MB))
    if($version -eq '0.1.0') {$large[4MB]=98}
    [IO.File]::WriteAllBytes((Join-Path $destination 'synthetic-large.bin'),$large)
    [IO.File]::WriteAllText((Join-Path $destination 'synthetic-unchanged.txt'),'unchanged across versions')
    [IO.File]::WriteAllText((Join-Path $destination 'synthetic-changed.txt'),"version $version")
    if($version -eq '0.1.0') {
        [IO.File]::WriteAllText((Join-Path $destination 'synthetic-obsolete.txt'),'removed from target')
        foreach($name in @('NovenLauncher.exe','NovenUpdater.exe')) {Copy-Item -LiteralPath (Join-Path $build $name) -Destination (Join-Path $program $name)}
        $seedProcess=Start-Process -FilePath (Join-Path $program 'NovenUpdater.exe') -ArgumentList '--seed' -WindowStyle Hidden -Wait -PassThru
        if($seedProcess.ExitCode -ne 0) {throw 'Review initial inventory failed.'}
    } else {
        [IO.File]::WriteAllText((Join-Path $destination 'synthetic-new.txt'),'new target file')
        & (Join-Path $build 'NovenReleaseTool.exe') --release $destination (Join-Path $program 'update-review-fixture') $version 'review-only-p256' $private '2026-10-10T00:00:00Z'
        if($LASTEXITCODE -ne 0) {throw 'Offline release generation failed.'}
    }
}
Write-Output "NON-PRODUCTION offline review ready: $program\NovenLauncher.exe"
Write-Output 'Nothing installed, launched, uploaded or published. User Data is the sibling program.update-review-data Test root.'
