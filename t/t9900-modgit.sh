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

test_done
