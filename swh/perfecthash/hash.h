/*
 * Copyright (C) 2021-2022  The Software Heritage developers
 * See the AUTHORS file at the top-level directory of this distribution
 * License: GNU General Public License version 3, or any later version
 * See top-level LICENSE file for more information
 */

#include <cmph.h>
#include <cmph_types.h>
#include <stdint.h>

#define SHARD_OFFSET_MAGIC 32
#define SHARD_OFFSET_HEADER 512
#define SHARD_KEY_LEN 32
extern const int shard_key_len;

#define SHARD_MAGIC "SWHShard"
#define SHARD_VERSION 1

typedef struct {
    uint64_t version;
    uint64_t objects_count;
    uint64_t objects_position;
    uint64_t objects_size;
    uint64_t index_position;
    uint64_t index_size;
    uint64_t hash_position;
} shard_header_t;

typedef struct {
    char key[SHARD_KEY_LEN];
    uint64_t object_offset;
} shard_index_t;

typedef struct {
    char *path;
    FILE *f;
    shard_header_t header;
    cmph_t *hash;

    // The following fields are only used when creating the Read Shard
    cmph_io_adapter_t *source;
    cmph_config_t *config;
    shard_index_t *index;
    uint64_t index_offset;
} shard_t;

shard_t *shard_init(const char *path);
int shard_destroy(shard_t *shard);

int shard_prepare(shard_t *shard, uint64_t objects_count);
int shard_object_write(shard_t *shard, const char *key, const char *object,
                       uint64_t object_size);
int shard_finalize(shard_t *shard);

int shard_load(shard_t *shard);
int shard_find_object(shard_t *shard, const char *key, uint64_t *object_size);
int shard_read_object(shard_t *shard, char *object, uint64_t object_size);
