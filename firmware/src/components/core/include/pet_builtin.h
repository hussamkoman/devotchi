#pragma once

#include <stddef.h>
#include <stdint.h>

/** Packs compiled into the firmware (generated from pets/<id>.pet.json by tools/petpack.py c-array). */
extern const uint8_t pet_builtin_devo[];
extern const size_t pet_builtin_devo_len;
extern const uint8_t pet_builtin_ember[];
extern const size_t pet_builtin_ember_len;

typedef struct {
    const char *id;
    const uint8_t *bytes;
    const size_t *len;
} pet_builtin_t;

#define PET_BUILTIN_COUNT    2
/** The default pet and the fallback for a stored pet that does not load. */
#define PET_BUILTIN_DEFAULT  "devo"

/** Built-in i (0..PET_BUILTIN_COUNT-1, Devo first), NULL past the end. */
const pet_builtin_t *pet_builtin_at(unsigned i);

/** The built-in with this id, NULL when the id is not a built-in. */
const pet_builtin_t *pet_builtin_find(const char *id);
