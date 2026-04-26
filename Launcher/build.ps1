param(
    [ValidateSet('Debug','Release')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$repoRoot = Split-Path $root -Parent
$outDir = Join-Path $root "build"

Write-Host "`n  Cypress Launcher Build" -ForegroundColor Yellow
Write-Host "  =====================`n" -ForegroundColor Yellow

Write-Host "Building Launcher ($Configuration)..." -ForegroundColor Cyan
dotnet publish "$root\CypressLauncher.csproj" -c $Configuration -f net8.0-windows -o "$outDir" /p:LangVersion=latest --nologo -v minimal
if ($LASTEXITCODE -ne 0) { Write-Host "  Build FAILED" -ForegroundColor Red; exit 1 }

# Copy supporting files
$extras = @('courgette.exe', 'GW2.patch', 'BFN.Patch')
foreach ($f in $extras) {
    $src = Join-Path $root $f
    if (Test-Path $src) { Copy-Item $src "$outDir\$f" -Force }
}
if (Test-Path "$root\Docs") {
    Copy-Item "$root\Docs" "$outDir\README" -Recurse -Force
}

# Build server DLLs
Write-Host "`nBuilding Server DLLs..." -ForegroundColor Cyan
$serverRoot = Join-Path $repoRoot "Server"
$serverBuild = Join-Path $serverRoot "build"
& powershell -ExecutionPolicy Bypass -File "$serverRoot\build.ps1" -Configuration $Configuration
if ($LASTEXITCODE -ne 0) { Write-Host "  Server build FAILED" -ForegroundColor Red; exit 1 }
foreach ($dll in Get-ChildItem $serverBuild -Filter "*.dll") {
    Copy-Item $dll.FullName "$outDir\$($dll.Name)" -Force
    Write-Host "  Copied $($dll.Name)" -ForegroundColor DarkGray
}

Write-Host "`n  Build complete -> $outDir" -ForegroundColor Green
Write-Host ""
Get-ChildItem $outDir -File | Sort-Object Name | ForEach-Object {
    $size = "{0,8:N0} KB" -f ($_.Length / 1KB)
    Write-Host "  $size  $($_.Name)"
}
Write-Host ""
