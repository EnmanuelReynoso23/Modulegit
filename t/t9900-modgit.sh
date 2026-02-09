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

test_done
