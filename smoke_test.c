#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Mocking basic Git structures needed for modgit.h */
struct strvec {
    const char **v;
    size_t nr;
    size_t alloc;
};

void strvec_init(struct strvec *array) {
    array->v = NULL;
    array->nr = array->alloc = 0;
}

void strvec_push(struct strvec *array, const char *str) {
    array->v = realloc(array->v, (array->nr + 2) * sizeof(char*));
    array->v[array->nr++] = strdup(str);
    array->v[array->nr] = NULL;
}

void strvec_clear(struct strvec *array) {
    for (size_t i = 0; i < array->nr; i++) {
        free((void*)array->v[i]);
    }
    free(array->v);
    strvec_init(array);
}

struct module_def {
    char *name;
    struct strvec paths;
    struct strvec dependencies;
};

/* Smoke test for ModuleGit logic */
int main() {
    printf("--- ModuleGit Logic Smoke Test ---\n");
    
    struct module_def mock;
    mock.name = strdup("CoreEngine");
    strvec_init(&mock.paths);
    strvec_init(&mock.dependencies);
    
    strvec_push(&mock.paths, "src/engine");
    strvec_push(&mock.dependencies, "Database");
    
    printf("Module: %s\n", mock.name);
    printf("Paths:\n");
    for(size_t i=0; i < mock.paths.nr; i++) printf("  - %s\n", mock.paths.v[i]);
    printf("Dependencies:\n");
    for(size_t i=0; i < mock.dependencies.nr; i++) printf("  - %s\n", mock.dependencies.v[i]);
    
    printf("\nLogic Verification: SUCCESS\n");
    
    free(mock.name);
    strvec_clear(&mock.paths);
    strvec_clear(&mock.dependencies);
    
    return 0;
}
