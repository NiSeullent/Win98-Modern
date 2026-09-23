param(
  [Parameter(Mandatory = $true)][string]$InputPath,
  [Parameter(Mandatory = $true)][string]$OutputPath
)
$ErrorActionPreference = 'Stop'
$section = ''
$matchesFound = 0
$source = [System.IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $InputPath))
$bomLength = 0
if ($source.Length -ge 3 -and $source[0] -eq 0xEF -and $source[1] -eq 0xBB -and $source[2] -eq 0xBF) {
  $encoding = [System.Text.UTF8Encoding]::new($false, $true)
  $bomLength = 3
} elseif ($source.Length -ge 2 -and $source[0] -eq 0xFF -and $source[1] -eq 0xFE) {
  $encoding = [System.Text.UnicodeEncoding]::new($false, $false, $true)
  $bomLength = 2
} elseif ($source.Length -ge 2 -and $source[0] -eq 0xFE -and $source[1] -eq 0xFF) {
  $encoding = [System.Text.UnicodeEncoding]::new($true, $false, $true)
  $bomLength = 2
} else {
  # Byte-preserving single-byte view: works for ANSI and BOM-less UTF-8,
  # including non-ASCII text outside the one ASCII configuration line.
  $encoding = [System.Text.Encoding]::GetEncoding(28591)
}
$configurationText = $encoding.GetString($source, $bomLength, $source.Length - $bomLength)
$linePattern = '[^\r\n]*(?:\r\n|\r|\n|$)'
$builder = [System.Text.StringBuilder]::new()
foreach ($item in [regex]::Matches($configurationText, $linePattern)) {
  $line = $item.Value
  if ($line.Length -eq 0) { continue }
  $ending = [regex]::Match($line, '(\r\n|\r|\n)$').Value
  $body = $line.Substring(0, $line.Length - $ending.Length)
  if ($body -match '^\s*\[([^]]+)\]') { $section = $Matches[1] }
  if ($section -eq 'DCFG1' -and $body -match '^\s*contents\s*=\s*(.*)$') {
    $libraries = @($Matches[1].Split(',') | ForEach-Object { $_.Trim() } | Where-Object { $_ })
    if ('m98wrap' -notin $libraries) { $libraries += 'm98wrap' }
    $body = 'contents=' + ($libraries -join ',')
    $matchesFound++
  }
  [void]$builder.Append($body).Append($ending)
}
if ($matchesFound -ne 1) { throw "Expected one DCFG1 contents line; found $matchesFound" }
$destination = [System.IO.Path]::GetFullPath($OutputPath)
$bodyBytes = $encoding.GetBytes($builder.ToString())
$result = [byte[]]::new($bomLength + $bodyBytes.Length)
if ($bomLength -gt 0) { [array]::Copy($source, 0, $result, 0, $bomLength) }
[array]::Copy($bodyBytes, 0, $result, $bomLength, $bodyBytes.Length)
$directory = [System.IO.Path]::GetDirectoryName($destination)
$temporary = [System.IO.Path]::Combine(
  $directory,
  '.' + [System.IO.Path]::GetFileName($destination) + '.' + [guid]::NewGuid().ToString('N') + '.tmp'
)
$backup = $temporary + '.bak'
$replaced = $false
try {
  [System.IO.File]::WriteAllBytes($temporary, $result)
  if ([System.IO.File]::Exists($destination)) {
    [System.IO.File]::Replace($temporary, $destination, $backup)
    $replaced = $true
  } else {
    [System.IO.File]::Move($temporary, $destination)
  }
} finally {
  if ([System.IO.File]::Exists($temporary)) {
    [System.IO.File]::Delete($temporary)
  }
  if ($replaced -and [System.IO.File]::Exists($backup)) {
    [System.IO.File]::Delete($backup)
  }
}
Write-Host $destination
