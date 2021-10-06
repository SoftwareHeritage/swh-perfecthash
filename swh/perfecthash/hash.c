#include "swh/perfecthash/hash.h"

int build(char *path) {
    return 0;
}

#if 0

#include <sys/mman.h>
#include <sys/stat.h>

#include <cmph_types.h>

typedef struct {
    size_t offset_hash;
    size_t count;
} shard_header_t;

int header_init(shard_header_t *shard, int fd) {
    if (read(fd, sizeof(shared_header_t), (char*)shard) < 0) {
	perror("read");
	return -1;
    }
    return 0;
}

int header_save(shard_header_t *shard, int fd) {
    if (seek(fd, 0) < 0) {
	perror("seek");
	return -1;
    }
    if (write(fd, (char*)shard, sizeof(shard_header_t)) < 0) {
	perror("write");
	return -1;
    }
    return 0;
}

typedef struct {
    void *addr;
    shard_header_t header;
    size_t size;
    size_t offset;
} shard_t;

int shard_init(shard_t *shard, int fd)
{
    struct stat sb;
    if (fstat(fd, &sb) == -1) {
	perror("fstat");
	return -1;
    }
    shard->size = sb.st_size;
    shard->addr = mmap(NULL, shard->size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (shard->addr == NULL) {
	perror("mmap");
	return -1;
    }
    header_init(&shard->header, fd);
    shard->header.offset_hash = sb.st_size;
    header_save(shard, fd)
    shard_rewind(shard);
    return 0;
}

int shard_uninit(shard_t *shard)
{
    return munmap(shard->addr, shard->size);
}

int shard_read(shard_t *shard, char **key, cmph_uint32 *keylen) {
    *key = (char *)(shard->data);
    *keylen = (cmph_uint32)SHARD_SHA256_LEN;
    size_t size = *(size_t *)(shard->data + SHARD_SHA256_LEN);
    offset += SHARD_SHA256_LEN + sizeof(size_t) + size;
}

void shard_rewind(shard_t *shard) {
    shard->offset = SHARD_OFFSET_HEADER + SHARD_SIZE_HEADER;
}

static int io_read(void *data, char **key, cmph_uint32 *keylen) {
    shard_t *shard = (shard_t *)data;
    return shard_read(shard, key, keylen);
}

static void io_dispose(void *data, char *key, cmph_uint32 keylen) {
}

static void io_rewind(void *data) {
    shard_rewind((shard_t *)data);
}

cmph_io_adapter_t *io_adapter(int fd) {
    cmph_io_adapter_t * key_source = (cmph_io_adapter_t *)malloc(sizeof(cmph_io_adapter_t));
    if (key_source == NULL)
	return NULL
    cmph_io_adapter_t * shard = (shard_t *)malloc(sizeof(shard_t));
    if (shard == NULL)
	return NULL;
    if (shard_init(shard, fd) < 0)
	return NULL;

    key_source->data = (void *)shard;
    key_source->nkeys = shard->header.count;
    key_source->read = io_read;
    key_source->dispose = io_dispose;
    key_source->rewind = io_rewind;
    return key_source;
}

int build(char *path) {
    int fd = open(path, "r");
    if (fd < 0) {
	perror("open");
	return -1;
    }
    cmph_io_adapter_t *source = io_adapter(fd);
    shard_t *shard = (shard_t *)source->data;
    cmph_config_t *config = cmph_config_new(source);
    cmph_config_set_algo(config, CMPH_CHD_PH);
    // cmph_config_set_keys_per_bin
    // cmph_config_set_b
    cmph_t *hash = cmph_new(config);
    FILE* mphf_fd = fopen(path, "a");
    fseek(mphf_fd, shard->header.offset_hash);
    cmph_config_destroy(config);
    cmph_dump(hash, mphf_fd);
    cmph_destroy(hash);
    fclose(mphf_fd);
}

cmph_t *load(char *path) {

}

#endif
