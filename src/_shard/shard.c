/*
 * Copyright (C) 2021-2025  The Software Heritage developers
 * See the AUTHORS file at the top-level directory of this distribution
 * License: GNU General Public License version 3, or any later version
 * See top-level LICENSE file for more information
 */
#ifdef __cplusplus
extern "C" {
#endif

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <memory.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "shard.h"

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
    shard->f = NULL;
    return r;
}

int shard_seek(shard_t *shard, uint64_t offset, int whence) {
    if (offset > INT64_MAX) {
        printf("shard_seek: %" PRIu64 " > %" PRId64 " (INT64_MAX)", offset,
               INT64_MAX);
        return -1;
    }
    int r = fseeko(shard->f, offset, whence);
    if (r < 0)
        printf("shard_seek: fseeko(%p, %" PRIu64 ", %d): %s\n", shard->f,
               offset, whence, strerror(errno));
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
        printf("shard_read: read %" PRIu64 " instead of %" PRIu64 "\n", read,
               size);
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
        printf("shard_write: wrote %" PRIu64 " instead of %" PRIu64 "\n", wrote,
               nmemb);
        return -1;
    }
    return 0;
}

int shard_write_zeros(shard_t *shard, uint64_t size) {
#define BUF_SIZE 4096
    char buf[BUF_SIZE];

    memset(buf, 0, BUF_SIZE);
    while (size > 0) {
        size_t bytes_written;
        if ((bytes_written = fwrite(buf, 1, MIN(size, BUF_SIZE), shard->f)) ==
            0) {
            return -1;
        }
        size -= bytes_written;
    }
    return 0;
#undef BUF_SIZE
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
#define PRINT(name)                                                            \
    debug("shard_header_print: " #name " %" PRIu64 "\n", header->name)
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
        printf("shard_header_load: unexpected version, got %" PRIu64
               " instead of %d\n",
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
    debug("shard_object_write: index_offset=%" PRIu64 "\n",
          shard->index_offset);
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
    debug("shard_index_save: index_position %" PRIu64 "\n",
          shard->header.index_position);
    assert(shard->header.index_position == shard_tell(shard));
    cmph_uint32 count = cmph_size(shard->hash);
    // Note that the 'count' computed by cmph is generally bigger than the
    // number of objects (in other word, it can be a NOT *minimal* perfect hash
    // map)", so we have to initialize the table of index entries with explicit
    // "invalid" entries (aka {key=0x00, offset=MAX_INT})
    debug("shard_index_save: count = %d\n", count);
    shard_index_t *index =
        (shard_index_t *)calloc(count, sizeof(shard_index_t));
    if (index == NULL) {
        printf("shard_index_save: could not allocate memory for the index");
        return -1;
    }
    shard->header.index_size = count * sizeof(shard_index_t);
    // initialize all the index entries as "deleted" entries by default, the
    // actual entries will be filled just below.
    for (uint64_t i = 0; i < count; i++) {
        index[i].object_offset = UINT64_MAX;
    }
    for (uint64_t i = 0; i < shard->index_offset; i++) {
        cmph_uint32 h =
            cmph_search(shard->hash, shard->index[i].key, SHARD_KEY_LEN);
        debug("shard_index_save: i = %" PRIu64 ", h = %d, offset = %" PRIu64
              "\n",
              i, h, shard->index[i].object_offset);
        assert(h < count);
        memcpy(index[h].key, shard->index[i].key, SHARD_KEY_LEN);
        index[h].object_offset = htonq(shard->index[i].object_offset);
    }
    uint64_t index_size = shard->header.index_size;
    debug("shard_index_save: save %" PRIu64 " index bytes at position %" PRIu64
          "\n",
          index_size, shard->header.index_position);
    if (shard_write(shard, (void *)index, index_size) < 0) {
        printf("shard_index_save\n");
        return -1;
    }
    free(index);
    return 0;
}

int shard_index_get(shard_t *shard, uint64_t pos, shard_index_t *idx) {
    // the number of entries in the cmph map (and thus in the index) is
    // generally larger than the number of saved objects, but we do not keep
    // the former number in the header, so recompute from the index size)
    if (pos > shard->header.index_size / sizeof(shard_index_t)) {
        printf("shard_index_get: position out of range\n");
        return -1;
    }
    uint64_t index_offset =
        shard->header.index_position + pos * sizeof(shard_index_t);
    if (shard_seek(shard, index_offset, SEEK_SET) < 0) {
        printf("shard_index_get: index not found\n");
    }
    errno = 0;
    if (shard_read(shard, idx, sizeof(shard_index_t)) < 0) {
        printf("shard_index_get: index not found\n");
        return -1;
    }
    idx->object_offset = ntohq(idx->object_offset);

    return 0;
}

int shard_cmph_search(shard_t *shard, const char *key, uint64_t *pos) {
    debug("shard_cmph_search\n");
    cmph_uint32 h = cmph_search(shard->hash, key, SHARD_KEY_LEN);
    *pos = h;
    return 0;
}

int shard_hash_save(shard_t *shard) {
    shard->header.hash_position =
        shard->header.index_position + shard->header.index_size;
    debug("shard_hash_save: hash_position %" PRIu64 "\n",
          shard->header.hash_position);
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

#ifdef __APPLE__
    /* fdatasync is not advertised in headers on macOS, use fcntl instead */
    ret = fcntl(fileno(shard->f), F_FULLFSYNC);
#else
    ret = fdatasync(fileno(shard->f));
#endif

    if (ret < 0) {
        if (errno == EINVAL || errno == EROFS) {
            /* File(system) does not support fdatasync or fcntl. Good luck! */
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
    // this is used only when creating a new shard
    debug("shard_prepare: objects=%" PRIu64 "\n", objects_count);
    if (objects_count > SHARD_MAX_OBJECTS) {
        printf("shard_prepare: objects_count too big: %" PRIu64
               " exceeds max value %" PRIu64,
               objects_count, SHARD_MAX_OBJECTS);
        return -1;
    }
    if (shard_open(shard, "w+") < 0)
        return -1;
    if (shard_reset(shard) < 0)
        return -1;
    shard->header.objects_count = objects_count;
    shard->index =
        (shard_index_t *)malloc(sizeof(shard_index_t) * objects_count);
    if (shard->index == NULL) {
        printf("shard_prepare: cannot allocate memory for the index");
        return -1;
    }
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
    debug("shard_find_object: index_offset = %" PRIu64 "\n", index_offset);
    if (shard_seek(shard, index_offset, SEEK_SET) < 0) {
        printf("shard_find_object: index_offset\n");
        return -1;
    }
    char object_id[SHARD_KEY_LEN];
    if (shard_read(shard, object_id, SHARD_KEY_LEN) < 0) {
        printf("shard_find_object: object_id\n");
        return -1;
    }
    uint64_t object_offset;
    if (shard_read_uint64_t(shard, &object_offset) < 0) {
        printf("shard_find_object: object_offset\n");
        return -1;
    }
    debug("shard_find_object: object_offset = %" PRIu64 "\n", object_offset);
    /* Has the object been deleted? */
    if (object_offset == UINT64_MAX) {
        return 1;
    }
    /* We compare the key after the offset so we have a way to
     * detect removed objects. */
    if (memcmp(key, object_id, SHARD_KEY_LEN) != 0) {
        printf("shard_find_object: key mismatch\n");
        return -1;
    }
    if (shard_seek(shard, object_offset, SEEK_SET) < 0) {
        printf("shard_find_object: object_offset\n");
        return -1;
    }
    if (shard_read_uint64_t(shard, object_size) < 0) {
        printf("shard_find_object: object_size\n");
        return -1;
    }
    debug("shard_find_object: object_size = %" PRIu64 "\n", *object_size);
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
    debug("shard_hash_load: hash_position %" PRIu64 "\n",
          shard->header.hash_position);
    shard->hash = cmph_load(shard->f);
    if (shard->hash == NULL) {
        printf("shard_hash_load: cmph_load\n");
        return -1;
    }
    return 0;
}

int shard_load(shard_t *shard) {
    debug("shard_load\n");
    if (shard_open(shard, "r") < 0) {
        debug("Open failed\n");
        return -1;
    }
    if (shard_magic_load(shard) < 0) {
        debug("Magic load failed\n");
        return -1;
    }
    if (shard_header_load(shard) < 0) {
        debug("Header load failed\n");
        return -1;
    }
    return shard_hash_load(shard);
}

/**********************************************************
 * Open a Shard and delete an object by zeroing out its
 * location in the hash table and its content.
 *
 * Returns:
 *  0 if delete was successful,
 *  1 if the key has already been deleted,
 * -1 in case of error.
 */
int shard_delete(shard_t *shard, const char *key) {
    cmph_uint32 h;
    uint64_t index_offset;
    char object_id[SHARD_KEY_LEN];
    uint64_t object_offset;
    uint64_t object_size;

    debug("shard_delete: loading\n");
    if (shard_open(shard, "r+") < 0) {
        return -1;
    }
    if (shard_magic_load(shard) < 0) {
        return -1;
    }
    if (shard_header_load(shard) < 0) {
        return -1;
    }
    if (shard_hash_load(shard) < 0) {
        return -1;
    }
    debug("shard_delete: looking up the key\n");
    h = cmph_search(shard->hash, key, SHARD_KEY_LEN);
    index_offset = shard->header.index_position + h * sizeof(shard_index_t);
    if (shard_seek(shard, index_offset, SEEK_SET) < 0) {
        printf("shard_delete: index_offset\n");
        return -1;
    }
    if (shard_read(shard, object_id, SHARD_KEY_LEN) < 0) {
        printf("shard_delete: object_id\n");
        return -1;
    }
    if (shard_read_uint64_t(shard, &object_offset) < 0) {
        printf("shard_delete: object_offset\n");
        return -1;
    }
    /* Has the object already been deleted? */
    if (object_offset == UINT64_MAX) {
        return 1;
    }
    /* We compare the key after the offset so we have a way to
     * detect removed objects. */
    if (memcmp(key, object_id, SHARD_KEY_LEN) != 0) {
        printf("shard_delete: key mismatch\n");
        return -1;
    }
    debug("shard_delete: reading object size\n");
    if (shard_seek(shard, object_offset, SEEK_SET) < 0) {
        printf("shard_delete: object_offset read\n");
        return -1;
    }
    if (shard_read_uint64_t(shard, &object_size) < 0) {
        printf("shard_delete: object_size\n");
        return -1;
    }
    debug("shard_delete: filling object size and data (len: %" PRIu64
          ") with zeros\n",
          object_size);
    if (shard_seek(shard, object_offset, SEEK_SET) < 0) {
        printf("shard_delete: object_offset fill\n");
        return -1;
    }
    if (shard_write_zeros(shard, sizeof(uint64_t) + object_size) < 0) {
        printf("shard_delete: write_zeros\n");
        return -1;
    }
    debug("shard_delete: writing tombstone for object offset\n");
    if (shard_seek(shard, index_offset, SEEK_SET) < 0) {
        printf("shard_delete: index_offset\n");
        return -1;
    }
    if (shard_write_zeros(shard, SHARD_KEY_LEN) < 0) {
        printf("shard_delete: rewrite key\n");
        return -1;
    }
    object_offset = UINT64_MAX;
    if (shard_write(shard, &object_offset, sizeof(uint64_t)) < 0) {
        printf("shard_delete: rewrite offset\n");
        return -1;
    }
    debug("shard_delete: closing\n");
    if (shard_close(shard) < 0) {
        printf("shard_delete: close\n");
        return -1;
    }
    return 0;
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

#ifdef __cplusplus
}
#endif
