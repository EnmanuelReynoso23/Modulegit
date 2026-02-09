# setup-demo.ps1 - Sets up a fresh modularized repository for testing
# Usage: powershell -ExecutionPolicy Bypass -File scripts/setup-demo.ps1

Write-Host "--- ModuleGit Demo Setup ---" -ForegroundColor Cyan

$DemoName = "ModuleGit-Demo"
if (Test-Path $DemoName) {
    Write-Host "Cleaning up existing demo..." -ForegroundColor Gray
    Remove-Item -Recurse -Force $DemoName
}

# 1. Create Repo
New-Item -ItemType Directory -Path $DemoName | Out-Null
Set-Location $DemoName
git init -b main | Out-Null

Write-Host "[1/3] Creating Project Structure..." -ForegroundColor Yellow

# Create modules
New-Item -ItemType Directory -Path "apps/web"
New-Item -ItemType Directory -Path "apps/api"
New-Item -ItemType Directory -Path "libs/shared"

# Add dummy files
"console.log('web')" > apps/web/index.js
"console.log('api')" > apps/api/main.js
"module.exports = {}" > libs/shared/utils.js

Write-Host "[2/3] Configuring .modgit..." -ForegroundColor Yellow

# Create .modgit config
$Config = @"
[module "web-app"]
    path = apps/web
    depends = shared-lib

[module "api-server"]
    path = apps/api
    depends = shared-lib

[module "shared-lib"]
    path = libs/shared
"@
$Config | Out-File -FilePath .modgit -Encoding utf8

git add .
git commit -m "initial: split monolith into modules" | Out-Null

Write-Host "[3/3] Verifying Installation..." -ForegroundColor Yellow

# Attempt to run git modgit (might require restart if just installed)
$Modgit = git modgit list 2>$null
if ($Modgit) {
    Write-Host "SUCCESS: git modgit is working in this repository!" -ForegroundColor Green
    Write-Host "Modules detected:" -ForegroundColor Gray
    git modgit list
}
else {
    Write-Host "WARNING: 'git modgit' commanded was not found in PATH yet." -ForegroundColor Red
    Write-Host "Try running this command manually after restarting your terminal:" -ForegroundColor Cyan
    Write-Host "cd $DemoName; git modgit list" -ForegroundColor White
}

Set-Location ..
Write-Host "Demo created at: $(Join-Path (Get-Location) $DemoName)" -ForegroundColor Cyan
