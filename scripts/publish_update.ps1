param(
    [Parameter(Mandatory)][string]$BuildDirectory,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [Parameter(Mandatory)][string]$PrivateKey,
    [Parameter(Mandatory)][string]$PublicKey,
    [string]$KeyId='noven-stable-2026',
    [string]$CMake='cmake',
    [switch]$Publish
)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
$build=(Resolve-Path -LiteralPath $BuildDirectory).Path
$private=(Resolve-Path -LiteralPath $PrivateKey).Path
$public=(Resolve-Path -LiteralPath $PublicKey).Path
if($private.StartsWith($repo+'\',[StringComparison]::OrdinalIgnoreCase)) {throw 'Private key must stay outside source repository.'}
$privateAcl=Get-Acl -LiteralPath $private
$userSid=[Security.Principal.WindowsIdentity]::GetCurrent().User.Value
foreach($rule in $privateAcl.Access) {
    if($rule.AccessControlType -eq 'Allow' -and $rule.IdentityReference.Translate([Security.Principal.SecurityIdentifier]).Value -ne $userSid) {throw 'Private key ACL must allow only the operator user.'}
}
$cache=Get-Content -LiteralPath (Join-Path $build 'CMakeCache.txt') -Raw
if($cache -notmatch 'CMAKE_BUILD_TYPE:STRING=Release' -or $cache -match 'NOVEN_(RESOURCE|UPDATE|MARKETPLACE)_REVIEW_FIXTURE:BOOL=ON') {throw 'Production Release build required.'}
$output=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path -LiteralPath $output) {throw 'Release output must be a new directory; immutable artifacts are never replaced.'}
if($output.StartsWith($repo+'\',[StringComparison]::OrdinalIgnoreCase) -or $private.StartsWith($output+'\',[StringComparison]::OrdinalIgnoreCase)) {throw 'Output/key containment is unsafe.'}
New-Item -ItemType Directory -Path $output | Out-Null
& $CMake --build $build --target NovenTarkovSupport NovenPluginHost NovenLauncher NovenUpdater NovenReleaseTool NovenUpdateChannelTool
if($LASTEXITCODE) {throw 'Release build failed.'}
$version=(Get-Content -LiteralPath (Join-Path $build 'package-version.txt') -Raw).Trim()
if($version -notmatch '^\d+\.\d+\.\d+$') {throw 'Invalid canonical version.'}
$staging=Join-Path $output 'runtime-staging'
& $CMake --install $build --config Release --component Runtime --prefix $staging
if($LASTEXITCODE) {throw 'Runtime install failed.'}
& (Join-Path $PSScriptRoot 'audit_payload.ps1') -Payload $staging -Version $version
$artifacts=Join-Path $output 'artifacts'
& (Join-Path $build 'NovenReleaseTool.exe') --release (Join-Path $staging "versions/$version") $artifacts $version $KeyId $private ([DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ'))
if($LASTEXITCODE) {throw 'Signed release generation failed.'}
$validator=Join-Path $build 'NovenUpdateChannelTool.exe'
& $validator --verify $artifacts $public $KeyId
if($LASTEXITCODE) {throw 'Independent signature/pack verification failed.'}
$packs=@(Get-ChildItem -LiteralPath $artifacts -Filter '*.pack' -File)
if(-not $packs.Count -or @($packs | Where-Object Length -ge 100MB).Count) {throw 'GitHub individual-file limit: split oversized packs before publishing.'}
$packs | ForEach-Object {[pscustomobject]@{Name=$_.Name;Bytes=$_.Length;SHA256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}}
if(-not $Publish) {Write-Output 'Prepared only; no remote mutation. Reuse the artifacts for reviewed publication.';return}
# 只向批准的通道仓库提交包，绝不推送源码仓库；清单在全部远程范围核验后才发布。
# Push only the approved channel repository, never source; publish manifest after all remote ranges verify.
$channel=Join-Path $output 'channel'
& git clone --branch main https://github.com/lingyunalingyun/Noven-Update-Channel.git $channel
if($LASTEXITCODE) {throw 'Channel clone failed.'}
$stable=Join-Path $channel 'stable'
New-Item -ItemType Directory -Path $stable -Force | Out-Null
foreach($pack in $packs) {
    $destination=Join-Path $stable $pack.Name
    if(Test-Path -LiteralPath $destination) {throw 'Pack identity already published; use a new version, never overwrite.'}
    Copy-Item -LiteralPath $pack.FullName -Destination $destination
    & git -C $channel add -- "stable/$($pack.Name)"
    if($LASTEXITCODE) {throw 'Pack staging failed.'}
}
& git -C $channel commit -m "Publish immutable Core $version packs"
if($LASTEXITCODE) {throw 'Pack commit failed.'}
& git -C $channel push origin main
if($LASTEXITCODE) {throw 'Pack publication failed; release.json unchanged.'}
$root='https://raw.githubusercontent.com/lingyunalingyun/Noven-Update-Channel/main/stable/'
& $validator --verify-remote $artifacts $public $KeyId $root
if($LASTEXITCODE) {throw 'Remote pack validation failed; release.json unchanged. Investigate, do not publish manifest.'}
Copy-Item -LiteralPath (Join-Path $artifacts 'release.json') -Destination (Join-Path $stable 'release.json')
& git -C $channel add -- stable/release.json
if($LASTEXITCODE) {throw 'Manifest staging failed.'}
& git -C $channel commit -m "Activate signed stable Core $version"
if($LASTEXITCODE) {throw 'Manifest commit failed.'}
& git -C $channel push origin main
if($LASTEXITCODE) {throw 'Manifest publication failed.'}
$response=Invoke-WebRequest -UseBasicParsing -Uri ($root+'release.json') -MaximumRedirection 0 -TimeoutSec 30
if($response.Content -ne (Get-Content -LiteralPath (Join-Path $artifacts 'release.json') -Raw)) {throw 'Raw manifest cache has not propagated; channel publication exists, acceptance pending. Wait for max-age expiry and verify again.'}
Write-Output "Signed stable $version published; installer/manual update acceptance is separate."
