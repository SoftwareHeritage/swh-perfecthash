from _hash_cffi import lib


def test_build():
    assert lib.build(b"path") == 0
