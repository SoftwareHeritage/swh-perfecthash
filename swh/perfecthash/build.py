# Copyright (C) 2021-2022  The Software Heritage developers
# See the AUTHORS file at the top-level directory of this distribution
# License: GNU General Public License version 3, or any later version
# See top-level LICENSE file for more information

import pathlib

from cffi import FFI

ffibuilder = FFI()

# cdef() expects a single string declaring the C types, functions and
# globals needed to use the shared object. It must be in valid C syntax.
#
# The following is only the necessary part parsed by cffi to generate python bindings.
#

ffibuilder.cdef(
    """
typedef struct shard_t shard_t;

shard_t* shard_init(const char* path);
int shard_destroy(shard_t* shard);

int shard_prepare(shard_t* shard, uint64_t objects_count);
int shard_object_write(shard_t* shard, const char* key,
    const char* object, uint64_t object_size);
int shard_finalize(shard_t* shard);

int shard_load(shard_t* shard);
int shard_find_object(shard_t *shard, const char *key, uint64_t *object_size);
int shard_read_object(shard_t *shard, char *object, uint64_t object_size);

extern const int shard_key_len;
"""
)

library_dirs = []
extra_compile_args = ["-D_FILE_OFFSET_BITS=64"]
bundled_cmph = pathlib.Path(__file__).parent.parent.parent / "cmph"
if bundled_cmph.is_dir():
    library_dirs.append(str(bundled_cmph / "lib"))
    extra_compile_args.append(f"-I{bundled_cmph}/include")

ffibuilder.set_source(
    "swh.perfecthash._hash_cffi",
    """
    #include "swh/perfecthash/hash.h"
    """,
    sources=["swh/perfecthash/hash.c"],
    include_dirs=["."],
    libraries=["cmph"],
    library_dirs=library_dirs,
    extra_compile_args=extra_compile_args,
)  # library name, for the linker

if __name__ == "__main__":
    ffibuilder.compile(verbose=True)
