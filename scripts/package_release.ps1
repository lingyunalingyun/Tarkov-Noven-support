param(
    [Parameter(Mandatory)][string]$BuildDirectory,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [Parameter(Mandatory)][string]$VcRedist,
    [string]$CMake = 'cmake',
    [string]$ISCC = 'ISCC',
    [string]$SigningScript
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = (Resolve-Path -LiteralPath $BuildDirectory).Path
$cache = Get-Content -LiteralPath (Join-Path $buildRoot 'CMakeCache.txt') -Encoding UTF8 -Raw
if ($cache -notmatch 'CMAKE_HOME_DIRECTORY:INTERNAL=([^\r\n]+)' -or [IO.Path]::GetFullPath($Matches[1]) -ne $repoRoot) { throw 'Build belongs to another source tree.' }
if ($cache -notmatch 'CMAKE_BUILD_TYPE:STRING=Release' -or $cache -notmatch 'CMAKE_CXX_COMPILER:FILEPATH=.*cl.exe' -or $cache -match 'NOVEN_(MARKETPLACE|RESOURCE|UPDATE)_REVIEW_FIXTURE:BOOL=ON') { throw 'Packaging requires an MSVC Release production build, not a review build.' }
$signature = Get-AuthenticodeSignature -LiteralPath $VcRedist
if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation') { throw 'VC prerequisite must have a valid Microsoft signature.' }
$vcVersion = (Get-Item -LiteralPath $VcRedist).VersionInfo
if ($vcVersion.FileMajorPart -lt 14) { throw 'VC prerequisite version is too old.' }
$outputRoot = [IO.Path]::GetFullPath($OutputDirectory)
if ($outputRoot -eq [IO.Path]::GetPathRoot($outputRoot) -or $outputRoot -eq $repoRoot -or $outputRoot -eq $buildRoot) { throw 'Unsafe package output root.' }
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
if ((Get-Item -LiteralPath $outputRoot -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Unsafe output reparse point.' }
$staging = Join-Path $outputRoot 'runtime-staging'
# 只删除精确输出子目录，不对源码、构建根或用户数据执行清理。
# Delete only the exact staging child, never source/build roots or user data.
if ([IO.Path]::GetFullPath($staging) -ne ($outputRoot.TrimEnd('\') + '\runtime-staging')) { throw 'Invalid staging containment.' }
if (Test-Path -LiteralPath $staging) {
    if ((Get-Item -LiteralPath $staging -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Unsafe staging reparse point.' }
    if (@(Get-ChildItem -LiteralPath $staging -Recurse -Force | Where-Object {$_.Attributes -band [IO.FileAttributes]::ReparsePoint}).Count) { throw 'Staging contains reparse points.' }
    Remove-Item -LiteralPath $staging -Recurse -Force
}
& $CMake --build $buildRoot --config Release --target NovenTarkovSupport NovenPluginHost NovenLauncher NovenUpdater
if ($LASTEXITCODE -ne 0) { throw 'Release build failed.' }
$version = (Get-Content -LiteralPath (Join-Path $buildRoot 'package-version.txt') -Raw).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+$') { throw 'Invalid CMake product version.' }
& $CMake --install $buildRoot --config Release --component Runtime --prefix $staging
if ($LASTEXITCODE -ne 0) { throw 'CMake install failed.' }
& (Join-Path $PSScriptRoot 'audit_payload.ps1') -Payload $staging -Version $version
if ($SigningScript) {
    foreach ($exe in @("versions/$version/NovenTarkovSupport.exe","versions/$version/NovenPluginHost.exe",'NovenLauncher.exe','NovenUpdater.exe')) {
        & $SigningScript (Join-Path $staging $exe)
        if (-not $? -or (Get-AuthenticodeSignature (Join-Path $staging $exe)).Status -ne 'Valid') { throw "Signing failed: $exe" }
    }
}
# Win32 EXE 不能依赖 PowerShell 的隐式等待；审计必须在 inventory 写完后开始。
# Do not rely on implicit PowerShell waiting for a Win32 EXE; audit only after inventory completion.
$seedProcess = Start-Process -FilePath (Join-Path $staging 'NovenUpdater.exe') -ArgumentList '--seed' -WindowStyle Hidden -Wait -PassThru
if ($seedProcess.ExitCode -ne 0) { throw 'Initial version inventory generation failed.' }
& (Join-Path $PSScriptRoot 'audit_payload.ps1') -Payload $staging -Version $version
foreach($metadata in @('initial.json','current.json','last-good.json')) {
    if(-not(Test-Path -LiteralPath (Join-Path $staging $metadata) -PathType Leaf)) { throw "Missing activation metadata: $metadata" }
}
& $ISCC "/DPayload=$staging" "/DArtifactDir=$outputRoot" "/DAppVersion=$version" "/DVcRedist=$VcRedist" "/DVcMajor=$($vcVersion.FileMajorPart)" "/DVcMinor=$($vcVersion.FileMinorPart)" "/DVcBuild=$($vcVersion.FileBuildPart)" (Join-Path $repoRoot 'installer\Noven.iss')
if ($LASTEXITCODE -ne 0) { throw 'Inno Setup failed.' }
$artifact = Join-Path $outputRoot "NovenTarkovSupport-Setup-$version.exe"
if ($SigningScript) { & $SigningScript $artifact; if (-not $? -or (Get-AuthenticodeSignature $artifact).Status -ne 'Valid') { throw 'Setup signing failed.' } }
$result = [ordered]@{File=$artifact;Version=$version;Bytes=(Get-Item -LiteralPath $artifact).Length;SHA256=(Get-FileHash -LiteralPath $artifact -Algorithm SHA256).Hash;Signed=[bool]$SigningScript}
$result | ConvertTo-Json | Tee-Object -FilePath (Join-Path $outputRoot "NovenTarkovSupport-Setup-$version.artifact.json")
