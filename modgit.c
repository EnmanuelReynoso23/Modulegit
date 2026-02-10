#include "git-compat-util.h"
#include "modgit.h"
#include "config.h"
#include "strvec.h"

static struct module_def *current_module = NULL;
static const char *target_module_name = NULL;

static int modgit_config_cb(const char *var, const char *value, void *data)
{
    const char *subkey;

    if (!skip_prefix(var, "module.", &var))
        return 0;

    // format: module.<name>.<key>
    const char *dot = strrchr(var, '.');
    if (!dot) return 0;

    size_t name_len = dot - var;
    if (strncmp(var, target_module_name, name_len) || target_module_name[name_len] != '\0')
        return 0;

    subkey = dot + 1;

    if (!strcmp(subkey, "path")) {
        strvec_push(&current_module->paths, value);
    } else if (!strcmp(subkey, "depends")) {
        strvec_push(&current_module->depends_on, value);
    } else if (!strcmp(subkey, "readonly")) {
        current_module->read_only = git_config_bool(var - name_len - 7, value);
    } else if (!strcmp(subkey, "ownersonly")) {
        current_module->owners_only = git_config_bool(var - name_len - 7, value);
    }

    return 0;
}

struct module_def *load_module_def(const char *module_name)
{
    current_module = xcalloc(1, sizeof(struct module_def));
    current_module->name = xstrdup(module_name);
    strvec_init(&current_module->paths);
    strvec_init(&current_module->depends_on);
    current_module->read_only = 0;
    current_module->owners_only = 0;

    target_module_name = module_name;

    git_config_from_file(modgit_config_cb, ".modgit", NULL);

    if (current_module->paths.nr == 0) {
        free_module_def(current_module);
        return NULL;
    }

    return current_module;
}

void free_module_def(struct module_def *module)
{
    if (!module) return;
    free(module->name);
    strvec_clear(&module->paths);
    strvec_clear(&module->depends_on);
    free(module);
}

// Helper: check if a string is already in a strvec
static int strvec_contains(const struct strvec *vec, const char *str)
{
    for (int i = 0; i < vec->nr; i++) {
        if (!strcmp(vec->v[i], str))
            return 1;
    }
    return 0;
}

// Internal recursive resolver with visited set to detect circular deps
static void resolve_dependencies_recursive(const char *module_name,
                                           struct strvec *all_paths,
                                           struct strvec *visited,
                                           int depth)
{
    // Guard against absurd depth (safety net)
    if (depth > 50) {
        warning("dependency depth limit exceeded at module '%s'", module_name);
        return;
    }

    // Circular dependency detection
    if (strvec_contains(visited, module_name)) {
        warning("circular dependency detected: '%s' already visited (skipping)", module_name);
        return;
    }

    strvec_push(visited, module_name);

    struct module_def *mod = load_module_def(module_name);
    if (!mod) {
        warning("dependency '%s' not found", module_name);
        return;
    }

    // Add paths (deduplicated)
    for (int i = 0; i < mod->paths.nr; i++) {
        if (!strvec_contains(all_paths, mod->paths.v[i]))
            strvec_push(all_paths, mod->paths.v[i]);
    }

    // Recurse into dependencies
    for (int i = 0; i < mod->depends_on.nr; i++) {
        resolve_dependencies_recursive(mod->depends_on.v[i], all_paths, visited, depth + 1);
    }

    free_module_def(mod);
}

void resolve_dependencies(struct module_def *module, struct strvec *all_paths)
{
    struct strvec visited = STRVEC_INIT;

    // Add current module's own paths first
    strvec_push(&visited, module->name);
    for (int i = 0; i < module->paths.nr; i++) {
        if (!strvec_contains(all_paths, module->paths.v[i]))
            strvec_push(all_paths, module->paths.v[i]);
    }

    // Resolve each dependency
    for (int i = 0; i < module->depends_on.nr; i++) {
        resolve_dependencies_recursive(module->depends_on.v[i], all_paths, &visited, 1);
    }

    strvec_clear(&visited);
}

static int list_modules_cb(const char *var, const char *value, void *data)
{
    struct strvec *names = data;
    const char *subkey;

    if (!skip_prefix(var, "module.", &var))
        return 0;

    const char *dot = strrchr(var, '.');
    if (!dot) return 0;

    size_t name_len = dot - var;

    subkey = dot + 1;
    if (strcmp(subkey, "path"))
        return 0;

    char *name = xmemdupz(var, name_len);

    if (!strvec_contains(names, name))
        strvec_push(names, name);

    free(name);
    return 0;
}

void list_modules(struct strvec *names)
{
    git_config_from_file(list_modules_cb, ".modgit", names);
}
