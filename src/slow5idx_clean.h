#ifndef SLOW5IDX_CLEAN_H
#define SLOW5IDX_CLEAN_H

#include <stdio.h>
#include <stdint.h>
#include "klib/khash.h"
#include "slow5.h"

#define INDEX_EXTENSION "." "idx"

// SLOW5 record index
struct slow5_rec_idx {
    uint64_t offset;
    uint64_t size;
};

// Read id map: read id -> index data
KHASH_MAP_INIT_STR(s2i, struct slow5_rec_idx)

// SLOW5 index
struct slow5_idx {
    FILE *fp;
    char *pathname;
    char **ids;
    uint64_t num_ids;
    uint64_t cap_ids;
    khash_t(s2i) *hash;
};

struct slow5_idx *slow5_idx_init(struct slow5_file *s5p);
/**
 * Create the index file for slow5 file.
 * Overrides if already exists.
 *
 * @param   s5p         slow5 file structure
 * @param   pathname    pathname to write index to
 */
int slow5_idx_to(struct slow5_file *s5p, const char *pathname);
void slow5_idx_free(struct slow5_idx *index);
int slow5_idx_get(struct slow5_idx *index, const char *read_id, struct slow5_rec_idx *read_index);
void slow5_idx_insert(struct slow5_idx *index, char *read_id, uint64_t offset, uint64_t size);
void slow5_idx_write(struct slow5_idx *index);
void slow5_rec_idx_print(struct slow5_rec_idx read_index);

#endif
