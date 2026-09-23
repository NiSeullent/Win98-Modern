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
# These static import gates use the exact isolated test artifacts compiled by
# release/build-dlls.ps1; verify they match the packaged wrapper DLL.
foreach ($candidate in @('build/finalpath/m98wrap.dll', 'build/stream/m98wrap.dll',
                         'build/localeinfo/m98wrap.dll', 'build/restart/m98wrap.dll',
                         'build/processpath/m98wrap.dll', 'build/condition/m98wrap.dll',
                         'build/initonce/m98wrap.dll', 'build/threadpool/m98wrap.dll',
                         'build/slist/m98wrap.dll', 'build/fls-integration/m98wrap.dll')) {
  $actual = (Get-FileHash -LiteralPath (Require-File $candidate) -Algorithm SHA256).Hash
  $shipped = (Get-FileHash -LiteralPath (Require-File 'build/m98wrap.dll') -Algorithm SHA256).Hash
  if ($actual -ne $shipped) { throw "KERNEL32 test DLL differs from packaged m98wrap.dll: $candidate" }
}
foreach ($artifact in @(
  'build/finalpath/finalpath_smoke.exe', 'build/finalpath/finalpath_import_probe.exe',
  'build/finalpath/smoke.exe',
  'build/stream/stream_smoke.exe', 'build/stream/stream_import_probe.exe',
  'build/stream/smoke.exe',
  'build/localeinfo/localeinfo_smoke.exe',
  'build/localeinfo/localeinfo_import_probe.exe', 'build/localeinfo/smoke.exe',
  'build/restart/restart_smoke.exe', 'build/restart/restart_import_probe.exe',
  'build/restart/smoke.exe',
  'build/processpath/processpath_smoke.exe',
  'build/processpath/processpath_import_probe.exe',
  'build/processpath/smoke.exe',
  'build/condition/condition_smoke.exe',
  'build/condition/condition_import_probe.exe',
  'build/condition/smoke.exe',
  'build/nls-ex/nls_ex_fixture.dll',
  'build/nls-ex/nls_ex_smoke.exe',
  'build/nls-ex/nls_ex_integrated_probe.exe',
  'build/nls-ex/nls_ex_native_probe.exe',
  'build/nls-ex/nls_ex_import_probe.exe',
  'build/threadpool/threadpool_fixture.dll',
  'build/threadpool/TPMARK.DLL',
  'build/threadpool/threadpool_smoke.exe',
  'build/threadpool/threadpool_integrated_probe.exe',
  'build/threadpool/threadpool_import_probe.exe',
  'build/threadpool/threadpool_guest_import_smoke.exe',
  'build/threadpool/threadpool_relocation_probe.exe',
  'build/threadpool/threadpool_callback_direct.exe',
  'build/threadpool/threadpool_callback_static.exe',
  'build/threadpool/threadpool_callback_integrated.exe',
  'build/initonce/ONCEFIX.DLL',
  'build/initonce/initonce_smoke.exe',
  'build/initonce/initonce_import_probe.exe',
  'build/initonce/initonce_integrated_probe.exe',
  'build/slist/SLISTFIX.DLL',
  'build/slist/slist_smoke.exe',
  'build/slist/slist_import_probe.exe',
  'build/slist/slist_integrated_probe.exe',
  'build/slist/slist_fault_smoke.exe',
  'build/slist/slist_native_probe.exe',
  'build/fls/FLSFIX.DLL', 'build/fls/fls_smoke.exe',
  'build/fls-rundown-race/FLSRACE.EXE',
  'build/fls-integration/fls_direct.exe', 'build/fls-integration/fls_static.exe',
  'build/fls-integration/fls_integrated.exe', 'build/thread-lifecycle/THRLIFE.EXE'
)) { [void](Require-File $artifact) }
Push-Location $projectRoot
try {
  & python 'tests/check_finalpath_pe98.py'
  if ($LASTEXITCODE -ne 0) { throw 'GetFinalPathNameByHandleW PE/import validation failed' }
  & python 'tests/check_stream_pe98.py'
  if ($LASTEXITCODE -ne 0) { throw 'FindFirstStreamW PE/import validation failed' }
  & python 'tests/check_localeinfo_pe98.py'
  if ($LASTEXITCODE -ne 0) { throw 'GetLocaleInfoEx PE/import validation failed' }
  & python 'tests/check_restart_pe98.py'
  if ($LASTEXITCODE -ne 0) { throw 'Application restart PE/import validation failed' }
  & python 'tests/check_processpath_pe98.py'
  if ($LASTEXITCODE -ne 0) { throw 'QueryFullProcessImageNameA/W PE/import validation failed' }
  & python 'tests/check_condition_pe98.py'
  if ($LASTEXITCODE -ne 0) { throw 'SRW condition-variable PE/import validation failed' }
  & python 'tests/check_nls_ex_pe98.py'
  if ($LASTEXITCODE -ne 0) { throw 'Locale-name NLS PE/import validation failed' }
  & python 'tests/check_threadpool_pe98.py'
  if ($LASTEXITCODE -ne 0) { throw 'Threadpool work/callback PE/import validation failed' }
  & python 'tests/check_initonce_pe98.py'
  if ($LASTEXITCODE -ne 0) { throw 'InitOnce family PE/import validation failed' }
  & python 'tests/fls_check_pe98.py' 'build/fls/FLSFIX.DLL' 'build/fls/fls_smoke.exe' 'build/fls-rundown-race/FLSRACE.EXE' 'build/thread-lifecycle/THRLIFE.EXE'
  if ($LASTEXITCODE -ne 0) { throw 'FLS fixture PE/import validation failed' }
  & python 'tests/fls_integration_pe98.py'
  if ($LASTEXITCODE -ne 0) { throw 'FLS integration PE/import validation failed' }
  & python 'tests/check_slist_pe98.py'
  if ($LASTEXITCODE -ne 0) { throw 'SList family PE/import validation failed' }
} finally { Pop-Location }
& python (Require-File 'tests/check_m98advapi_pe98.py') `
  (Require-File 'build/advapi/m98advapi.dll') `
  (Require-File 'build/advapi/advapi_import_probe.exe') `
  (Require-File 'build/advapi/advapi_smoke.exe')
if ($LASTEXITCODE -ne 0) { throw 'Windows 98 ADVAPI PE validation failed' }
Check-PE 'release/check-uxtheme-release-pe98.py' 'build/uxtheme-known/UXTHEME.DLL'
Check-PE 'tests/check_known_dll_switch_pe98.py' 'build/known_dll_switch.exe'
Check-PE 'tests/check_threadpool_exit_pe98.py' 'build/threadpool-exit/TPEXIT.EXE'
$includeDbgHelp = Test-Path -LiteralPath (Join-Path $buildDir 'dbghelp.dll') -PathType Leaf
$includeDwmApi = Test-Path -LiteralPath (Join-Path $buildDir 'dwmapi.dll') -PathType Leaf
$includeBCrypt = Test-Path -LiteralPath (Join-Path $buildDir 'bcrypt.dll') -PathType Leaf
$includeShell = Test-Path -LiteralPath (Join-Path $buildDir 'm98shell.dll') -PathType Leaf
if ($includeDbgHelp) {
  Check-PE 'tests/check_dbghelp_pe98.py' 'build/dbghelp.dll'
}
if ($includeDwmApi) {
  Check-PE 'tests/check_dwmapi_pe98.py' 'build/dwmapi.dll'
}
if ($includeBCrypt) {
  Check-PE 'tests/check_bcrypt_pe98.py' 'build/bcrypt.dll'
}
if ($includeShell) {
  Check-PE 'tests/check_m98shell_pe98.py' 'build/m98shell.dll'
}

$inputs = [ordered]@{
  'porting/runtime-routes.json' = 'porting/runtime-routes.json'
  'integration/core.ini' = 'integration/core.ini'
  'remote/patch_core_family.py' = 'remote/patch_core_family.py'
  'remote/patch_core_wrapper_upgrade.py' = 'remote/patch_core_wrapper_upgrade.py'
  'tools/build-fls-rundown-race.ps1' = 'tools/build-fls-rundown-race.ps1'
  'tests/fls_rundown_race.c' = 'tests/fls_rundown_race.c'
  'guest-tests/FLSRACE.EXE' = 'build/fls-rundown-race/FLSRACE.EXE'
  'tools/m98wrap-sources.ps1' = 'tools/m98wrap-sources.ps1'
  'tools/build-fls.ps1' = 'tools/build-fls.ps1'
  'tools/build-fls-integration.ps1' = 'tools/build-fls-integration.ps1'
  'tools/build-thread-lifecycle.ps1' = 'tools/build-thread-lifecycle.ps1'
  'src/m98_fls.c' = 'src/m98_fls.c'
  'src/m98_fls.h' = 'src/m98_fls.h'
  'tests/fls_fixture.c' = 'tests/fls_fixture.c'
  'tests/fls_smoke.c' = 'tests/fls_smoke.c'
  'tests/fls_import_probe.c' = 'tests/fls_import_probe.c'
  'tests/fls_check_pe98.py' = 'tests/fls_check_pe98.py'
  'tests/fls_integration_pe98.py' = 'tests/fls_integration_pe98.py'
  'tests/thread_lifecycle_probe.c' = 'tests/thread_lifecycle_probe.c'
  'docs/FLS_PORT_IMPLEMENTATION.md' = 'docs/FLS_PORT_IMPLEMENTATION.md'
  'docs/KERNELEX_THREAD_LIFECYCLE.md' = 'docs/KERNELEX_THREAD_LIFECYCLE.md'
  'guest-tests/FLSFIX.DLL' = 'build/fls/FLSFIX.DLL'
  'guest-tests/fls_smoke.exe' = 'build/fls/fls_smoke.exe'
  'guest-tests/fls_static.exe' = 'build/fls-integration/fls_static.exe'
  'guest-tests/fls_integrated.exe' = 'build/fls-integration/fls_integrated.exe'
  'guest-tests/fls_direct.exe' = 'build/fls-integration/fls_direct.exe'
  'guest-tests/THRLIFE.EXE' = 'build/thread-lifecycle/THRLIFE.EXE'
  'm98wrap.dll'                   = 'build/m98wrap.dll'
  'm98adv.dll'                    = 'build/m98adv.dll'
  'UXTHEME.DLL'                   = 'build/uxtheme-known/UXTHEME.DLL'
  'KSWITCH.EXE'                   = 'build/known_dll_switch.exe'
  'INSTALL-ko.md'                 = 'release/INSTALL-ko.md'
  'BUILD-SOURCE.md'               = 'release/BUILD-SOURCE.md'
  'KERNELEX-UXTHEME-NOTICES.md'   = 'release/KERNELEX-UXTHEME-NOTICES.md'
  'build-dlls.ps1'                = 'release/build-dlls.ps1'
  'tools/build-finalpath.ps1'     = 'tools/build-finalpath.ps1'
  'tools/build-stream.ps1'        = 'tools/build-stream.ps1'
  'tools/build-localeinfo.ps1'    = 'tools/build-localeinfo.ps1'
  'tools/build-restart.ps1'       = 'tools/build-restart.ps1'
  'tools/build-processpath.ps1'   = 'tools/build-processpath.ps1'
  'tools/build-condition.ps1'     = 'tools/build-condition.ps1'
  'tools/build-nls-ex.ps1'        = 'tools/build-nls-ex.ps1'
  'tools/build-threadpool.ps1'    = 'tools/build-threadpool.ps1'
  'tools/build-threadpool-exit.ps1' = 'tools/build-threadpool-exit.ps1'
  'tools/build-initonce.ps1'      = 'tools/build-initonce.ps1'
  'tools/build-slist.ps1'         = 'tools/build-slist.ps1'
  'LICENSE'                       = 'LICENSE'
  'THIRD_PARTY.md'                = 'THIRD_PARTY.md'
  'licenses/Wine-LGPL-2.1.txt'   = 'licenses/Wine-LGPL-2.1.txt'
  'src/kex_abi.h'                 = 'src/kex_abi.h'
  'src/m98wrap.c'                 = 'src/m98wrap.c'
  'src/m98nls_ex.c'               = 'src/m98nls_ex.c'
  'src/m98nls_ex.h'               = 'src/m98nls_ex.h'
  'src/m98_threadpool.c'           = 'src/m98_threadpool.c'
  'src/m98_threadpool.h'           = 'src/m98_threadpool.h'
  'src/m98_initonce.c'            = 'src/m98_initonce.c'
  'src/m98_initonce.h'            = 'src/m98_initonce.h'
  'src/m98_slist.c'               = 'src/m98_slist.c'
  'src/m98_slist.h'               = 'src/m98_slist.h'
  'src/m98advapi.c'               = 'src/m98advapi.c'
  'src/wine_uppercase.c'          = 'src/wine_uppercase.c'
  'src/uxtheme_shim.c'            = 'src/uxtheme_shim.c'
  'src/uxtheme_shim.def'          = 'src/uxtheme_shim.def'
  'src/uxtheme_sysfont.c'         = 'src/uxtheme_sysfont.c'
  'third_party/KernelEx/auxiliary/uxtheme/uxtheme.c' = 'third_party/KernelEx/auxiliary/uxtheme/uxtheme.c'
  'third_party/KernelEx/auxiliary/uxtheme/metric.c' = 'third_party/KernelEx/auxiliary/uxtheme/metric.c'
  'third_party/KernelEx/auxiliary/uxtheme/uxtheme.def' = 'third_party/KernelEx/auxiliary/uxtheme/uxtheme.def'
  'src/dbghelp_shim.c'            = 'src/dbghelp_shim.c'
  'src/dbghelp_shim.def'          = 'src/dbghelp_shim.def'
  'src/dwmapi_shim.c'             = 'src/dwmapi_shim.c'
  'src/dwmapi_shim.def'           = 'src/dwmapi_shim.def'
  'src/bcrypt_shim.c'             = 'src/bcrypt_shim.c'
  'src/bcrypt_shim.def'           = 'src/bcrypt_shim.def'
  'tests/check_pe98.py'           = 'tests/check_pe98.py'
  'tests/smoke.c'                 = 'tests/smoke.c'
  'tests/finalpath_smoke.c'       = 'tests/finalpath_smoke.c'
  'tests/finalpath_import_probe.c' = 'tests/finalpath_import_probe.c'
  'tests/check_finalpath_pe98.py' = 'tests/check_finalpath_pe98.py'
  'tests/stream_smoke.c'          = 'tests/stream_smoke.c'
  'tests/stream_import_probe.c'   = 'tests/stream_import_probe.c'
  'tests/check_stream_pe98.py'    = 'tests/check_stream_pe98.py'
  'tests/localeinfo_smoke.c'      = 'tests/localeinfo_smoke.c'
  'tests/localeinfo_import_probe.c' = 'tests/localeinfo_import_probe.c'
  'tests/check_localeinfo_pe98.py' = 'tests/check_localeinfo_pe98.py'
  'tests/restart_smoke.c'         = 'tests/restart_smoke.c'
  'tests/restart_import_probe.c'  = 'tests/restart_import_probe.c'
  'tests/check_restart_pe98.py'  = 'tests/check_restart_pe98.py'
  'tests/processpath_smoke.c'     = 'tests/processpath_smoke.c'
  'tests/processpath_import_probe.c' = 'tests/processpath_import_probe.c'
  'tests/check_processpath_pe98.py' = 'tests/check_processpath_pe98.py'
  'tests/condition_smoke.c'       = 'tests/condition_smoke.c'
  'tests/condition_import_probe.c' = 'tests/condition_import_probe.c'
  'tests/check_condition_pe98.py' = 'tests/check_condition_pe98.py'
  'tests/nls_ex_fixture.c'        = 'tests/nls_ex_fixture.c'
  'tests/nls_ex_smoke.c'          = 'tests/nls_ex_smoke.c'
  'tests/nls_ex_native_probe.c'   = 'tests/nls_ex_native_probe.c'
  'tests/nls_ex_import_probe.c'   = 'tests/nls_ex_import_probe.c'
  'tests/check_nls_ex_pe98.py'    = 'tests/check_nls_ex_pe98.py'
  'tests/threadpool_fixture.c'    = 'tests/threadpool_fixture.c'
  'tests/threadpool_exit_smoke.c' = 'tests/threadpool_exit_smoke.c'
  'tests/threadpool_marker.c'     = 'tests/threadpool_marker.c'
  'tests/threadpool_smoke.c'      = 'tests/threadpool_smoke.c'
  'tests/threadpool_import_probe.c' = 'tests/threadpool_import_probe.c'
  'tests/threadpool_relocation_probe.c' = 'tests/threadpool_relocation_probe.c'
  'tests/threadpool_callback_smoke.c' = 'tests/threadpool_callback_smoke.c'
  'tests/check_threadpool_pe98.py' = 'tests/check_threadpool_pe98.py'
  'tests/check_threadpool_exit_pe98.py' = 'tests/check_threadpool_exit_pe98.py'
  'tests/initonce_fixture.c'       = 'tests/initonce_fixture.c'
  'tests/initonce_smoke.c'         = 'tests/initonce_smoke.c'
  'tests/initonce_import_probe.c'  = 'tests/initonce_import_probe.c'
  'tests/check_initonce_pe98.py'   = 'tests/check_initonce_pe98.py'
  'tests/slist_fixture.c'          = 'tests/slist_fixture.c'
  'tests/slist_smoke.c'            = 'tests/slist_smoke.c'
  'tests/slist_import_probe.c'     = 'tests/slist_import_probe.c'
  'tests/slist_fault_smoke.c'      = 'tests/slist_fault_smoke.c'
  'tests/slist_native_probe.c'     = 'tests/slist_native_probe.c'
  'tests/check_slist_pe98.py'      = 'tests/check_slist_pe98.py'
  'guest-tests/threadpool_guest_import_smoke.exe' = 'build/threadpool/threadpool_guest_import_smoke.exe'
  'guest-tests/threadpool_callback_direct.exe' = 'build/threadpool/threadpool_callback_direct.exe'
  'guest-tests/threadpool_callback_static.exe' = 'build/threadpool/threadpool_callback_static.exe'
  'guest-tests/threadpool_callback_integrated.exe' = 'build/threadpool/threadpool_callback_integrated.exe'
  'guest-tests/threadpool_fixture.dll' = 'build/threadpool/threadpool_fixture.dll'
  'guest-tests/TPMARK.DLL'         = 'build/threadpool/TPMARK.DLL'
  'guest-tests/TPEXIT.EXE'         = 'build/threadpool-exit/TPEXIT.EXE'
  'guest-tests/initonce_import_probe.exe' = 'build/initonce/initonce_import_probe.exe'
  'guest-tests/SLISTFIX.DLL'       = 'build/slist/SLISTFIX.DLL'
  'guest-tests/slist_smoke.exe'    = 'build/slist/slist_smoke.exe'
  'guest-tests/slist_import_probe.exe' = 'build/slist/slist_import_probe.exe'
  'guest-tests/slist_integrated_probe.exe' = 'build/slist/slist_integrated_probe.exe'
  'guest-tests/slist_fault_smoke.exe' = 'build/slist/slist_fault_smoke.exe'
  'tests/check_m98advapi_pe98.py' = 'tests/check_m98advapi_pe98.py'
  'tests/advapi_smoke.c'          = 'tests/advapi_smoke.c'
  'tests/advapi_import_probe.c'   = 'tests/advapi_import_probe.c'
  'tests/check_uxtheme_release_pe98.py' = 'release/check-uxtheme-release-pe98.py'
  'tests/check_known_dll_switch_pe98.py' = 'tests/check_known_dll_switch_pe98.py'
  'remote/guest/known_dll_switch.c' = 'remote/guest/known_dll_switch.c'
  'tests/check_dbghelp_pe98.py'   = 'tests/check_dbghelp_pe98.py'
  'tests/check_dwmapi_pe98.py'    = 'tests/check_dwmapi_pe98.py'
  'tests/check_bcrypt_pe98.py'    = 'tests/check_bcrypt_pe98.py'
  'tests/requirements.txt'        = 'tests/requirements.txt'
  'benchmarks/win98se-ko-oem-native-exports-v1.json' = 'benchmarks/win98se-ko-oem-native-exports-v1.json'
  'docs/ADVAPI_REGGETVALUE_PORT.md' = 'docs/ADVAPI_REGGETVALUE_PORT.md'
  'docs/FINALPATH_PORT.md'        = 'docs/FINALPATH_PORT.md'
  'docs/FIRSTSTREAM_PORT.md'      = 'docs/FIRSTSTREAM_PORT.md'
  'docs/LOCALEINFO_PORT.md'       = 'docs/LOCALEINFO_PORT.md'
  'docs/NPP_RESTART_PORT.md'      = 'docs/NPP_RESTART_PORT.md'
  'docs/NPP_PROCESSPATH_PORT.md'  = 'docs/NPP_PROCESSPATH_PORT.md'
  'docs/NPP_CONDITION_PORT.md'    = 'docs/NPP_CONDITION_PORT.md'
  'docs/NLS_EX_PORT.md'           = 'docs/NLS_EX_PORT.md'
  'docs/THREADPOOL_WORK_PORT.md'  = 'docs/THREADPOOL_WORK_PORT.md'
  'docs/THREADPOOL_CALLBACK_PORT.md' = 'docs/THREADPOOL_CALLBACK_PORT.md'
  'docs/INITONCE_PORT.md'         = 'docs/INITONCE_PORT.md'
  'docs/SLIST_PORT.md'            = 'docs/SLIST_PORT.md'
  'docs/PUBLIC_KERNELEX_VARIANTS.md' = 'docs/PUBLIC_KERNELEX_VARIANTS.md'
  'docs/UXTHEME_NPP_PORT.md'      = 'docs/UXTHEME_NPP_PORT.md'
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
if ($includeShell) {
  $inputs['m98shell.dll'] = 'build/m98shell.dll'
  $inputs['src/m98shell.c'] = 'src/m98shell.c'
  $inputs['src/m98shell_openfolder.c'] = 'src/m98shell_openfolder.c'
  $inputs['tests/check_m98shell_pe98.py'] = 'tests/check_m98shell_pe98.py'
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
Write-Host "M98SHELL.DLL included: $includeShell"
Write-Host 'M98ADV.DLL included: True'
Write-Host '89-entry KERNEL32 wrapper and focused NLS/threadpool/callback/InitOnce/SList/FLS test sources included: True'
Write-Host 'Guest static/integrated/fixture probes, SList fault probe and TPMARK.DLL included: True'
Write-Host 'UXTHEME.DLL KnownDLL candidate included: True'
Write-Host 'KSWITCH.EXE guarded mapping helper included: True'
