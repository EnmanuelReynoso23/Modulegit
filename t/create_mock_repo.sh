#!/bin/sh
# Generate a mock "System" monorepo for ModuleGit testing

mkdir -p mock-monorepo
cd mock-monorepo
git init

# Create directory structure
mkdir -p services/auth
mkdir -p services/payment
mkdir -p services/users
mkdir -p libs/common
mkdir -p libs/db-client
mkdir -p apps/web-frontend
mkdir -p apps/mobile-app

# Create some dummy files
echo "Auth Service" > services/auth/main.go
echo "Payment Service" > services/payment/main.go
echo "Users Service" > services/users/main.go
echo "Common Lib" > libs/common/utils.go
echo "DB Client" > libs/db-client/client.go
echo "Web App" > apps/web-frontend/index.html
echo "Mobile App" > apps/mobile-app/App.tsx

# Create .modgit configuration
cat >.modgit <<EOF
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
EOF

git add .
git commit -m "Initial commit of mock monorepo"

echo "Mock repository created at $(pwd)"
