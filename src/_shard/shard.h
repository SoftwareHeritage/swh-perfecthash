/*
 * Copyright (C) 2021-2025  The Software Heritage developers
 * See the AUTHORS file at the top-level directory of this distribution
 * License: GNU General Public License version 3, or any later version
 * See top-level LICENSE file for more information
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <cmph.h>
#include <cmph_types.h>
#include <stdint.h>

#define SHARD_OFFSET_MAGIC 32
#define SHARD_OFFSET_HEADER 512
#define SHARD_KEY_LEN 32
#define SHARD_MAX_OBJECTS (SIZE_MAX / (SHARD_KEY_LEN + sizeof(shard_index_t)))
extern const int shard_key_len;

#define SHARD_MAGIC "SWHShard"
#define SHARD_VERSION 1

/* Shard File Format

   +------------------------+------+----------------------------+
   | SHARD_MAGIC            | 0    | SHARD_OFFSET_MAGIC (32)    |
   +------------------------+------+----------------------------+
   | *header*               |      | (56)                       |
   |   version              | 32   | uint64_t (8)               |
   |   objects_count        | 40   | uint64_t (8)               |
   |   objects_position (op)| 48   | uint64_t (8)               |
   |   objects_size         | 56   | uint64_t (8)               |
   |   index_position (ip)  | 64   | uint64_t (8)               |
   |   index_size           | 72   | uint64_t (8)               |
   |   hash_position (hp)   | 80   | uint64_t (8)               |
   +------------------------+------+----------------------------+
   | *Objects*              | <op> |                            |
   |   objectx size         |      | uint64_t (8)               |
   |   objectx data         |      | <object0 size>             |
   |   objecty size         |      | uint64_t (8)               |
   |   objecty data         |      | <object1 size>             |
   |   ...                  |      |                            |
   +------------------------+------+----------------------------+
   | *Index*                | <ip> |                            |
   |   object0 key          |      | SHARD_KEY_LEN (32)         |
   |   object0 offset       |      | uint64_t (8)               |
   |   ...                  |      |                            |
   +------------------------+------+----------------------------+
   | *Hash map*             | <hp> |                            |
   |   hash function        |      | <as written by cmph_dump>  |
   +------------------------+------+----------------------------+

   Note: objects are not listed in the same order in the *Objects* section and
   the *Index* one. Objects are stored in insertion order, while the index is
   in the order that is most efficient for the cmph function.
 */

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
int shard_close(shard_t *shard);

int shard_prepare(shard_t *shard, uint64_t objects_count);
int shard_object_write(shard_t *shard, const char *key, const char *object,
                       uint64_t object_size);
int shard_finalize(shard_t *shard);

int shard_load(shard_t *shard);
int shard_find_object(shard_t *shard, const char *key, uint64_t *object_size);
int shard_read_object(shard_t *shard, char *object, uint64_t object_size);

int shard_index_get(shard_t *shard, const uint64_t pos, shard_index_t *idx);
int shard_cmph_search(shard_t *shard, const char *key, uint64_t *pos);

int shard_delete(shard_t *shard, const char *key);

int shard_read(shard_t *shard, void *ptr, uint64_t size);
int shard_seek(shard_t *shard, uint64_t offset, int whence);

#ifdef __cplusplus
}
#endif
