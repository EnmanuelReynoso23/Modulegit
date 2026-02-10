#define USE_THE_REPOSITORY_VARIABLE

#include "builtin.h"
#include "config.h"
#include "parse-options.h"
#include "repository.h"
#include "modgit.h"
#include "strvec.h"
#include "run-command.h"
#include "dir.h"
#include "gettext.h"

static const char * const modgit_usage[] = {
	N_("git modgit clone --module=<name> <url> [dir]"),
	N_("git modgit list"),
	N_("git modgit status"),
	N_("git modgit switch [--full] <module>"),
	N_("git modgit reset"),
	N_("git modgit run <command>"),
	N_("git modgit commit [message]"),
	N_("git modgit commit [message]"),
	N_("git modgit orphan <module>"),
	N_("git modgit ai-context --module=<name>"),
	NULL
};

/*
 * ── Helpers ──────────────────────────────────────────────
 */

static int run_git_cmd_v(struct strvec *args)
{
    struct child_process cmd = CHILD_PROCESS_INIT;
    cmd.git_cmd = 1;
    strvec_pushv(&cmd.args, args->v);
    return run_command(&cmd);
}

static int run_git_cmd(const char **argv)
{
    struct strvec args = STRVEC_INIT;
    for (int i = 0; argv[i]; i++)
        strvec_push(&args, argv[i]);
    int ret = run_git_cmd_v(&args);
    strvec_clear(&args);
    return ret;
}

static void die_with_hint(const char *err, const char *hint)
{
    fprintf(stderr, _("error: %s\n"), err);
    if (hint)
        fprintf(stderr, _("hint: %s\n"), hint);
    exit(128);
}

/*
 * ── Active module state (.git/modgit-active) ─────────────
 * Stores the name of the current module so status/commit know the context.
 */

static void save_active_module(const char *module_name)
{
    FILE *f = fopen(".git/modgit-active", "w");
    if (f) {
        fprintf(f, "%s\n", module_name);
        fclose(f);
    }
}

static char *read_active_module(void)
{
    FILE *f = fopen(".git/modgit-active", "r");
    if (!f) return NULL;
    char buf[256];
    if (!fgets(buf, sizeof(buf), f)) {
        fclose(f);
        return NULL;
    }
    fclose(f);
    // Trim newline
    size_t len = strlen(buf);
    if (len > 0 && buf[len-1] == '\n') buf[len-1] = '\0';
    return xstrdup(buf);
}

static void clear_active_module(void)
{
    unlink(".git/modgit-active");
}

/*
 * ── Check if a file path belongs to the active module ────
 */

static int path_belongs_to_module(const char *filepath, struct strvec *module_paths)
{
    for (int i = 0; i < module_paths->nr; i++) {
        size_t plen = strlen(module_paths->v[i]);
        // filepath starts with module path (directory match)
        if (!strncmp(filepath, module_paths->v[i], plen)) {
            // Exact match or followed by '/'
            if (filepath[plen] == '\0' || filepath[plen] == '/')
                return 1;
        }
    }
    return 0;
}

/*
 * ── SWITCH ───────────────────────────────────────────────
 * Default: SPARSE mode (only show files from your module)
 * --full: overlay mode (see everything, commit only your module)
 *
 * The idea: if you have 500 files but your module only has 4,
 * you only see those 4. Focus on what matters.
 */

static int switch_to_module(const char *module_name)
{
    struct module_def *mod = load_module_def(module_name);
    if (!mod)
        die_with_hint(_("module not found"),
                     _("Run 'git modgit list' to see available modules."));

    struct strvec paths = STRVEC_INIT;
    resolve_dependencies(mod, &paths);

    if (paths.nr == 0)
        die_with_hint(_("module '%s' has no paths defined"), module_name,
                     _("Add 'path = <dir>' entries to your .modgit file."));

    // Enable sparse-checkout: only show module files
    // Use --no-cone to support both directory paths and individual files
    struct strvec sparse_args = STRVEC_INIT;
    strvec_push(&sparse_args, "sparse-checkout");
    strvec_push(&sparse_args, "set");
    strvec_push(&sparse_args, "--no-cone");
    for (int i = 0; i < paths.nr; i++)
        strvec_push(&sparse_args, paths.v[i]);

    if (run_git_cmd_v(&sparse_args))
        die_with_hint(_("failed to configure sparse-checkout"),
                     _("Ensure your git version supports sparse-checkout (v2.25+)"));

    save_active_module(module_name);

    printf(_("\nSwitched to module '%s'\n"), module_name);
    printf(_("  Only these paths are now visible:\n"));
    for (int i = 0; i < paths.nr; i++)
        printf(_("    %s/\n"), paths.v[i]);
    if (mod->depends_on.nr > 0) {
        printf(_("  Dependencies included: "));
        for (int i = 0; i < mod->depends_on.nr; i++)
            printf("%s%s", mod->depends_on.v[i], (i < mod->depends_on.nr - 1) ? ", " : "");
        printf("\n");
    }
    printf(_("\n  Everything else is hidden. Use 'git modgit reset' to restore all files.\n"));

    free_module_def(mod);
    strvec_clear(&paths);
    strvec_clear(&sparse_args);
    return 0;
}

static int switch_to_module_full(const char *module_name)
{
    struct module_def *mod = load_module_def(module_name);
    if (!mod)
        die_with_hint(_("module not found"),
                     _("Run 'git modgit list' to see available modules."));

    // Disable sparse-checkout so ALL files are visible
    const char *disable_args[] = { "sparse-checkout", "disable", NULL };
    run_git_cmd(disable_args);

    save_active_module(module_name);

    struct strvec paths = STRVEC_INIT;
    resolve_dependencies(mod, &paths);

    printf(_("Switched to module '%s' (full mode — all files visible)\n"), module_name);
    printf(_("  Your module paths: "));
    for (int i = 0; i < paths.nr; i++)
        printf("%s%s", paths.v[i], (i < paths.nr - 1) ? ", " : "");
    printf(_("\n  hint: 'git modgit commit' will only commit files inside your module.\n"));

    free_module_def(mod);
    strvec_clear(&paths);
    return 0;
}

static int cmd_modgit_switch(int argc, const char **argv, const char *prefix)
{
    const char *module_name = NULL;
    int use_full = 0;

    struct option options[] = {
        OPT_STRING(0, "module", &module_name, N_("name"), N_("name of the module to switch to")),
        OPT_BOOL(0, "full", &use_full, N_("show all files, not just module files")),
        OPT_END()
    };

    argc = parse_options(argc, argv, prefix, options, modgit_usage, 0);

    if (!module_name && argc > 0)
        module_name = argv[0];

    if (!module_name)
        die(_("module name is required"));

    if (use_full)
        return switch_to_module_full(module_name);
    else
        return switch_to_module(module_name);
}

/*
 * ── CLONE ────────────────────────────────────────────────
 */

static int cmd_modgit_clone(int argc, const char **argv, const char *prefix)
{
	const char *module_name = NULL;

	struct option options[] = {
		OPT_STRING(0, "module", &module_name, N_("name"), N_("name of the module to clone")),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options, modgit_usage, 0);

	if (!module_name)
		die(_("module name is required for clone"));

    if (argc < 1)
        die(_("repository url is required"));

    const char *repo_url = argv[0];
    const char *repo_dir = (argc > 1) ? argv[1] : "repo";

	printf(_("Cloning module '%s' from '%s'...\n"), module_name, repo_url);

    const char *clone_args[] = {
        "clone",
        "--filter=blob:none",
        "--sparse",
        repo_url,
        repo_dir,
        NULL
    };
    if (run_git_cmd(clone_args))
        die_with_hint(_("failed to clone repository"),
                     _("Check your network connection and repository URL access permissions."));

    if (chdir(repo_dir))
        die_errno("cannot chdir to newly cloned repo");

    return switch_to_module(module_name);
}

/*
 * ── LIST ─────────────────────────────────────────────────
 */

static int cmd_modgit_list(int argc, const char **argv, const char *prefix)
{
    struct strvec modules = STRVEC_INIT;

    list_modules(&modules);

    if (modules.nr == 0) {
        printf(_("No modules found.\n"));
        printf(_("hint: Create a .modgit file in the root to define modules.\n"));
    } else {
        char *active = read_active_module();
        printf(_("Available modules:\n"));
        for (int i = 0; i < modules.nr; i++) {
            int is_active = active && !strcmp(modules.v[i], active);
            printf("  %s%s\n", modules.v[i], is_active ? " (active)" : "");
        }
        free(active);
    }

    strvec_clear(&modules);
    return 0;
}

/*
 * ── RUN ──────────────────────────────────────────────────
 */

static int cmd_modgit_run(int argc, const char **argv, const char *prefix)
{
    if (argc < 1)
        die(_("usage: modgit run <command>"));

    printf(_("Running in module context: %s...\n"), argv[0]);

    struct child_process cmd = CHILD_PROCESS_INIT;
    cmd.use_shell = 1;
    for (int i = 0; i < argc; i++)
        strvec_push(&cmd.args, argv[i]);
    int ret = run_command(&cmd);
    if (ret)
        warning(_("command '%s' exited with error code %d"), argv[0], ret);
    return ret;
}

/*
 * ── COMMIT (module-aware) ────────────────────────────────
 * Only stages and commits files that belong to the active module.
 * Files outside the module are shown as warnings but NOT committed.
 */

static int cmd_modgit_commit(int argc, const char **argv, const char *prefix)
{
    const char *msg = (argc > 0) ? argv[0] : "Module update";

    char *active = read_active_module();
    if (!active) {
        // No active module — behave like normal commit
        printf(_("No active module. Committing all changes...\n"));
        const char *add_args[] = { "add", "-u", NULL };
        run_git_cmd(add_args);
        const char *commit_args[] = { "commit", "-m", msg, NULL };
        return run_git_cmd(commit_args);
    }

    printf(_("Committing changes for module '%s'...\n"), active);

    // Load module paths
    struct module_def *mod = load_module_def(active);
    if (!mod)
        die(_("active module '%s' not found in .modgit"), active);

    struct strvec module_paths = STRVEC_INIT;
    resolve_dependencies(mod, &module_paths);

    // Get list of modified files
    struct child_process diff_cmd = CHILD_PROCESS_INIT;
    diff_cmd.git_cmd = 1;
    strvec_push(&diff_cmd.args, "diff");
    strvec_push(&diff_cmd.args, "--name-only");
    diff_cmd.out = -1;  // capture stdout

    if (start_command(&diff_cmd))
        die(_("failed to get changed files"));

    // Read output and classify files
    FILE *fp = fdopen(diff_cmd.out, "r");
    struct strvec inside = STRVEC_INIT;
    struct strvec outside = STRVEC_INIT;
    char line[PATH_MAX];

    while (fgets(line, sizeof(line), fp)) {
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';
        if (path_belongs_to_module(line, &module_paths))
            strvec_push(&inside, line);
        else
            strvec_push(&outside, line);
    }
    fclose(fp);
    finish_command(&diff_cmd);

    // Warn about outside changes
    if (outside.nr > 0) {
        warning(_("The following modified files are OUTSIDE module '%s' and will NOT be committed:"), active);
        for (int i = 0; i < outside.nr; i++)
            fprintf(stderr, "  %s\n", outside.v[i]);
        fprintf(stderr, "\n");
    }

    if (inside.nr == 0) {
        printf(_("No changes inside module '%s'. Nothing to commit.\n"), active);
        free_module_def(mod);
        strvec_clear(&module_paths);
        strvec_clear(&inside);
        strvec_clear(&outside);
        free(active);
        return 0;
    }

    // Stage only files inside the module
    printf(_("Staging %d file(s) from module '%s':\n"), inside.nr, active);
    for (int i = 0; i < inside.nr; i++) {
        printf(_("  + %s\n"), inside.v[i]);
        const char *add_args[] = { "add", inside.v[i], NULL };
        run_git_cmd(add_args);
    }

    // Create branch and commit
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char branch_name[256];
    snprintf(branch_name, sizeof(branch_name), "modgit/%s-%04d%02d%02d-%02d%02d%02d",
             active, t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
             t->tm_hour, t->tm_min, t->tm_sec);

    printf(_("\nCreating branch '%s'...\n"), branch_name);

    const char *checkout_args[] = { "checkout", "-b", branch_name, NULL };
    if (run_git_cmd(checkout_args))
        warning(_("Could not create branch. Committing on current branch."));

    char full_msg[512];
    snprintf(full_msg, sizeof(full_msg), "[%s] %s", active, msg);
    const char *commit_args[] = { "commit", "-m", full_msg, NULL };
    int ret = run_git_cmd(commit_args);

    free_module_def(mod);
    strvec_clear(&module_paths);
    strvec_clear(&inside);
    strvec_clear(&outside);
    free(active);
    return ret;
}

/*
 * ── STATUS (module-aware) ────────────────────────────────
 * Shows which changed files are inside/outside your module.
 */

static int cmd_modgit_status(int argc, const char **argv, const char *prefix)
{
    char *active = read_active_module();

    printf(_("ModuleGit Status:\n\n"));

    if (active) {
        printf(_("  Active module: %s\n"), active);

        // Load module and resolve paths
        struct module_def *mod = load_module_def(active);
        if (mod) {
            struct strvec module_paths = STRVEC_INIT;
            resolve_dependencies(mod, &module_paths);

            printf(_("  Module paths:\n"));
            for (int i = 0; i < module_paths.nr; i++)
                printf(_("    %s/\n"), module_paths.v[i]);

            // Check for modified files and classify
            struct child_process diff_cmd = CHILD_PROCESS_INIT;
            diff_cmd.git_cmd = 1;
            strvec_push(&diff_cmd.args, "diff");
            strvec_push(&diff_cmd.args, "--name-only");
            diff_cmd.out = -1;

            if (!start_command(&diff_cmd)) {
                FILE *fp = fdopen(diff_cmd.out, "r");
                struct strvec inside = STRVEC_INIT;
                struct strvec outside = STRVEC_INIT;
                char line[PATH_MAX];

                while (fgets(line, sizeof(line), fp)) {
                    size_t len = strlen(line);
                    if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';
                    if (len == 0) continue;
                    if (path_belongs_to_module(line, &module_paths))
                        strvec_push(&inside, line);
                    else
                        strvec_push(&outside, line);
                }
                fclose(fp);
                finish_command(&diff_cmd);

                printf(_("\n  Changes INSIDE your module (%d):\n"), inside.nr);
                if (inside.nr == 0)
                    printf(_("    (none)\n"));
                for (int i = 0; i < inside.nr; i++)
                    printf(_("    \033[32m+ %s\033[0m\n"), inside.v[i]);

                printf(_("\n  Changes OUTSIDE your module (%d):\n"), outside.nr);
                if (outside.nr == 0)
                    printf(_("    (none)\n"));
                for (int i = 0; i < outside.nr; i++)
                    printf(_("    \033[31m! %s\033[0m (not yours)\n"), outside.v[i]);

                strvec_clear(&inside);
                strvec_clear(&outside);
            }

            free_module_def(mod);
            strvec_clear(&module_paths);
        }
        free(active);
    } else {
        printf(_("  No active module (full repository mode)\n"));
        printf(_("  hint: Use 'git modgit switch <module>' to focus on a module.\n"));
    }

    printf(_("\n"));

    // Show available modules
    struct strvec modules = STRVEC_INIT;
    list_modules(&modules);
    if (modules.nr > 0) {
        printf(_("  Defined modules: "));
        for (int i = 0; i < modules.nr; i++)
            printf("%s%s", modules.v[i], (i < modules.nr - 1) ? ", " : "");
        printf("\n");
    }
    strvec_clear(&modules);

    printf(_("\n"));
    const char *status_args[] = { "status", "--short", NULL };
    return run_git_cmd(status_args);
}

/*
 * ── RESET ────────────────────────────────────────────────
 */

static int cmd_modgit_reset(int argc, const char **argv, const char *prefix)
{
    printf(_("Resetting to full repository (deactivating module mode)...\n"));

    const char *disable_args[] = { "sparse-checkout", "disable", NULL };
    run_git_cmd(disable_args); // best effort

    clear_active_module();

    printf(_("Done. Module mode deactivated. All files visible, no restrictions.\n"));
    return 0;
}

/*
 * ── ORPHAN (Module as Branch) ────────────────────────────
 * Creates an orphan branch containing ONLY the module files.
 */

static int cmd_modgit_orphan(int argc, const char **argv, const char *prefix)
{
    const char *module_name = NULL;

    struct option options[] = {
        OPT_STRING(0, "module", &module_name, N_("name"), N_("name of the module")),
        OPT_END()
    };

    argc = parse_options(argc, argv, prefix, options, modgit_usage, 0);

    if (!module_name && argc > 0)
        module_name = argv[0];

    if (!module_name)
        die(_("module name is required"));

    struct module_def *mod = load_module_def(module_name);
    if (!mod)
        die(_("module '%s' not found"), module_name);

    struct strvec paths = STRVEC_INIT;
    resolve_dependencies(mod, &paths);

    char branch_name[256];
    snprintf(branch_name, sizeof(branch_name), "module/%s", module_name);

    printf(_("Switching to isolated module branch '%s'...\n"), branch_name);

    // 1. Try to checkout existing branch first
    struct child_process check_cmd = CHILD_PROCESS_INIT;
    check_cmd.git_cmd = 1;
    strvec_pushl(&check_cmd.args, "rev-parse", "--verify", branch_name, NULL);
    check_cmd.no_stdout = 1;
    check_cmd.no_stderr = 1;
    
    if (run_command(&check_cmd) == 0) {
        // Branch exists, just switch
        const char *switch_args[] = { "switch", branch_name, NULL };
        return run_git_cmd(switch_args);
    }

    // 2. Checkout orphan branch (keeps index/worktree from current HEAD)
    const char *orphan_args[] = { "checkout", "--orphan", branch_name, NULL };
    if (run_git_cmd(orphan_args))
        die(_("failed to create orphan branch"));

    // 3. Unstage everything (reset index)
    const char *reset_args[] = { "reset", NULL };
    if (run_git_cmd(reset_args))
        die(_("failed to reset index"));

    // 4. Add only module paths
    struct strvec add_args = STRVEC_INIT;
    strvec_push(&add_args, "add");
    for (int i = 0; i < paths.nr; i++)
        strvec_push(&add_args, paths.v[i]);
    
    // Also add .modgit so we keep module definitions!
    strvec_push(&add_args, ".modgit");

    if (run_git_cmd_v(&add_args))
        die(_("failed to add module files"));

    // 5. Commit
    const char *commit_args[] = { "commit", "-m", "Initialize module branch", NULL };
    if (run_git_cmd(commit_args))
        die(_("failed to commit module files"));

    // 6. Clean untracked files (removes everything else from worktree)
    // -f: force, -d: directories, -x: ignored files too
    printf(_("Cleaning up non-module files...\n"));
    const char *clean_args[] = { "clean", "-fd", NULL }; 
    if (run_git_cmd(clean_args))
        warning(_("failed to clean up non-module files"));

    printf(_("Success! You are now on isolated branch '%s'.\n"), branch_name);
    
    free_module_def(mod);
    strvec_clear(&paths);
    strvec_clear(&add_args);
    return 0;
}

/*
 * ── AI-CONTEXT ───────────────────────────────────────────
 */

static int cmd_modgit_ai_context(int argc, const char **argv, const char *prefix)
{
    const char *module_name = NULL;

    struct option options[] = {
        OPT_STRING(0, "module", &module_name, N_("name"), N_("name of the module")),
        OPT_END()
    };

    argc = parse_options(argc, argv, prefix, options, modgit_usage, 0);

    if (!module_name)
        die(_("module name is required"));

    struct module_def *mod = load_module_def(module_name);
    if (!mod)
        die(_("module '%s' not found"), module_name);

    struct strvec paths = STRVEC_INIT;
    resolve_dependencies(mod, &paths);

    printf("Subject: Context for module '%s'\n\n", module_name);
    printf("This context includes the following paths:\n");
    for (int i = 0; i < paths.nr; i++)
        printf("- %s\n", paths.v[i]);

    free_module_def(mod);
    strvec_clear(&paths);
    return 0;
}

/*
 * ── MAIN DISPATCH ────────────────────────────────────────
 */

int cmd_modgit(int argc, const char **argv, const char *prefix, struct repository *repo)
{
	parse_opt_subcommand_fn *fn = NULL;
	struct option options[] = {
		OPT_SUBCOMMAND("clone", &fn, cmd_modgit_clone),
		OPT_SUBCOMMAND("list", &fn, cmd_modgit_list),
		OPT_SUBCOMMAND("status", &fn, cmd_modgit_status),
		OPT_SUBCOMMAND("switch", &fn, cmd_modgit_switch),
		OPT_SUBCOMMAND("reset", &fn, cmd_modgit_reset),
		OPT_SUBCOMMAND("run", &fn, cmd_modgit_run),
		OPT_SUBCOMMAND("commit", &fn, cmd_modgit_commit),
		OPT_SUBCOMMAND("orphan", &fn, cmd_modgit_orphan),
		OPT_SUBCOMMAND("ai-context", &fn, cmd_modgit_ai_context),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options, modgit_usage, 0);

    if (fn)
		return fn(argc, argv, prefix);

    usage_with_options(modgit_usage, options);
    return 1;
}
