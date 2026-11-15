#include "pet_builtin.h"

#include <string.h>

const pet_builtin_t *pet_builtin_at(unsigned i)
{
    return i < PET_BUILTIN_COUNT ? &pet_builtins[i] : NULL;
}

const pet_builtin_t *pet_builtin_find(const char *id)
{
    for (unsigned i = 0; i < PET_BUILTIN_COUNT; i++) {
        if (strcmp(pet_builtins[i].id, id) == 0) {
            return &pet_builtins[i];
        }
    }
    return NULL;
}
