#ifndef MODGIT_H
#define MODGIT_H

#include "strvec.h"
#include "hashmap.h"

struct module_def {
    char *name;
    struct strvec paths;
    struct strvec depends_on;
    // Permissions
    int read_only;
    int owners_only;
};

/* 
 * Loads module definition from .modgit file in the worktree
 */
struct module_def *load_module_def(const char *module_name);

/*
 * List all available modules
 */
void list_modules(struct strvec *names);

void free_module_def(struct module_def *module);
void resolve_dependencies(struct module_def *module, struct strvec *all_paths);

#endif
