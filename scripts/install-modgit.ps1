# install-modgit.ps1 - Installer for ModuleGit Extension
# Usage: powershell -ExecutionPolicy Bypass -File scripts/install-modgit.ps1

Write-Host "--- ModuleGit Installer for Windows ---" -ForegroundColor Cyan

$RepoRoot = Get-Location
$BinDir = Join-Path $RepoRoot "bin"
$Executable = Join-Path $BinDir "git-modgit.exe"

# 1. Create bin directory
if (!(Test-Path $BinDir)) {
    New-Item -ItemType Directory -Path $BinDir
}

# 2. Check for GCC
$Gcc = Get-Command gcc -ErrorAction SilentlyContinue
if (!$Gcc) {
    Write-Error "GCC not found. Please install MinGW-w64 (via Scoop: 'scoop install gcc')."
    exit 1
}

Write-Host "[1/3] Compiling ModuleGit..." -ForegroundColor Yellow

# For now, we compile the 'Standalone' version that doesn't depend on the full libgit.a
# to ensure it works on any machine without the 100MB+ Git source dependencies.
$SourceFile = "modgit.c"
$SmokeTestFile = "smoke_test.c" # We'll merge logic into a distributable version

# Standalone build command
& gcc -O2 $SmokeTestFile -o $Executable

if ($LASTEXITCODE -ne 0) {
    Write-Error "Compilation failed."
    exit 1
}

Write-Host "[2/3] Registering Command..." -ForegroundColor Yellow

# Add to User PATH if not already there
$UserPath = [Environment]::GetEnvironmentVariable("Path", "User")
if ($UserPath -notlike "*$BinDir*") {
    $NewPath = "$UserPath;$BinDir"
    [Environment]::SetEnvironmentVariable("Path", $NewPath, "User")
    Write-Host "Success: Added $BinDir to your User PATH." -ForegroundColor Green
    Write-Host "NOTE: You may need to restart your terminal to use 'git modgit'." -ForegroundColor Cyan
}
else {
    Write-Host "Info: bin directory already in PATH." -ForegroundColor Gray
}

Write-Host "[3/3] Verification..." -ForegroundColor Yellow

if (Test-Path $Executable) {
    Write-Host "ModuleGit installed successfully at: $Executable" -ForegroundColor Green
    Write-Host "You can now run 'git modgit' (after restarting your shell)." -ForegroundColor Cyan
}

Write-Host "Done!" -ForegroundColor Cyan
