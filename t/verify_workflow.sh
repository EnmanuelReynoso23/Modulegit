#!/bin/sh
# Manual Verification Script for ModuleGit Workflow

set -e

echo "=== 1. Setting up Mock Repository ==="
./t/create_mock_repo.sh
ABS_REPO_PATH=$(pwd)/mock-monorepo
echo "Mock repo at: $ABS_REPO_PATH"

echo "\n=== 2. Testing 'modgit list' in main repo ==="
cd mock-monorepo
git modgit list
cd ..

echo "\n=== 3. Testing 'modgit clone' (Module: web) ==="
rm -rf web-dev-env
# Note: In a real scenario, we'd use the URL, here we use file path
git modgit clone --module=web "file://$ABS_REPO_PATH" web-dev-env

echo "\n=== 4. Verifying Sparse Checkout for 'web' ==="
cd web-dev-env
echo "Checking visible files:"
ls -R
# We expect apps/web-frontend, services/auth, services/users, libs/common, libs/db-client
# (Based on dependencies defined in create_mock_repo.sh)

echo "\n=== 5. Testing 'modgit status' ==="
git modgit status

echo "\n=== 6. Testing 'modgit switch' (Switching to: mobile) ==="
git modgit switch mobile
echo "Switched to mobile. Checking visible files:"
ls -R
# We expect apps/mobile-app, services/auth, services/payment, etc.

echo "\n=== 7. Testing 'modgit commit' ==="
echo "// Mobile change" >> apps/mobile-app/App.tsx
git modgit commit "Feat: Add mobile comment"

echo "\n=== 8. Testing 'modgit ai-context' ==="
git modgit ai-context --module=mobile

echo "\n=== Verification Complete! ==="
