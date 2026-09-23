param(
  [Parameter(Mandatory = $true)][ValidatePattern('^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$')][string]$Version
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $projectRoot 'build'
$releaseDir = Join-Path $buildDir 'releases'

function Require-File([string]$relativePath) {
  $path = Join-Path $projectRoot $relativePath
  $item = Get-Item -LiteralPath $path -ErrorAction Stop
  if ($item.PSIsContainer -or $item.Length -eq 0 -or
      ($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint)) {
    throw "Missing, empty, or linked package input: $relativePath"
  }
  return $item.FullName
}

function Check-PE([string]$checker, [string]$artifact) {
  & python (Require-File $checker) (Require-File $artifact)
  if ($LASTEXITCODE -ne 0) {
    throw "Windows 98 PE validation failed: $artifact"
  }
}

# The PE gates inspect machine type, loader version, imports, and exports.
# Never pick files by glob: build/ can also contain private test media.
Check-PE 'tests/check_pe98.py' 'build/m98wrap.dll'
$includeDbgHelp = Test-Path -LiteralPath (Join-Path $buildDir 'dbghelp.dll') -PathType Leaf
$includeDwmApi = Test-Path -LiteralPath (Join-Path $buildDir 'dwmapi.dll') -PathType Leaf
$includeBCrypt = Test-Path -LiteralPath (Join-Path $buildDir 'bcrypt.dll') -PathType Leaf
if ($includeDbgHelp) {
  Check-PE 'tests/check_dbghelp_pe98.py' 'build/dbghelp.dll'
}
if ($includeDwmApi) {
  Check-PE 'tests/check_dwmapi_pe98.py' 'build/dwmapi.dll'
}
if ($includeBCrypt) {
  Check-PE 'tests/check_bcrypt_pe98.py' 'build/bcrypt.dll'
}

$inputs = [ordered]@{
  'm98wrap.dll'                   = 'build/m98wrap.dll'
  'INSTALL-ko.md'                 = 'release/INSTALL-ko.md'
  'BUILD-SOURCE.md'               = 'release/BUILD-SOURCE.md'
  'build-dlls.ps1'                = 'release/build-dlls.ps1'
  'LICENSE'                       = 'LICENSE'
  'THIRD_PARTY.md'                = 'THIRD_PARTY.md'
  'licenses/Wine-LGPL-2.1.txt'   = 'licenses/Wine-LGPL-2.1.txt'
  'src/kex_abi.h'                 = 'src/kex_abi.h'
  'src/m98wrap.c'                 = 'src/m98wrap.c'
  'src/wine_uppercase.c'          = 'src/wine_uppercase.c'
  'src/dbghelp_shim.c'            = 'src/dbghelp_shim.c'
  'src/dbghelp_shim.def'          = 'src/dbghelp_shim.def'
  'src/dwmapi_shim.c'             = 'src/dwmapi_shim.c'
  'src/dwmapi_shim.def'           = 'src/dwmapi_shim.def'
  'src/bcrypt_shim.c'             = 'src/bcrypt_shim.c'
  'src/bcrypt_shim.def'           = 'src/bcrypt_shim.def'
  'tests/check_pe98.py'           = 'tests/check_pe98.py'
  'tests/check_dbghelp_pe98.py'   = 'tests/check_dbghelp_pe98.py'
  'tests/check_dwmapi_pe98.py'    = 'tests/check_dwmapi_pe98.py'
  'tests/check_bcrypt_pe98.py'    = 'tests/check_bcrypt_pe98.py'
  'tests/requirements.txt'        = 'tests/requirements.txt'
  'skills/win98-modern-lab/SKILL.md' = 'skills/win98-modern-lab/SKILL.md'
  'skills/win98-modern-lab/agents/openai.yaml' = 'skills/win98-modern-lab/agents/openai.yaml'
  'skills/win98-modern-lab/references/api-porting.md' = 'skills/win98-modern-lab/references/api-porting.md'
  'skills/win98-modern-lab/references/guest-validation.md' = 'skills/win98-modern-lab/references/guest-validation.md'
  'skills/win98-modern-lab/references/system-work.md' = 'skills/win98-modern-lab/references/system-work.md'
  'skills/win98-modern-lab/references/release-publication.md' = 'skills/win98-modern-lab/references/release-publication.md'
}
if ($includeDbgHelp) {
  $inputs['dbghelp.dll'] = 'build/dbghelp.dll'
}
if ($includeDwmApi) {
  $inputs['dwmapi.dll'] = 'build/dwmapi.dll'
}
if ($includeBCrypt) {
  $inputs['bcrypt.dll'] = 'build/bcrypt.dll'
}

$resolved = [ordered]@{}
foreach ($name in $inputs.Keys) {
  $resolved[$name] = Require-File $inputs[$name]
}

New-Item -ItemType Directory -Path $releaseDir -Force | Out-Null
$baseName = "win98-modern-$Version-x86"
$zipPath = Join-Path $releaseDir "$baseName.zip"
$hashPath = Join-Path $releaseDir "$baseName.zip.sha256"
$temporaryZip = Join-Path $releaseDir ".$baseName.$([guid]::NewGuid().ToString('N')).tmp"
$temporaryHash = Join-Path $releaseDir ".$baseName.$([guid]::NewGuid().ToString('N')).sha256.tmp"
$utf8 = [System.Text.UTF8Encoding]::new($false)
$fixedTime = [System.DateTimeOffset]::new(1980, 1, 1, 0, 0, 0, [System.TimeSpan]::Zero)
$sha256 = [System.Security.Cryptography.SHA256]::Create()

try {
  $stream = [System.IO.File]::Open($temporaryZip, [System.IO.FileMode]::CreateNew)
  try {
    $archive = [System.IO.Compression.ZipArchive]::new(
      $stream, [System.IO.Compression.ZipArchiveMode]::Create, $true, $utf8
    )
    try {
      $checksums = [System.Collections.Generic.List[string]]::new()
      foreach ($name in @($resolved.Keys | Sort-Object -CaseSensitive)) {
        $bytes = [System.IO.File]::ReadAllBytes($resolved[$name])
        $digest = [System.Convert]::ToHexString($sha256.ComputeHash($bytes)).ToLowerInvariant()
        $checksums.Add("$digest  $name")
        $entry = $archive.CreateEntry($name, [System.IO.Compression.CompressionLevel]::Optimal)
        $entry.LastWriteTime = $fixedTime
        $entryStream = $entry.Open()
        try { $entryStream.Write($bytes, 0, $bytes.Length) }
        finally { $entryStream.Dispose() }
      }
      $manifest = $utf8.GetBytes(($checksums -join "`n") + "`n")
      $entry = $archive.CreateEntry('SHA256SUMS.txt', [System.IO.Compression.CompressionLevel]::Optimal)
      $entry.LastWriteTime = $fixedTime
      $entryStream = $entry.Open()
      try { $entryStream.Write($manifest, 0, $manifest.Length) }
      finally { $entryStream.Dispose() }
    } finally { $archive.Dispose() }
  } finally { $stream.Dispose() }

  $zipDigest = (Get-FileHash -LiteralPath $temporaryZip -Algorithm SHA256).Hash.ToLowerInvariant()
  [System.IO.File]::WriteAllText($temporaryHash, "$zipDigest  $baseName.zip`n", $utf8)
  [System.IO.File]::Move($temporaryZip, $zipPath, $true)
  [System.IO.File]::Move($temporaryHash, $hashPath, $true)
} finally {
  $sha256.Dispose()
  if (Test-Path -LiteralPath $temporaryZip) { Remove-Item -LiteralPath $temporaryZip -Force }
  if (Test-Path -LiteralPath $temporaryHash) { Remove-Item -LiteralPath $temporaryHash -Force }
}

Write-Host "Package: $zipPath"
Write-Host "SHA-256: $zipDigest"
Write-Host "Checksum file: $hashPath"
Write-Host "DBGHELP.DLL included: $includeDbgHelp"
Write-Host "DWMAPI.DLL included: $includeDwmApi"
Write-Host "BCRYPT.DLL included: $includeBCrypt"
