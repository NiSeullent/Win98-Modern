# Ordered source list for every m98wrap.dll build. Keep entries explicit.
param([Parameter(Mandatory = $true)][string]$ProjectRoot)

$relativePaths = @(
    'src/m98wrap.c'
    'src/m98nls_ex.c'
    'src/m98_threadpool.c'
    'src/m98_initonce.c'
    'src/m98_slist.c'
    'src/m98_fls.c'
    'src/wine_uppercase.c'
)

foreach ($relativePath in $relativePaths) {
    Join-Path -Path $ProjectRoot -ChildPath $relativePath
}
