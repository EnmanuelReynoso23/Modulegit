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
    
    // Check if this config belongs to the module we are looking for
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
    }
    
    return 0;
}

struct module_def *load_module_def(const char *module_name)
{
    current_module = xcalloc(1, sizeof(struct module_def));
    current_module->name = xstrdup(module_name);
    strvec_init(&current_module->paths);
    strvec_init(&current_module->depends_on);
    
    target_module_name = module_name;
    
    // Read from .modgit file in root
    // We assume the file is in Git config format
    git_config_from_file(modgit_config_cb, ".modgit", NULL);
    
    if (current_module->paths.nr == 0) {
        // Module not found or empty
        // TODO: Handle error properly
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

void resolve_dependencies(struct module_def *module, struct strvec *all_paths)
{
    // Add current module paths
    for (int i = 0; i < module->paths.nr; i++) {
        strvec_push(all_paths, module->paths.v[i]);
    }

    // Recursively resolve dependencies
    for (int i = 0; i < module->depends_on.nr; i++) {
        struct module_def *dep = load_module_def(module->depends_on.v[i]);
        if (dep) {
            resolve_dependencies(dep, all_paths);
            free_module_def(dep);
        } else {
            warning("Dependency '%s' not found for module '%s'", 
                    module->depends_on.v[i], module->name);
        }
    }
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
    // We strictly want the name, so we check if we already have it
    // But since we can't easily check for duplicates in strvec without iterating,
    // we will rely on checking if the key is 'path' to only add it once per module definition
    // assuming valid config module.<name>.path exists.
    
    subkey = dot + 1;
    if (strcmp(subkey, "path"))
        return 0;
        
    char *name = xmemdupz(var, name_len);
    
    // Check duplicates
    int found = 0;
    for (int i = 0; i < names->nr; i++) {
        if (!strcmp(names->v[i], name)) {
            found = 1;
            break;
        }
    }
    
    if (!found)
        strvec_push(names, name);
        
    free(name);
    return 0;
}

void list_modules(struct strvec *names)
{
    git_config_from_file(list_modules_cb, ".modgit", names);
}
