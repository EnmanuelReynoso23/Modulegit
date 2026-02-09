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
	N_("git modgit switch <module>"),
	N_("git modgit run <command>"),
	N_("git modgit commit [message]"),
	N_("git modgit ai-context --module=<name>"),
	NULL
};

// Helper to run internal git commands
static int run_git_cmd(const char **argv)
{
    struct child_process cmd = CHILD_PROCESS_INIT;
    cmd.git_cmd = 1;
    cmd.args.v = argv;
    return run_command(&cmd);
}

    return run_command(&cmd);
}

// Helper for user-friendly errors
static void die_with_hint(const char *err, const char *hint)
{
    fprintf(stderr, _("error: %s\n"), err);
    if (hint)
        fprintf(stderr, _("hint: %s\n"), hint);
    exit(128);
}

static int switch_to_module(const char *module_name)
{
    struct module_def *mod = load_module_def(module_name);
    if (!mod)
        die_with_hint(_("module not found"), 
                     _("Run 'git modgit list' to see available modules. Check .modgit file presence."));

    struct strvec paths = STRVEC_INIT;
    resolve_dependencies(mod, &paths);
    
    if (paths.nr == 0) {
        warning(_("module '%s' has no paths defined"), module_name);
    }

    // Configure Sparse Checkout
    struct strvec sparse_args = STRVEC_INIT;
    strvec_push(&sparse_args, "sparse-checkout");
    strvec_push(&sparse_args, "set");
    for (int i = 0; i < paths.nr; i++)
        strvec_push(&sparse_args, paths.v[i]);
    strvec_push(&sparse_args, NULL);
    
    if (run_git_cmd((const char **)sparse_args.v))
        die_with_hint(_("bfailed to configure sparse-checkout"), 
                     _("Ensure your git version supports sparse-checkout (v2.25+)"));

    printf(_("Switched to module '%s'\n"), module_name);

    free_module_def(mod);
    strvec_clear(&paths);
    strvec_clear(&sparse_args);
    return 0;
}

static int cmd_modgit_switch(int argc, const char **argv, const char *prefix)
{
    const char *module_name = NULL;
    
    struct option options[] = {
        OPT_STRING(0, "module", &module_name, N_("name"), N_("name of the module to switch to")),
        OPT_END()
    };

    argc = parse_options(argc, argv, prefix, options, modgit_usage, 0);
    
    // If no --module flag, maybe the first arg is the name
    if (!module_name && argc > 0)
        module_name = argv[0];

    if (!module_name)
        die(_("module name is required"));

    return switch_to_module(module_name);
}

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
    const char *repo_dir = (argc > 1) ? argv[1] : "repo"; // Simple default

	printf(_("Cloning module '%s' from '%s'...\n"), module_name, repo_url);

    // 1. Partial Clone (Blob:None) + Sparse
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

    // 2. Resolve Module Dependencies
    // We must switch context to the new repo dir
    if (chdir(repo_dir))
        die_errno("cannot chdir to newly cloned repo");

    // 3. Switch to module (Configure Sparse Checkout)
    return switch_to_module(module_name);
}

static int cmd_modgit_list(int argc, const char **argv, const char *prefix)
{
    struct strvec modules = STRVEC_INIT;
    
    list_modules(&modules);
    
    if (modules.nr == 0) {
        printf(_("No modules found.\n"));
        printf(_("hint: Create a .modgit file in the root to define modules.\n"));
    } else {
        printf(_("Available modules:\n"));
        for (int i = 0; i < modules.nr; i++) {
            printf("  %s\n", modules.v[i]);
        }
    }
    
    strvec_clear(&modules);
    return 0;
}

static int cmd_modgit_run(int argc, const char **argv, const char *prefix)
{
    // modgit run <command>
    if (argc < 1)
        die(_("usage: modgit run <command>"));

    printf(_("Running in module overlay: %s...\n"), argv[0]);
    
    struct child_process cmd = CHILD_PROCESS_INIT;
    cmd.use_shell = 1;
    cmd.args.v = argv;
    int ret = run_command(&cmd);
    if (ret)
        warning(_("command '%s' exited with error code %d"), argv[0], ret);
    return ret;
}

static int cmd_modgit_commit(int argc, const char **argv, const char *prefix)
{
    // modgit commit [message]
    const char *msg = (argc > 0) ? argv[0] : "Module update";

    printf(_("Committing overlay changes...\n"));
    
    // 1. Create a dynamic branchname: modgit/patch-<timestamp>
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char branch_name[100];
    strftime(branch_name, sizeof(branch_name), "modgit/patch-%Y%m%d-%H%M%S", t);
    
    printf(_("Creating branch '%s'...\n"), branch_name);
    
    const char *checkout_args[] = { "checkout", "-b", branch_name, NULL };
    if (run_git_cmd(checkout_args))
        warning(_("Could not create branch (maybe already on it?)\n"
                 "hint: Proceeding with commit on current branch."));
        
    // 2. Add only modified files that are tracked
    const char *add_args[] = { "add", "-u", NULL }; 
    if (run_git_cmd(add_args))
        die_with_hint(_("failed to add changes"), _("Check if files are locked or permissions are correct."));
    
    const char *commit_args[] = { "commit", "-m", msg, NULL };
    return run_git_cmd(commit_args);
}

static int cmd_modgit_status(int argc, const char **argv, const char *prefix)
{
    printf(_("ModuleGit Status:\n"));
    
    // 1. Check if we are in a sparse-checkout (module mode)
    // This is a rough check. Real implementation would check core.sparseCheckout
    
    // 2. Run git status
    const char *status_args[] = { "status", NULL };
    return run_git_cmd(status_args);
}

static int cmd_modgit_ai_context(int argc, const char **argv, const char *prefix)
{
    const char *module_name = NULL;
    
    struct option options[] = {
        OPT_STRING(0, "module", &module_name, N_("name"), N_("name of the module to generate context for")),
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
    
    // Output format: Just the file paths for now, maybe content later
    // Could support different formats like --format=xml/json
    printf("Subject: Context for module '%s'\n\n", module_name);
    printf("This context includes the following paths:\n");
    for (int i = 0; i < paths.nr; i++) {
        printf("- %s\n", paths.v[i]);
    }
    
    // TODO: Actually dump file contents or generate XML/Markdown for LLM
    
    free_module_def(mod);
    strvec_clear(&paths);
    return 0;
}

int cmd_modgit(int argc, const char **argv, const char *prefix, struct repository *repo)
{
	parse_opt_subcommand_fn *fn = NULL;
	struct option options[] = {
		OPT_SUBCOMMAND("clone", &fn, cmd_modgit_clone),
		OPT_SUBCOMMAND("list", &fn, cmd_modgit_list),
		OPT_SUBCOMMAND("status", &fn, cmd_modgit_status),
		OPT_SUBCOMMAND("switch", &fn, cmd_modgit_switch),
		OPT_SUBCOMMAND("run", &fn, cmd_modgit_run),
		OPT_SUBCOMMAND("commit", &fn, cmd_modgit_commit),
		OPT_SUBCOMMAND("ai-context", &fn, cmd_modgit_ai_context),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options, modgit_usage, 0);
	
    if (fn)
		return fn(argc, argv, prefix);
    
    usage_with_options(modgit_usage, options);
    return 1;
}
