# Manual Verification Script for ModuleGit Workflow (PowerShell)

$ErrorActionPreference = "Stop"

Write-Host "=== 1. Setting up Mock Repository ==="
# Ensure we are in the root
$ScriptRoot = $PSScriptRoot
Write-Host "Script Root: $ScriptRoot"
$RepoRoot = Split-Path -Parent $ScriptRoot
Write-Host "Repo Root: $RepoRoot"

# Invoke creation script
& "$PSScriptRoot/create_mock_repo.ps1"

$MockRepoName = "mock-monorepo"
$ABS_REPO_PATH = Join-Path (Get-Location) $MockRepoName

if (-not (Test-Path $ABS_REPO_PATH)) {
    Write-Error "Failed to create mock repo at $ABS_REPO_PATH"
}

Write-Host "Mock repo at: $ABS_REPO_PATH"

Write-Host "`n=== 2. Testing 'modgit list' in main repo ==="
Set-Location $ABS_REPO_PATH
git modgit list
Set-Location $RepoRoot

Write-Host "`n=== 3. Testing 'modgit clone' (Module: web) ==="
if (Test-Path "web-dev-env") {
    Remove-Item -Recurse -Force "web-dev-env"
}
# Using file:// URI for local path
git modgit clone --module=web "file:///$ABS_REPO_PATH" web-dev-env

Write-Host "`n=== 4. Verifying Sparse Checkout for 'web' ==="
Set-Location "web-dev-env"
Write-Host "Checking visible files:"
Get-ChildItem -Recurse | Select-Object FullName

Write-Host "`n=== 5. Testing 'modgit status' ==="
git modgit status

Write-Host "`n=== 6. Testing 'modgit switch' (Switching to: mobile) ==="
git modgit switch mobile
Write-Host "Switched to mobile. Checking visible files:"
Get-ChildItem -Recurse | Select-Object FullName

Write-Host "`n=== 7. Testing 'modgit commit' ==="
"// Mobile change" | Add-Content "apps/mobile-app/App.tsx"
git modgit commit "Feat: Add mobile comment"

Write-Host "`n=== 8. Testing 'modgit ai-context' ==="
git modgit ai-context --module=mobile

Write-Host "`n=== Verification Complete! ==="
