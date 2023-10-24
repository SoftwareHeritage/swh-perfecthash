/*
 * Copyright (C) 2021-2022  The Software Heritage developers
 * See the AUTHORS file at the top-level directory of this distribution
 * License: GNU General Public License version 3, or any later version
 * See top-level LICENSE file for more information
 */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <memory.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "swh/perfecthash/hash.h"

const int shard_key_len = SHARD_KEY_LEN;

#ifdef HASH_DEBUG
#define debug(...) printf(__VA_ARGS__)
#else
#define debug(...)
#endif

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
uint64_t ntohq(uint64_t v) { return __builtin_bswap64(v); }
uint64_t htonq(uint64_t v) { return __builtin_bswap64(v); }
#else  /* __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__ */
uint64_t ntohq(uint64_t v) { return v; }
uint64_t htonq(uint64_t v) { return v; }
#endif /* __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__ */

/***********************************************************
 * wrappers around FILE functions that:
 * - return -1 on error
 * - print a meaningful message when an error occurs
 *
 */

int shard_open(shard_t *shard, const char *mode) {
    shard->f = fopen(shard->path, mode);
    if (shard->f == NULL) {
        printf("shard_open: open(%s, %s): %s\n", shard->path, mode,
               strerror(errno));
        return -1;
    } else
        return 0;
}

int shard_close(shard_t *shard) {
    if (shard->f == NULL)
        return 0;
    int r = fclose(shard->f);
    if (r < 0)
        printf("shard_close: fclose(%p): %s\n", shard->f, strerror(errno));
    return r;
}

int shard_seek(shard_t *shard, uint64_t offset, int whence) {
    if (offset > INT64_MAX) {
        printf("shard_seek: %ld > %ld (INT64_MAX)", offset, INT64_MAX);
        return -1;
    }
    int r = fseeko(shard->f, offset, whence);
    if (r < 0)
        printf("shard_seek: fseeko(%p, %ld, %d): %s\n", shard->f, offset,
               whence, strerror(errno));
    return r;
}

uint64_t shard_tell(shard_t *shard) {
    off_t r = ftello(shard->f);
    if (r < 0)
        printf("shard_tell: ftello(%p): %s\n", shard->f, strerror(errno));
    return r;
}

int shard_read(shard_t *shard, void *ptr, uint64_t size) {
    uint64_t read;
    if ((read = fread(ptr, 1, size, shard->f)) != size) {
        printf("shard_read: read %ld instead of %ld\n", read, size);
        return -1;
    }
    return 0;
}

int shard_read_uint64_t(shard_t *shard, uint64_t *ptr) {
    uint64_t n_size;
    if (shard_read(shard, &n_size, sizeof(uint64_t)) < 0) {
        printf("shard_read_uint64_t: shard_read\n");
        return -1;
    }
    *ptr = ntohq(n_size);
    return 0;
}

int shard_write(shard_t *shard, const void *ptr, uint64_t nmemb) {
    uint64_t wrote;
    if ((wrote = fwrite(ptr, 1, nmemb, shard->f)) != nmemb) {
        printf("shard_write: wrote %ld instead of %ld\n", wrote, nmemb);
        return -1;
    }
    return 0;
}

/***********************************************************
 * load or save a SHARD_MAGIC
 */

int shard_magic_load(shard_t *shard) {
    if (shard_seek(shard, 0, SEEK_SET) < 0) {
        printf("shard_magic_load: seek\n");
        return -1;
    }
    char magic[sizeof(SHARD_MAGIC)];
    if (shard_read(shard, (void *)magic, sizeof(SHARD_MAGIC)) < 0) {
        printf("shard_magic_load: read\n");
        return -1;
    }
    if (memcmp(magic, SHARD_MAGIC, sizeof(SHARD_MAGIC)) != 0) {
        printf("shard_magic_load: memcmp(%.*s, %s)\n", (int)sizeof(SHARD_MAGIC),
               magic, SHARD_MAGIC);
        return -1;
    }
    return 0;
}

int shard_magic_save(shard_t *shard) {
    if (shard_seek(shard, 0, SEEK_SET) < 0) {
        printf("shard_magic_save: seek\n");
        return -1;
    }
    if (shard_write(shard, (void *)SHARD_MAGIC, sizeof(SHARD_MAGIC)) < 0) {
        printf("shard_magic_save: write\n");
        return -1;
    }
    return 0;
}

/***********************************************************
 * load or save a shard_header_t
 */

int shard_header_print(shard_header_t *header) {
#define PRINT(name) debug("shard_header_print: " #name " %ld\n", header->name)
    PRINT(version);
    PRINT(objects_count);
    PRINT(objects_position);
    PRINT(objects_size);
    PRINT(index_position);
    PRINT(index_size);
    PRINT(hash_position);
#undef PRINT
    return 0;
}

int shard_header_load(shard_t *shard) {
    if (shard_seek(shard, SHARD_OFFSET_MAGIC, SEEK_SET) < 0) {
        printf("shard_header_load\n");
        return -1;
    }
    shard_header_t header;
#define LOAD(name)                                                             \
    if (shard_read(shard, (void *)&header.name, sizeof(uint64_t)) < 0) {       \
        printf("shard_header_load: " #name "\n");                              \
        return -1;                                                             \
    }                                                                          \
    shard->header.name = ntohq(header.name)
    LOAD(version);
    LOAD(objects_count);
    LOAD(objects_position);
    LOAD(objects_size);
    LOAD(index_position);
    LOAD(index_size);
    LOAD(hash_position);
#undef LOAD
    shard_header_print(&shard->header);
    if (shard->header.version != SHARD_VERSION) {
        printf("shard_header_load: unexpected version, got %ld instead of %d\n",
               shard->header.version, SHARD_VERSION);
        return -1;
    }
    return 0;
}

int shard_header_save(shard_t *shard) {
    if (shard_seek(shard, SHARD_OFFSET_MAGIC, SEEK_SET) < 0) {
        printf("shard_header_save\n");
        return -1;
    }
    shard_header_print(&shard->header);
    shard_header_t header;
#define SAVE(name)                                                             \
    header.name = htonq(shard->header.name);                                   \
    if (shard_write(shard, (void *)&header.name, sizeof(uint64_t)) < 0) {      \
        printf("shard_header_save " #name "\n");                               \
        return -1;                                                             \
    }
    SAVE(version);
    SAVE(objects_count);
    SAVE(objects_position);
    SAVE(objects_size);
    SAVE(index_position);
    SAVE(index_size);
    SAVE(hash_position);
#undef SAVE
    return 0;
}

int shard_header_reset(shard_header_t *header) {
    memset((void *)header, '\0', sizeof(shard_header_t));
    header->version = SHARD_VERSION;
    header->objects_position = SHARD_OFFSET_HEADER;
    return 0;
}

/***********************************************************
 * Create the Read Shard
 */

int shard_object_write(shard_t *shard, const char *key, const char *object,
                       uint64_t object_size) {
    // save key & index to later build the hash
    shard_index_t *index = &shard->index[shard->index_offset];
    memcpy((void *)index->key, key, SHARD_KEY_LEN);
    index->object_offset = shard_tell(shard);
    shard->index_offset++;
    // write the object size and the object itself
    uint64_t n_object_size = htonq(object_size);
    if (shard_write(shard, (void *)&n_object_size, sizeof(uint64_t)) < 0) {
        printf("shard_object_write: object_size\n");
        return -1;
    }
    if (shard_write(shard, (void *)object, object_size) < 0) {
        printf("shard_object_write: object\n");
        return -1;
    }
    return 0;
}

static int io_read(void *data, char **key, cmph_uint32 *keylen) {
    shard_t *shard = (shard_t *)data;
    *key = shard->index[shard->index_offset].key;
    *keylen = SHARD_KEY_LEN;
    shard->index_offset++;
    return shard->index_offset >= shard->header.objects_count ? -1 : 0;
}

static void io_dispose(void *data, char *key, cmph_uint32 keylen) {}

static void io_rewind(void *data) {
    shard_t *shard = (shard_t *)data;
    shard->index_offset = 0;
}

static cmph_io_adapter_t *io_adapter(shard_t *shard) {
    cmph_io_adapter_t *key_source =
        (cmph_io_adapter_t *)malloc(sizeof(cmph_io_adapter_t));
    if (key_source == NULL)
        return NULL;
    key_source->data = (void *)shard;
    key_source->nkeys = shard->header.objects_count;
    key_source->read = io_read;
    key_source->dispose = io_dispose;
    key_source->rewind = io_rewind;
    return key_source;
}

int shard_hash_create(shard_t *shard) {
    shard->source = io_adapter(shard);
    shard->config = cmph_config_new(shard->source);
    cmph_config_set_algo(shard->config, CMPH_CHD_PH);
    /* Set the load factor for the CHD algorithm to its maximum to waste the
     * minimal amount of entries in the index. The resulting function should
     * use 2.07 bits per objects.  */
    cmph_config_set_graphsize(shard->config, 0.99);
    cmph_config_set_keys_per_bin(shard->config, 1);
    cmph_config_set_b(shard->config, 4);
    shard->hash = cmph_new(shard->config);
    if (shard->hash == NULL) {
        return -1;
    }
    return 0;
}

int shard_index_save(shard_t *shard) {
    shard->header.index_position =
        shard->header.objects_position + shard->header.objects_size;
    debug("shard_index_save: index_position %ld\n",
          shard->header.index_position);
    assert(shard->header.index_position == shard_tell(shard));
    cmph_uint32 count = cmph_size(shard->hash);
    debug("shard_index_save: count = %d\n", count);
    shard->header.index_size = count * sizeof(shard_index_t);
    shard_index_t *index = (shard_index_t *)calloc(1, shard->header.index_size);
    for (uint64_t i = 0; i < shard->index_offset; i++) {
        cmph_uint32 h =
            cmph_search(shard->hash, shard->index[i].key, SHARD_KEY_LEN);
        debug("shard_index_save: i = %ld, h = %d, offset = %ld\n", i, h,
              shard->index[i].object_offset);
        assert(h < count);
        memcpy(index[h].key, shard->index[i].key, SHARD_KEY_LEN);
        index[h].object_offset = htonq(shard->index[i].object_offset);
    }
    uint64_t index_size = shard->header.index_size;
    if (shard_write(shard, (void *)index, index_size) < 0) {
        printf("shard_index_save\n");
        return -1;
    }
    free(index);
    return 0;
}

int shard_hash_save(shard_t *shard) {
    shard->header.hash_position =
        shard->header.index_position + shard->header.index_size;
    debug("shard_hash_save: hash_position %ld\n", shard->header.hash_position);
    cmph_dump(shard->hash, shard->f);
    return 0;
}

int shard_finalize(shard_t *shard) {
    int ret = 0;

    shard->header.objects_size =
        shard_tell(shard) - shard->header.objects_position;

    if ((ret = shard_hash_create(shard)) < 0) {
        printf("shard_hash_create\n");
        return ret;
    }
    if ((ret = shard_index_save(shard)) < 0) {
        printf("shard_index_save\n");
        return ret;
    }
    if ((ret = shard_hash_save(shard)) < 0) {
        printf("shard_hash_save\n");
        return ret;
    }
    if ((ret = shard_header_save(shard)) < 0) {
        printf("shard_header_save\n");
        return ret;
    }
    if ((ret = shard_magic_save(shard)) < 0) {
        printf("shard_magic_save\n");
        return ret;
    }

    if ((ret = fdatasync(fileno(shard->f))) < 0) {
        if (errno == EINVAL || errno == EROFS) {
            /* File(system) does not support fdatasync. Good luck! */
            ret = 0;
        } else {
            printf("fdatasync: %s\n", strerror(errno));
            return ret;
        }
    }
    return ret;
}

int shard_reset(shard_t *shard) {
    if (shard_header_reset(&shard->header) < 0)
        return -1;
    return shard_seek(shard, SHARD_OFFSET_HEADER, SEEK_SET);
}

int shard_prepare(shard_t *shard, uint64_t objects_count) {
    if (shard_open(shard, "w+") < 0)
        return -1;
    if (shard_reset(shard) < 0)
        return -1;
    shard->header.objects_count = objects_count;
    shard->index =
        (shard_index_t *)malloc(sizeof(shard_index_t) * objects_count);
    return 0;
}

/**********************************************************
 * Lookup objects from a Read Shard
 */

int shard_find_object(shard_t *shard, const char *key, uint64_t *object_size) {
    debug("shard_find_object\n");
    cmph_uint32 h = cmph_search(shard->hash, key, SHARD_KEY_LEN);
    debug("shard_find_object: h = %d\n", h);
    uint64_t index_offset =
        shard->header.index_position + h * sizeof(shard_index_t);
    debug("shard_find_object: index_offset = %ld\n", index_offset);
    if (shard_seek(shard, index_offset, SEEK_SET) < 0) {
        printf("shard_find_object: index_offset\n");
        return -1;
    }
    char object_id[SHARD_KEY_LEN];
    if (shard_read(shard, object_id, SHARD_KEY_LEN) < 0) {
        printf("shard_find_object: object_id\n");
        return -1;
    }
    if (memcmp(key, object_id, SHARD_KEY_LEN) != 0) {
        printf("shard_find_object: key mismatch");
        return -1;
    }
    uint64_t object_offset;
    if (shard_read_uint64_t(shard, &object_offset) < 0) {
        printf("shard_find_object: object_offset\n");
        return -1;
    }
    debug("shard_find_object: object_offset = %ld\n", object_offset);
    if (shard_seek(shard, object_offset, SEEK_SET) < 0) {
        printf("shard_find_object: object_offset\n");
        return -1;
    }
    if (shard_read_uint64_t(shard, object_size) < 0) {
        printf("shard_find_object: object_size\n");
        return -1;
    }
    debug("shard_find_object: object_size = %ld\n", *object_size);
    return 0;
}

int shard_read_object(shard_t *shard, char *object, uint64_t object_size) {
    if (shard_read(shard, (void *)object, object_size) < 0) {
        printf("shard_read_object: object\n");
        return -1;
    }
    return 0;
}

int shard_hash_load(shard_t *shard) {
    if (shard_seek(shard, shard->header.hash_position, SEEK_SET) < 0) {
        printf("shard_hash_load\n");
        return -1;
    }
    debug("shard_hash_load: hash_position %ld\n", shard->header.hash_position);
    shard->hash = cmph_load(shard->f);
    if (shard->hash == NULL) {
        printf("shard_hash_load: cmph_load\n");
        return -1;
    }
    return 0;
}

int shard_load(shard_t *shard) {
    debug("shard_load\n");
    if (shard_open(shard, "r") < 0)
        return -1;
    if (shard_magic_load(shard) < 0)
        return -1;
    if (shard_header_load(shard) < 0)
        return -1;
    return shard_hash_load(shard);
}

/**********************************************************
 * Initialize and destroy a Read Shard
 */

shard_t *shard_init(const char *path) {
    debug("shard_init\n");
    shard_t *shard = (shard_t *)malloc(sizeof(shard_t));
    if (shard == NULL)
        return NULL;
    memset((void *)shard, '\0', sizeof(shard_t));
    shard->path = strdup(path);
    return shard;
}

int shard_destroy(shard_t *shard) {
    if (shard->source)
        free(shard->source);
    if (shard->config)
        cmph_config_destroy(shard->config);
    if (shard->hash)
        cmph_destroy(shard->hash);
    if (shard->index)
        free(shard->index);
    free(shard->path);
    int r = shard_close(shard);
    free(shard);
    return r;
}
