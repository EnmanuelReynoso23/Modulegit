#!/bin/sh

test_description='Verify ModuleGit orphan logic (manual simulation)'

. ./test-lib.sh

test_expect_success 'setup repo' '
    git init repo &&
    cd repo &&
    mkdir -p apps/web apps/api libs/shared &&
    echo "web" > apps/web/index.js &&
    echo "api" > apps/api/main.js &&
    echo "shared" > libs/shared/utils.js &&
    echo "root" > README.md &&
    cat >.modgit <<-\EOF &&
    [module "web"]
        path = apps/web
        depends = shared
    [module "shared"]
        path = libs/shared
    EOF
    git add . &&
    git commit -m "Initial commit"
'

test_expect_success 'simulate git modgit orphan web' '
    # 1. Checkout orphan
    git checkout --orphan module/web &&
    
    # 2. Reset index (unstage everything)
    git reset &&
    
    # 3. Add module paths (simulating resolution: apps/web and libs/shared + .modgit)
    git add apps/web libs/shared .modgit &&
    
    # 4. Commit
    git commit -m "Initialize module branch" &&
    
    # 5. Clean untracked (remove non-module files)
    git clean -fd &&
    
    # VERIFICATION
    # README.md and apps/api should be GONE
    test_path_is_missing README.md &&
    test_path_is_missing apps/api &&
    
    # Module files should EXIST
    test_path_is_file apps/web/index.js &&
    test_path_is_file libs/shared/utils.js &&
    test_path_is_file .modgit
'

test_done
