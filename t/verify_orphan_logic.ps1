# verify_orphan_logic.ps1 - Verify ModuleGit orphan logic

$ErrorActionPreference = "Continue" # Don't stop on stderr from git

function Assert-PathExists($Path) {
    if (!(Test-Path $Path)) {
        Write-Error "Assertion Failed: Path '$Path' should exist."
        exit 1
    }
}

function Assert-PathMissing($Path) {
    if (Test-Path $Path) {
        Write-Error "Assertion Failed: Path '$Path' should NOT exist."
        exit 1
    }
}

Write-Host "--- Verifying Module Orphan Logic ---" -ForegroundColor Cyan

# Setup
if (Test-Path "repo_orphan_test") { Remove-Item -Recurse -Force "repo_orphan_test" }
New-Item -ItemType Directory -Path "repo_orphan_test" | Out-Null
Set-Location "repo_orphan_test"

git init | Out-Null
git config user.email "you@example.com"
git config user.name "Your Name"

# Create initial structure
New-Item -ItemType Directory -Path "apps/web" | Out-Null
New-Item -ItemType Directory -Path "apps/api" | Out-Null
New-Item -ItemType Directory -Path "libs/shared" | Out-Null

"web" > "apps/web/index.js"
"api" > "apps/api/main.js"
"shared" > "libs/shared/utils.js"
"root" > "README.md"

# Create .modgit
@"
[module "web"]
    path = apps/web
    depends = shared
[module "shared"]
    path = libs/shared
"@ | Out-File -FilePath .modgit -Encoding utf8

git add .
git commit -m "Initial commit" | Out-Null

Write-Host "[1/4] Initial commit created." -ForegroundColor Gray

# SIMULATE ORPHAN COMMAND
Write-Host "[2/4] Simulating 'git modgit orphan web' logic..." -ForegroundColor Yellow

# 1. Checkout orphan
git checkout --orphan "module/web" 2>$null
if ($LASTEXITCODE -ne 0) { Write-Error "Checkout orphan failed"; exit 1 }

# 2. Reset index
git reset 2>$null
if ($LASTEXITCODE -ne 0) { Write-Error "Reset failed"; exit 1 }

# 3. Add module paths
git add "apps/web/index.js" "libs/shared/utils.js" ".modgit"
if ($LASTEXITCODE -ne 0) { Write-Error "Add failed"; exit 1 }

# 4. Commit
git commit -m "Initialize module branch" | Out-Null
if ($LASTEXITCODE -ne 0) { Write-Error "Commit failed"; exit 1 }

# 5. Clean untracked
git clean -fd 2>$null
if ($LASTEXITCODE -ne 0) { Write-Error "Clean failed"; exit 1 }

Write-Host "[3/4] Checking results..." -ForegroundColor Yellow

# VERIFICATION
Assert-PathMissing "README.md"
Assert-PathMissing "apps/api/main.js"

Assert-PathExists "apps/web/index.js"
Assert-PathExists "libs/shared/utils.js"
Assert-PathExists ".modgit"

Write-Host "SUCCESS: Orphan branch contains ONLY module files!" -ForegroundColor Green

Set-Location ..
remove-item -recurse -force repo_orphan_test
