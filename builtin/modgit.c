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
	N_("git modgit switch [--full|--dev] <module>"),
	N_("git modgit reset"),
	N_("git modgit run <command>"),
	N_("git modgit commit [message]"),
	N_("git modgit orphan <module>"),
	N_("git modgit sync [--source=<branch>]"),
	N_("git modgit push [--target=<branch>]"),
	N_("git modgit init <name> --path=<p> [--path=<p>...] [--depends=<d>...]"),
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
 * --dev:  infrastructure mode (see module + root config, hide other modules)
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
    {
        char msg[256];
        snprintf(msg, sizeof(msg), _("module '%s' has no paths defined"), module_name);
        die_with_hint(msg, _("Add 'path = <dir>' entries to your .modgit file."));
    }

    // Enable sparse-checkout: only show module files
    // Use --no-cone to support both directory paths and individual files
    struct strvec sparse_args = STRVEC_INIT;
    strvec_push(&sparse_args, "sparse-checkout");
    strvec_push(&sparse_args, "set");
    strvec_push(&sparse_args, "--no-cone");
    for (int i = 0; i < paths.nr; i++)
        strvec_push(&sparse_args, paths.v[i]);
    // Always include .modgit so module definitions remain accessible
    strvec_push(&sparse_args, ".modgit");

    if (run_git_cmd_v(&sparse_args))
        die_with_hint(_("failed to configure sparse-checkout"),
                     _("Ensure your git version supports sparse-checkout (v2.25+)"));

    save_active_module(module_name);
    // Clear dev mode if it exists
    unlink(".git/modgit-mode");

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
    unlink(".git/modgit-mode");

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

/*
 * ── Helper: checks if 'parent' is a directory prefix of 'child'
 * e.g. parent="apps/web", child="apps/web/src/marketing" -> 1
 */
static int path_is_parent(const char *parent, const char *child)
{
    size_t plen = strlen(parent);
    size_t clen = strlen(child);
    if (plen >= clen) return 0;
    if (strncmp(parent, child, plen)) return 0;
    return child[plen] == '/';
}

static int switch_to_module_dev(const char *module_name)
{
    struct module_def *mod = load_module_def(module_name);
    if (!mod)
        die_with_hint(_("module not found"),
                     _("Run 'git modgit list' to see available modules."));

    struct strvec allowed_paths = STRVEC_INIT;
    resolve_dependencies(mod, &allowed_paths);

    if (allowed_paths.nr == 0)
    {
        char msg[256];
        snprintf(msg, sizeof(msg), _("module '%s' has no paths defined"), module_name);
        die_with_hint(msg, _("Add 'path = <dir>' entries to your .modgit file."));
    }

    struct strvec sparse_args = STRVEC_INIT;
    strvec_push(&sparse_args, "sparse-checkout");
    strvec_push(&sparse_args, "set");
    strvec_push(&sparse_args, "--no-cone");
    
    // 1. Include everything by default (infrastructure, configs, etc.)
    strvec_push(&sparse_args, "/*");

    // 2. Exclude other modules specifically
    struct strvec all_modules = STRVEC_INIT;
    list_modules(&all_modules);

    for (int i = 0; i < all_modules.nr; i++) {
        const char *other_name = all_modules.v[i];
        
        // Skip ourself
        if (!strcmp(other_name, module_name))
            continue;

        struct module_def *other = load_module_def(other_name);
        if (!other) continue;

        // Skip infrastructure modules (they should remain visible)
        if (other->is_infrastructure) {
            free_module_def(other);
            continue;
        }

        for (int p = 0; p < other->paths.nr; p++) {
            const char *forbidden_path = other->paths.v[p];
            
            // Check if this path is allowed (part of our dependency tree)
            int is_allowed = 0;
            for (int k = 0; k < allowed_paths.nr; k++) {
                if (!strcmp(allowed_paths.v[k], forbidden_path)) {
                    is_allowed = 1;
                    break;
                }
            }
            if (is_allowed) continue;

            // Check if this path is a parent of an allowed path
            // (If so, we MUST NOT exclude it, or we'll break the path to our module)
            int is_parent_path = 0;
            for (int k = 0; k < allowed_paths.nr; k++) {
                if (path_is_parent(forbidden_path, allowed_paths.v[k])) {
                    is_parent_path = 1;
                    break;
                }
            }
            if (is_parent_path) continue;

            // Exclude this path (and its children)
            // !/path matches directory
            // !/path/* matches contents
            char *pat1 = xstrfmt("!/%s", forbidden_path);
            char *pat2 = xstrfmt("!/%s/*", forbidden_path);
            
            strvec_push(&sparse_args, pat1);
            strvec_push(&sparse_args, pat2);
            
            free(pat1);
            free(pat2);
        }
        free_module_def(other);
    }
    strvec_clear(&all_modules);

    if (run_git_cmd_v(&sparse_args))
        die_with_hint(_("failed to configure sparse-checkout"),
                     _("Ensure your git version supports sparse-checkout (v2.25+)"));

    save_active_module(module_name);
    
    // Save dev mode state
    FILE *f_mode = fopen(".git/modgit-mode", "w");
    if (f_mode) {
        fprintf(f_mode, "dev");
        fclose(f_mode);
    }

    printf(_("\nSwitched to module '%s' (dev mode)\n"), module_name);
    printf(_("  Mode: DEV (Infrastructure visible, other modules hidden)\n"));
    printf(_("  Visible:\n    - %s (and dependencies)\n    - Project root files & infrastructure\n"), module_name);
    printf(_("  Hidden:\n    - Other unrelated modules\n"));
    printf(_("  Ready for 'npm run dev' or equivalent!\n"));

    free_module_def(mod);
    strvec_clear(&allowed_paths);
    strvec_clear(&sparse_args);
    return 0;
}

static int cmd_modgit_switch(int argc, const char **argv, const char *prefix, struct repository *repo)
{
    const char *module_name = NULL;
    int use_full = 0;
    int use_dev = 0;

    struct option options[] = {
        OPT_STRING(0, "module", &module_name, N_("name"), N_("name of the module to switch to")),
        OPT_BOOL(0, "full", &use_full, N_("show all files, not just module files")),
        OPT_BOOL(0, "dev", &use_dev, N_("dev mode: show module + infrastructure, hide other modules")),
        OPT_END()
    };

    argc = parse_options(argc, argv, prefix, options, modgit_usage, 0);

    if (!module_name && argc > 0)
        module_name = argv[0];

    if (!module_name)
        die(_("module name is required"));

    if (use_full && use_dev)
        die(_("--full and --dev are Mutually Exclusive"));

    if (use_full)
        return switch_to_module_full(module_name);
    else if (use_dev)
        return switch_to_module_dev(module_name);
    else
        return switch_to_module(module_name);
}

/*
 * ── CLONE ────────────────────────────────────────────────
 */

static int cmd_modgit_clone(int argc, const char **argv, const char *prefix, struct repository *repo)
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

/*
 * Check if module name is a root (no '/') at a given depth.
 * Returns the "display name" part after the parent prefix, or full name if root.
 */
static int module_depth(const char *name)
{
    int depth = 0;
    for (const char *p = name; *p; p++)
        if (*p == '/') depth++;
    return depth;
}

static const char *module_leaf_name(const char *name)
{
    const char *last_slash = strrchr(name, '/');
    return last_slash ? last_slash + 1 : name;
}

static int is_child_of(const char *child, const char *parent)
{
    size_t plen = strlen(parent);
    return !strncmp(child, parent, plen) && child[plen] == '/';
}

/* Count how many siblings follow at the same depth under the same parent */
static int is_last_sibling(const struct strvec *modules, int idx)
{
    const char *name = modules->v[idx];
    int depth = module_depth(name);

    /* Find parent prefix */
    const char *last_slash = strrchr(name, '/');
    size_t prefix_len = last_slash ? (size_t)(last_slash - name + 1) : 0;

    for (int j = idx + 1; j < modules->nr; j++) {
        int jdepth = module_depth(modules->v[j]);
        if (jdepth == depth) {
            if (prefix_len == 0)
                return 0; /* another root module follows */
            if (!strncmp(modules->v[j], name, prefix_len))
                return 0; /* another sibling follows */
            return 1; /* different parent */
        }
        if (jdepth < depth)
            return 1; /* back to parent level */
    }
    return 1; /* end of list */
}

static int cmd_modgit_list(int argc, const char **argv, const char *prefix, struct repository *repo)
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
            int depth = module_depth(modules.v[i]);

            if (depth == 0) {
                /* Root module */
                printf("  %s%s\n", modules.v[i],
                       is_active ? " \033[32m(active)\033[0m" : "");
            } else {
                /* Child module — show with tree connector */
                int last = is_last_sibling(&modules, i);
                const char *connector = last ? "\xe2\x94\x94\xe2\x94\x80\xe2\x94\x80" : "\xe2\x94\x9c\xe2\x94\x80\xe2\x94\x80";

                /* Indent based on depth */
                printf("  ");
                for (int d = 1; d < depth; d++)
                    printf("    ");
                printf("  %s %s%s\n", connector, module_leaf_name(modules.v[i]),
                       is_active ? " \033[32m(active)\033[0m" : "");
            }
        }
        free(active);
    }

    strvec_clear(&modules);
    return 0;
}

/*
 * ── RUN ──────────────────────────────────────────────────
 */

static int cmd_modgit_run(int argc, const char **argv, const char *prefix, struct repository *repo)
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

static int cmd_modgit_commit(int argc, const char **argv, const char *prefix, struct repository *repo)
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
    struct tm tm_buf;
    struct tm *t = localtime_r(&now, &tm_buf);
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

static int cmd_modgit_status(int argc, const char **argv, const char *prefix, struct repository *repo)
{
    char *active = read_active_module();

    printf(_("ModuleGit Status:\n\n"));

    // Check if we are in dev mode
    int active_is_dev_mode = 0;
    FILE *f_mode = fopen(".git/modgit-mode", "r");
    if (f_mode) {
        char mode_buf[64];
        if (fgets(mode_buf, sizeof(mode_buf), f_mode)) {
            if (strstr(mode_buf, "dev"))
                active_is_dev_mode = 1;
        }
        fclose(f_mode);
    }

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

                printf(_("\n  Changes INSIDE your module (%zu):\n"), inside.nr);
                if (inside.nr == 0)
                    printf(_("    (none)\n"));
                for (int i = 0; i < inside.nr; i++)
                    printf(_("    \033[32m+ %s\033[0m\n"), inside.v[i]);

                printf(_("\n  Changes OUTSIDE your module (%zu):\n"), outside.nr);
                if (outside.nr == 0)
                    printf(_("    (none)\n"));
                for (int i = 0; i < outside.nr; i++)
                    printf(_("    \033[31m! %s\033[0m (not yours)\n"), outside.v[i]);

                if (active_is_dev_mode) {
                     printf(_("\n  Note: In --dev mode, changes to project infrastructure files are allowed.\n"));
                }

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

static int cmd_modgit_reset(int argc, const char **argv, const char *prefix, struct repository *repo)
{
    printf(_("Resetting to full repository (deactivating module mode)...\n"));

    const char *disable_args[] = { "sparse-checkout", "disable", NULL };
    run_git_cmd(disable_args); // best effort

    clear_active_module();
    unlink(".git/modgit-mode");

    printf(_("Done. Module mode deactivated. All files visible, no restrictions.\n"));
    return 0;
}

/*
 * ── ORPHAN (Module as Branch) ────────────────────────────
 * Creates an orphan branch containing ONLY the module files.
 */

static int cmd_modgit_orphan(int argc, const char **argv, const char *prefix, struct repository *repo)
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

    /* Clean untracked files that might block branch switching */
    const char *clean_args[] = { "clean", "-fd", NULL };
    run_git_cmd(clean_args);

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
    const char *clean_args2[] = { "clean", "-fd", NULL };
    if (run_git_cmd(clean_args2))
        warning(_("failed to clean up non-module files"));

    printf(_("Success! You are now on isolated branch '%s'.\n"), branch_name);
    
    free_module_def(mod);
    strvec_clear(&paths);
    strvec_clear(&add_args);
    return 0;
}

/*
 * ── Helper: detect module name from branch ───────────────
 * If on branch "module/foo", returns "foo". Caller must free().
 */
static char *detect_module_from_branch(void)
{
    struct child_process cmd = CHILD_PROCESS_INIT;
    cmd.git_cmd = 1;
    strvec_pushl(&cmd.args, "symbolic-ref", "--short", "HEAD", NULL);
    cmd.out = -1;

    if (start_command(&cmd))
        return NULL;

    FILE *fp = fdopen(cmd.out, "r");
    char buf[256];
    if (!fgets(buf, sizeof(buf), fp)) {
        fclose(fp);
        finish_command(&cmd);
        return NULL;
    }
    fclose(fp);
    finish_command(&cmd);

    size_t len = strlen(buf);
    if (len > 0 && buf[len-1] == '\n') buf[len-1] = '\0';

    const char *module_part;
    if (!skip_prefix(buf, "module/", &module_part))
        return NULL;

    return xstrdup(module_part);
}

/*
 * ── SYNC (pull updates from source into orphan branch) ───
 * Checks out module files from the source branch (default: master)
 * into the current orphan branch.
 */

static int cmd_modgit_sync(int argc, const char **argv, const char *prefix, struct repository *repo)
{
    const char *source_branch = "master";

    struct option options[] = {
        OPT_STRING(0, "source", &source_branch, N_("branch"),
                   N_("source branch to sync from (default: master)")),
        OPT_END()
    };

    argc = parse_options(argc, argv, prefix, options, modgit_usage, 0);

    // Detect module from branch name
    char *module_name = detect_module_from_branch();
    if (!module_name)
        die(_("not on a module branch (expected branch 'module/<name>').\n"
              "Use 'git modgit orphan <module>' first."));

    struct module_def *mod = load_module_def(module_name);
    if (!mod)
        die(_("module '%s' not found in .modgit"), module_name);

    struct strvec paths = STRVEC_INIT;
    resolve_dependencies(mod, &paths);

    printf(_("Syncing module '%s' from '%s'...\n"), module_name, source_branch);

    // Checkout module files from source branch
    struct strvec checkout_args = STRVEC_INIT;
    strvec_push(&checkout_args, "checkout");
    strvec_push(&checkout_args, source_branch);
    strvec_push(&checkout_args, "--");
    for (int i = 0; i < paths.nr; i++)
        strvec_push(&checkout_args, paths.v[i]);
    // Also sync .modgit
    strvec_push(&checkout_args, ".modgit");

    if (run_git_cmd_v(&checkout_args)) {
        warning(_("failed to checkout files from '%s'"), source_branch);
        free_module_def(mod);
        strvec_clear(&paths);
        strvec_clear(&checkout_args);
        free(module_name);
        return 1;
    }

    // Check if there are actual changes
    struct child_process diff_cmd = CHILD_PROCESS_INIT;
    diff_cmd.git_cmd = 1;
    strvec_pushl(&diff_cmd.args, "diff", "--cached", "--quiet", NULL);
    int has_changes = run_command(&diff_cmd);

    if (has_changes) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Sync module '%s' from %s", module_name, source_branch);
        const char *commit_args[] = { "commit", "-m", msg, NULL };
        if (run_git_cmd(commit_args))
            warning(_("failed to commit sync changes"));
        else
            printf(_("Synced successfully. Changes from '%s' applied.\n"), source_branch);
    } else {
        printf(_("Already up to date. No changes to sync.\n"));
    }

    free_module_def(mod);
    strvec_clear(&paths);
    strvec_clear(&checkout_args);
    free(module_name);
    return 0;
}

/*
 * ── PUSH (push orphan branch changes back to source) ─────
 * Takes files from the current orphan branch and applies them
 * to the target branch (default: master).
 */

static int cmd_modgit_push(int argc, const char **argv, const char *prefix, struct repository *repo)
{
    const char *target_branch = "master";

    struct option options[] = {
        OPT_STRING(0, "target", &target_branch, N_("branch"),
                   N_("target branch to push to (default: master)")),
        OPT_END()
    };

    argc = parse_options(argc, argv, prefix, options, modgit_usage, 0);

    // Detect module from branch name
    char *module_name = detect_module_from_branch();
    if (!module_name)
        die(_("not on a module branch (expected branch 'module/<name>').\n"
              "Use 'git modgit orphan <module>' first."));

    char orphan_branch[256];
    snprintf(orphan_branch, sizeof(orphan_branch), "module/%s", module_name);

    struct module_def *mod = load_module_def(module_name);
    if (!mod)
        die(_("module '%s' not found in .modgit"), module_name);

    /* For push, only use module's OWN paths (not dependencies).
     * Dependencies belong to other modules and shouldn't be pushed back. */
    struct strvec paths = STRVEC_INIT;
    for (int i = 0; i < mod->paths.nr; i++)
        strvec_push(&paths, mod->paths.v[i]);

    printf(_("Pushing module '%s' changes to '%s'...\n"), module_name, target_branch);

    /* Clean untracked files that might block checkout to target branch */
    const char *clean_args[] = { "clean", "-fd", NULL };
    run_git_cmd(clean_args);

    // Switch to target branch
    const char *switch_args[] = { "checkout", target_branch, NULL };
    if (run_git_cmd(switch_args))
        die(_("failed to switch to '%s'"), target_branch);

    // Checkout module files from the orphan branch
    struct strvec checkout_args = STRVEC_INIT;
    strvec_push(&checkout_args, "checkout");
    strvec_push(&checkout_args, orphan_branch);
    strvec_push(&checkout_args, "--");
    for (int i = 0; i < paths.nr; i++)
        strvec_push(&checkout_args, paths.v[i]);

    if (run_git_cmd_v(&checkout_args)) {
        warning(_("failed to checkout files from '%s'"), orphan_branch);
        // Try to switch back
        const char *back_args[] = { "checkout", orphan_branch, NULL };
        run_git_cmd(back_args);
        free_module_def(mod);
        strvec_clear(&paths);
        strvec_clear(&checkout_args);
        free(module_name);
        return 1;
    }

    // Check if there are actual changes
    struct child_process diff_cmd = CHILD_PROCESS_INIT;
    diff_cmd.git_cmd = 1;
    strvec_pushl(&diff_cmd.args, "diff", "--cached", "--quiet", NULL);
    int has_changes = run_command(&diff_cmd);

    if (has_changes) {
        // Stage and commit
        struct strvec add_args = STRVEC_INIT;
        strvec_push(&add_args, "add");
        for (int i = 0; i < paths.nr; i++)
            strvec_push(&add_args, paths.v[i]);
        run_git_cmd_v(&add_args);
        strvec_clear(&add_args);

        char msg[256];
        snprintf(msg, sizeof(msg), "[%s] Update from isolated branch", module_name);
        const char *commit_args[] = { "commit", "-m", msg, NULL };
        if (run_git_cmd(commit_args))
            warning(_("failed to commit changes to '%s'"), target_branch);
        else
            printf(_("Changes from module '%s' applied to '%s'.\n"), module_name, target_branch);
    } else {
        printf(_("No changes to push. Module files are identical.\n"));
    }

    // Clean untracked files before switching back to orphan
    const char *clean_back_args[] = { "clean", "-fd", NULL };
    run_git_cmd(clean_back_args);

    // Switch back to orphan branch
    const char *back_args[] = { "checkout", orphan_branch, NULL };
    run_git_cmd(back_args);

    printf(_("Back on branch '%s'.\n"), orphan_branch);

    free_module_def(mod);
    strvec_clear(&paths);
    strvec_clear(&checkout_args);
    free(module_name);
    return 0;
}

/*
 * ── INIT (create/append module to .modgit) ───────────────
 * Usage: git modgit init <name> --path=<p> [--path=<p>...] [--depends=<d>...]
 * Supports nested modules: git modgit init frontend/css --path=src/assets/css
 */

static int cmd_modgit_init(int argc, const char **argv, const char *prefix, struct repository *repo)
{
    const char *module_name = NULL;
    struct strvec paths_arg = STRVEC_INIT;
    struct strvec depends_arg = STRVEC_INIT;

    /* parse_options doesn't support repeated string options well,
     * so we parse manually */
    for (int i = 0; i < argc; i++) {
        const char *arg = argv[i];
        const char *val;
        if (skip_prefix(arg, "--path=", &val)) {
            strvec_push(&paths_arg, val);
        } else if (skip_prefix(arg, "--depends=", &val)) {
            strvec_push(&depends_arg, val);
        } else if (arg[0] != '-' && !module_name) {
            module_name = arg;
        }
    }

    if (!module_name)
        die(_("usage: git modgit init <name> --path=<path> [--path=<path>...] [--depends=<dep>...]"));

    if (paths_arg.nr == 0)
        die(_("at least one --path is required"));

    /* Append to .modgit file */
    FILE *f = fopen(".modgit", "a");
    if (!f)
        die_errno(_("cannot open .modgit for writing"));

    fprintf(f, "\n[module \"%s\"]\n", module_name);
    for (int i = 0; i < paths_arg.nr; i++)
        fprintf(f, "\tpath = %s\n", paths_arg.v[i]);
    for (int i = 0; i < depends_arg.nr; i++)
        fprintf(f, "\tdepends = %s\n", depends_arg.v[i]);

    fclose(f);

    printf(_("Module '%s' added to .modgit\n"), module_name);
    printf(_("  Paths: "));
    for (int i = 0; i < paths_arg.nr; i++)
        printf("%s%s", paths_arg.v[i], (i < paths_arg.nr - 1) ? ", " : "");
    printf("\n");

    if (depends_arg.nr > 0) {
        printf(_("  Depends on: "));
        for (int i = 0; i < depends_arg.nr; i++)
            printf("%s%s", depends_arg.v[i], (i < depends_arg.nr - 1) ? ", " : "");
        printf("\n");
    }

    /* Check if this is a nested module */
    if (strchr(module_name, '/'))
        printf(_("  (nested submodule of '%.*s')\n"),
               (int)(strrchr(module_name, '/') - module_name), module_name);

    strvec_clear(&paths_arg);
    strvec_clear(&depends_arg);
    return 0;
}

/*
 * ── AI-CONTEXT ───────────────────────────────────────────
 */

static int cmd_modgit_ai_context(int argc, const char **argv, const char *prefix, struct repository *repo)
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
 * ── HELP ─────────────────────────────────────────────────
 */

static void show_modgit_help(void)
{
    printf("\n");
    printf("  ModuleGit - Modular Git for Monorepos\n");
    printf("  ======================================\n");
    printf("\n");
    printf("  usage: git modgit <command> [<args>]\n");
    printf("\n");
    printf("  Core Workflow:\n");
    printf("    switch [--full|--dev] <module>  Switch to a module\n");
    printf("    status                          Show module-aware status\n");
    printf("    commit [message]                Module-scoped commit\n");
    printf("    reset                           Restore full repo visibility\n");
    printf("    run <command>                   Run command in module context\n");
    printf("\n");
    printf("  Setup & Discovery:\n");
    printf("    list                            List all modules\n");
    printf("    clone --module=<name> <url>     Partial clone for a module\n");
    printf("    init <name> --path=<p>          Add module to .modgit\n");
    printf("    ai-context --module=<name>      Generate AI context dump\n");
    printf("\n");
    printf("  Orphan Branches:\n");
    printf("    orphan <module>                 Create isolated module branch\n");
    printf("    sync [--source=<branch>]        Sync from source branch\n");
    printf("    push [--target=<branch>]        Push changes to target branch\n");
    printf("\n");
    printf("    help                            Show this help message\n");
    printf("\n");
    printf("  See 'https://modulegit.vercel.app' for full documentation.\n");
    printf("\n");
}

static int cmd_modgit_help(int argc, const char **argv, const char *prefix, struct repository *repo)
{
    show_modgit_help();
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
		OPT_SUBCOMMAND("sync", &fn, cmd_modgit_sync),
		OPT_SUBCOMMAND("push", &fn, cmd_modgit_push),
		OPT_SUBCOMMAND("init", &fn, cmd_modgit_init),
		OPT_SUBCOMMAND("ai-context", &fn, cmd_modgit_ai_context),
		OPT_SUBCOMMAND("help", &fn, cmd_modgit_help),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options, modgit_usage,
			     PARSE_OPT_SUBCOMMAND_OPTIONAL);

    if (fn)
		return fn(argc, argv, prefix, repo);

    /* No subcommand given — show help */
    show_modgit_help();
    return 0;
}
