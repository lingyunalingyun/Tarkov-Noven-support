param(
    [Parameter(Mandatory)][string]$SourceRoot,
    [Parameter(Mandatory)][string[]]$PrivateKeys,
    [string[]]$PayloadRoots = @()
)
$ErrorActionPreference = 'Stop'
# 只比较操作者显式指定的密钥，不输出秘密、指纹或文件内容。
# Compare explicitly supplied operator secrets without printing bytes or private fingerprints.
$source = (Resolve-Path -LiteralPath $SourceRoot).Path
$encoding = [Text.Encoding]::GetEncoding(28591)
$needles = @()
foreach ($key in $PrivateKeys) {
    $bytes = [IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $key).Path)
    try {
        if ($bytes.Length -ne 104) { throw 'Expected CNG P-256 private blob.' }
        foreach ($part in @($bytes, $bytes[72..103])) {
            $needles += $encoding.GetString([byte[]]$part)
            $needles += [Convert]::ToBase64String([byte[]]$part)
            $hex = ([BitConverter]::ToString([byte[]]$part)).Replace('-', '')
            $needles += $hex, $hex.ToLowerInvariant()
        }
    } finally { [Array]::Clear($bytes, 0, $bytes.Length) }
}
$files = @(& git -C $source -c core.quotepath=false ls-files --cached --others --exclude-standard)
if ($LASTEXITCODE) { throw 'Source enumeration failed.' }
$paths = @($files | ForEach-Object { Join-Path $source $_ })
foreach ($root in $PayloadRoots) {
    $item = Get-Item -LiteralPath $root -Force
    if ($item.PSIsContainer) {
        $paths += @(Get-ChildItem -LiteralPath $item.FullName -Recurse -File -Force | ForEach-Object FullName)
    } else { $paths += $item.FullName }
}
$checked = 0; $total = 0L; $matches = 0
foreach ($path in $paths) {
    $item = Get-Item -LiteralPath $path -Force
    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Reparse input in secret audit.' }
    $content = [IO.File]::ReadAllBytes($path)
    $text = $encoding.GetString($content)
    foreach ($needle in $needles) {
        if ($text.IndexOf($needle, [StringComparison]::Ordinal) -ge 0) { $matches++; break }
    }
    $checked++; $total += $content.Length
}
[pscustomobject]@{CheckedFiles=$checked;CheckedBytes=$total;PrivateKeyMatches=$matches}
if ($matches) { throw 'Signing-secret leakage detected; stop publication.' }
