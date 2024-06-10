# Copyright (C) 2021-2024  The Software Heritage developers
# See the AUTHORS file at the top-level directory of this distribution
# License: GNU General Public License version 3, or any later version
# See top-level LICENSE file for more information

from pathlib import Path
import platform

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

int shard_delete(shard_t* shard, const char *key);

extern const int shard_key_len;
"""
)

library_dirs = []
extra_compile_args = ["-D_FILE_OFFSET_BITS=64"]
bundled_cmph = Path(__file__).parent.parent.parent / "cmph"
if bundled_cmph.is_dir():
    library_dirs.append(str(bundled_cmph / "lib"))
    extra_compile_args.append(f"-I{bundled_cmph}/include")
elif platform.system() == "Darwin" and Path("/usr/local/include/cmph.h").is_file():
    # ensure to find cmph on macOS if installed with Homebrew using "brew install libcmph"
    library_dirs.append("/usr/local/lib")
    extra_compile_args.append("-I/usr/local/include")
elif platform.system() == "Darwin" and Path("/opt/local/include/cmph.h").is_file():
    # ensure to find cmph on macOS if installed with MacPorts using "port install cmph"
    library_dirs.append("/opt/local/lib")
    extra_compile_args.append("-I/opt/local/include")


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
