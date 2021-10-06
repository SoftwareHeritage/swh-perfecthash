from cffi import FFI

ffibuilder = FFI()

# cdef() expects a single string declaring the C types, functions and
# globals needed to use the shared object. It must be in valid C syntax.
ffibuilder.cdef(
    """
int build(char* path);
"""
)

ffibuilder.set_source(
    "_hash_cffi",
    """
                      #include "swh/perfecthash/hash.h"
                      """,
    sources=["swh/perfecthash/hash.c"],
    include_dirs=["."],
    libraries=["cmph"],
)  # library name, for the linker

if __name__ == "__main__":
    ffibuilder.compile(verbose=True)
