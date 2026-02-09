# PowerShell Script to generate a mock "System" monorepo for ModuleGit testing

$ErrorActionPreference = "Stop"

if (Test-Path "mock-monorepo") {
    Remove-Item -Recurse -Force "mock-monorepo"
}

New-Item -ItemType Directory -Force -Path "mock-monorepo" | Out-Null
Set-Location "mock-monorepo"
git init

# Create directory structure
New-Item -ItemType Directory -Force -Path "services/auth" | Out-Null
New-Item -ItemType Directory -Force -Path "services/payment" | Out-Null
New-Item -ItemType Directory -Force -Path "services/users" | Out-Null
New-Item -ItemType Directory -Force -Path "libs/common" | Out-Null
New-Item -ItemType Directory -Force -Path "libs/db-client" | Out-Null
New-Item -ItemType Directory -Force -Path "apps/web-frontend" | Out-Null
New-Item -ItemType Directory -Force -Path "apps/mobile-app" | Out-Null

# Create some dummy files
"Auth Service" | Set-Content "services/auth/main.go"
"Payment Service" | Set-Content "services/payment/main.go"
"Users Service" | Set-Content "services/users/main.go"
"Common Lib" | Set-Content "libs/common/utils.go"
"DB Client" | Set-Content "libs/db-client/client.go"
"Web App" | Set-Content "apps/web-frontend/index.html"
"Mobile App" | Set-Content "apps/mobile-app/App.tsx"

# Create .modgit configuration
$config = @"
[module "auth"]
    path = services/auth
    depends = common
    depends = db-client

[module "payment"]
    path = services/payment
    depends = common
    depends = db-client

[module "users"]
    path = services/users
    depends = common

[module "web"]
    path = apps/web-frontend
    depends = auth
    depends = users

[module "mobile"]
    path = apps/mobile-app
    depends = auth
    depends = payment

[module "common"]
    path = libs/common

[module "db-client"]
    path = libs/db-client
    depends = common
"@

$config | Set-Content ".modgit" -Encoding ASCII

git add .
git commit -m "Initial commit of mock monorepo"

Write-Host "Mock repository created at $(Get-Location)"
