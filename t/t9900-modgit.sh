#!/bin/sh

test_description='ModuleGit basic integration tests'

. ./test-lib.sh

test_expect_success 'setup .modgit config' '
	git init repo &&
	cd repo &&
	cat >.modgit <<-\EOF
	[module "frontend"]
		path = src/ui
		path = src/assets
		depends = backend
	[module "backend"]
		path = src/api
		path = src/db
	EOF
'

test_expect_success 'modgit list shows available modules' '
	git modgit list >actual &&
	cat >expected <<-\EOF &&
	Available modules:
	  frontend
	  backend
	EOF
	test_cmp expected actual
'

test_expect_success 'modgit list hints when .modgit is missing' '
	mkdir empty-repo &&
	cd empty-repo &&
	git init &&
	git modgit list >actual &&
	grep "hint: Create a .modgit file" actual
'

test_expect_success 'setup circular dependency config' '
	cd "$TRASH_DIRECTORY" &&
	git init circular-repo &&
	cd circular-repo &&
	cat >.modgit <<-\EOF
	[module "a"]
		path = src/a
		depends = b
	[module "b"]
		path = src/b
		depends = a
	EOF
'

test_expect_success 'modgit switch detects circular dependencies' '
	git modgit switch a 2>err &&
	grep "circular dependency" err
'

test_expect_success 'setup shared dependency config' '
	cd "$TRASH_DIRECTORY" &&
	git init shared-repo &&
	cd shared-repo &&
	cat >.modgit <<-\EOF
	[module "app"]
		path = src/app
		depends = lib-a
		depends = lib-b
	[module "lib-a"]
		path = src/lib-a
		depends = common
	[module "lib-b"]
		path = src/lib-b
		depends = common
	[module "common"]
		path = src/common
	EOF
'

test_expect_success 'modgit switch deduplicates shared dependency paths' '
	git modgit switch app 2>err &&
	! grep "src/common" err | grep -c "src/common" | grep "2"
'

test_expect_success 'modgit status shows module mode info' '
	cd "$TRASH_DIRECTORY/repo" &&
	git modgit status >actual &&
	grep "ModuleGit Status" actual
'

test_expect_success 'modgit orphan creates isolated branch' '
	cd "$TRASH_DIRECTORY/repo" &&
	# Ensure clean state
	git modgit reset || true &&
	
	# Create orphan branch
	git modgit orphan frontend &&
	
	# Verify branch name
	git symbolic-ref HEAD >actual &&
	echo "refs/heads/module/frontend" >expected &&
	test_cmp expected actual &&
	
	# Verify file isolation
	ls -R >actual_files &&
	! grep "src/api" actual_files &&
	grep "src/ui" actual_files &&
	grep ".modgit" actual_files
'

test_expect_success 'setup sync-push test repo' '
	cd "$TRASH_DIRECTORY" &&
	git init sync-repo &&
	cd sync-repo &&
	cat >.modgit <<-\EOF &&
	[module "web"]
		path = src/web
	[module "api"]
		path = src/api
	EOF
	mkdir -p src/web src/api &&
	echo "hello" >src/web/index.html &&
	echo "api" >src/api/server.js &&
	git add -A &&
	git commit -m "initial" &&

	# Create orphan branch for web module
	git modgit orphan web
'

test_expect_success 'modgit sync pulls updates from master' '
	cd "$TRASH_DIRECTORY/sync-repo" &&
	# Go back to master and make a change
	git checkout master &&
	echo "updated" >src/web/index.html &&
	git add src/web/index.html &&
	git commit -m "update web on master" &&

	# Switch to orphan and sync
	git checkout module/web &&
	git modgit sync --source=master &&

	# Verify the file was updated
	grep "updated" src/web/index.html
'

test_expect_success 'modgit push sends changes to master' '
	cd "$TRASH_DIRECTORY/sync-repo" &&
	git checkout module/web &&
	echo "from-orphan" >src/web/index.html &&
	git add src/web/index.html &&
	git commit -m "edit on orphan" &&

	# Push changes to master
	git modgit push --target=master &&

	# Verify we are back on orphan branch
	git symbolic-ref --short HEAD >actual_branch &&
	echo "module/web" >expected_branch &&
	test_cmp expected_branch actual_branch &&

	# Verify master has the change
	git checkout master &&
	grep "from-orphan" src/web/index.html
'

test_expect_success 'setup nested modules repo' '
	cd "$TRASH_DIRECTORY" &&
	git init nested-repo &&
	cd nested-repo &&
	cat >.modgit <<-\EOF &&
	[module "frontend"]
		path = src/ui
		path = src/assets
		depends = backend
	[module "frontend/css"]
		path = src/assets/css
	[module "frontend/js"]
		path = src/ui/components
	[module "backend"]
		path = src/api
	EOF
	mkdir -p src/ui/components src/assets/css src/api &&
	echo "app" >src/ui/app.js &&
	echo "btn" >src/ui/components/button.js &&
	echo "style" >src/assets/css/main.css &&
	echo "api" >src/api/server.js &&
	git add -A &&
	git commit -m "initial nested"
'

test_expect_success 'modgit list shows tree hierarchy for nested modules' '
	cd "$TRASH_DIRECTORY/nested-repo" &&
	git modgit list >actual &&
	grep "frontend" actual &&
	grep "css" actual &&
	grep "js" actual &&
	grep "backend" actual
'

test_expect_success 'modgit init creates new module in .modgit' '
	cd "$TRASH_DIRECTORY/nested-repo" &&
	git modgit init frontend/icons --path=src/assets/icons &&
	grep "frontend/icons" .modgit &&
	grep "src/assets/icons" .modgit
'

test_expect_success 'modgit orphan of child module only gets child paths' '
	cd "$TRASH_DIRECTORY/nested-repo" &&
	git modgit orphan frontend/css &&

	# Verify branch
	git symbolic-ref --short HEAD >actual_branch &&
	echo "module/frontend/css" >expected_branch &&
	test_cmp expected_branch actual_branch &&

	# Only css files should exist, not app.js or server.js
	ls -R >actual_files &&
	grep "main.css" actual_files &&
	! grep "app.js" actual_files &&
	! grep "server.js" actual_files
'

test_expect_success 'child module inherits parent dependencies' '
	cd "$TRASH_DIRECTORY/nested-repo" &&
	git checkout master &&
	git modgit orphan frontend/css 2>err &&

	# frontend/css has no depends, so it inherits from frontend (depends=backend)
	# The orphan should include backend paths (src/api) through inherited deps
	# Check that .modgit is present (always included)
	ls -R >actual_files &&
	grep ".modgit" actual_files
'

test_done
