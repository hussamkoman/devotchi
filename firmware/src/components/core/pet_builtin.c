#include "pet_builtin.h"

#include <string.h>

static const pet_builtin_t BUILTINS[PET_BUILTIN_COUNT] = {
    {"devo", pet_builtin_devo, &pet_builtin_devo_len},
    {"ember", pet_builtin_ember, &pet_builtin_ember_len},
};

const pet_builtin_t *pet_builtin_at(unsigned i)
{
    return i < PET_BUILTIN_COUNT ? &BUILTINS[i] : NULL;
}

const pet_builtin_t *pet_builtin_find(const char *id)
{
    for (unsigned i = 0; i < PET_BUILTIN_COUNT; i++) {
        if (strcmp(BUILTINS[i].id, id) == 0) {
            return &BUILTINS[i];
        }
    }
    return NULL;
}
