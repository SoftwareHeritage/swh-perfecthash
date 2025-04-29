# Copyright (C) 2021-2022  The Software Heritage developers
# See the AUTHORS file at the top-level directory of this distribution
# License: GNU General Public License version 3, or any later version
# See top-level LICENSE file for more information

import gc
from hashlib import sha256
import logging
import os
from pathlib import Path
import platform
import random
import resource
import time

import pytest

from swh.shard import Shard, ShardCreator

logger = logging.getLogger(__name__)


PYPY = platform.python_implementation() == "PyPy"


@pytest.fixture(scope="function", autouse=True)
def setrlimit(request):
    marker = request.node.get_closest_marker("setrlimit")
    rlimits = []
    if marker is not None:
        for which, (soft, hard) in marker.args:
            backup = resource.getrlimit(which)
            logger.info("Saving rlimit %s (%s, %s)", which, *backup)
            rlimits.append((which, backup))
            logger.info("Setting rlimit %s (%s, %s)", which, soft, hard)
            resource.setrlimit(which, (soft, hard))

    yield

    for which, (soft, hard) in rlimits:
        logger.info("Resetting rlimit %s (%s, %s)", which, soft, hard)
        resource.setrlimit(which, (soft, hard))
        result = resource.getrlimit(which)
        logger.info("Resulting rlimit %s (%s, %s)", which, *result)


KEY_A = b"A" * Shard.key_len
KEY_B = b"B" * Shard.key_len
KEY_C = b"C" * Shard.key_len

OBJECT_A = b"AAAA"
OBJECT_B = b"BBBB"
# Use a size bigger than BUF_SIZE used in shard_write_zeros()
OBJECT_C = b"C" * 10_000


@pytest.fixture
def populated_shard_path(tmp_path):
    shard_path = tmp_path / "shard"
    shard_path.open("w").close()
    os.truncate(shard_path, 10 * 1024 * 1024)

    with ShardCreator(str(shard_path), 3) as s:
        s.write(KEY_A, OBJECT_A)
        s.write(KEY_B, OBJECT_B)
        s.write(KEY_C, OBJECT_C)

    return str(shard_path)


def test_lookup(populated_shard_path):
    with Shard(populated_shard_path) as s:
        assert s.lookup(KEY_A) == OBJECT_A
        assert s.lookup(KEY_B) == OBJECT_B
        assert s.lookup(KEY_C) == OBJECT_C


def test_creator_open_without_permission(tmpdir):
    path = Path(tmpdir / "no-perm")
    path.touch()
    # Remove all permissions
    path.chmod(0o000)
    exc_cls = RuntimeError if PYPY else PermissionError
    with pytest.raises(exc_cls):
        with ShardCreator(str(path), 1):
            pass


@pytest.mark.setrlimit((resource.RLIMIT_FSIZE, (64_000, -1)))
def test_write_above_rlimit_fsize(tmpdir):
    with pytest.raises(OSError, match=r"File too large"):
        with ShardCreator(f"{tmpdir}/test-shard", 1) as shard:
            shard.write(b"A" * Shard.key_len, b"A" * 72_000)


def test_write_errors_if_too_many(tmpdir):
    with ShardCreator(f"{tmpdir}/shard", 1) as shard:
        shard.write(b"A" * Shard.key_len, b"AAAA")
        with pytest.raises(ValueError):
            shard.write(b"B" * Shard.key_len, b"BBBB")


def test_write_errors_if_not_enought(tmpdir):
    with pytest.raises(RuntimeError):
        with ShardCreator(f"{tmpdir}/shard", 2) as shard:
            shard.write(b"A" * Shard.key_len, b"AAAA")


def test_write_errors_for_wrong_key_len(tmpdir):
    with ShardCreator(f"{tmpdir}/shard", 1) as shard:
        with pytest.raises(ValueError):
            shard.write(b"A", b"AAAA")
        # add a valid entry so the __exit__ does not raise an exception
        shard.write(b"A" * Shard.key_len, b"AAAA")


def test_creator_context_does_not_run_finalize_on_error(tmpdir, mocker):
    pytest.skip("Not sure how to do that...")
    import contextlib

    mock_method = mocker.patch.object(ShardCreator, "__exit__")
    with contextlib.suppress(KeyError):
        with ShardCreator(f"{tmpdir}/shard", 1) as _:
            raise KeyError(42)
    mock_method.assert_not_called()


@pytest.mark.setrlimit((resource.RLIMIT_FSIZE, (64_000, -1)))
def test_finalize_above_rlimit_fsize(tmpdir):
    path = f"{tmpdir}/shard"
    with pytest.raises(OSError, match="File too large"):
        with ShardCreator(path, 1) as shard:
            shard.write(b"A" * Shard.key_len, b"A" * 63_500)


def test_creator_errors_with_duplicate_key(tmpdir):
    with pytest.raises(RuntimeError, match="duplicate"):
        with ShardCreator(f"{tmpdir}/shard", 2) as shard:
            shard.write(b"A" * Shard.key_len, b"AAAA")
            shard.write(b"A" * Shard.key_len, b"BBBB")


def test_load_non_existing():
    exc_cls = RuntimeError if PYPY else FileNotFoundError
    with pytest.raises(exc_cls):
        _ = Shard("/nonexistent")


@pytest.fixture
def corrupted_shard_path(tmpdir):
    # taken from hash.h
    SHARD_OFFSET_HEADER = 512
    path = f"{tmpdir}/corrupted"
    with ShardCreator(path, 1) as s:
        s.write(b"A" * Shard.key_len, b"AAAA")
    with open(path, "rb+") as f:
        f.seek(SHARD_OFFSET_HEADER)
        # replace the object size (uint64_t) by something larger than file size
        f.write(b"\x00\x00\x00\x00\x00\x00\xff\xff")
    return path


def test_lookup_failure(corrupted_shard_path):
    with Shard(corrupted_shard_path) as shard:
        with pytest.raises(RuntimeError, match=r"failed.*corrupted"):
            shard.lookup(b"A" * Shard.key_len)


def test_lookup_errors_for_wrong_key_len(tmpdir):
    with pytest.raises(ValueError):
        with ShardCreator(f"{tmpdir}/shard", 1) as shard:
            shard.write(b"A", b"AAAA")


@pytest.fixture
def shard_with_mismatched_key(tmp_path):
    path = tmp_path / "mismatched"
    with ShardCreator(str(path), 1) as s:
        s.write(b"A" * Shard.key_len, b"AAAA")
    # Replace the key in the index
    content = path.read_bytes()
    path.write_bytes(content.replace(b"A" * Shard.key_len, b"B" * Shard.key_len))
    return str(path)


def test_lookup_errors_for_mismatched_key(shard_with_mismatched_key):
    with Shard(shard_with_mismatched_key) as shard:
        with pytest.raises(KeyError):
            shard.lookup(b"A" * Shard.key_len)


@pytest.fixture
def shard_with_deleted_objects_path(populated_shard_path):
    Shard.delete(populated_shard_path, KEY_A)
    Shard.delete(populated_shard_path, KEY_C)
    return populated_shard_path


def test_delete_has_zeroed_object_data(shard_with_deleted_objects_path):
    shard_content = open(shard_with_deleted_objects_path, "rb").read()
    assert b"AAAA" not in shard_content
    assert b"CCCCCCCC" not in shard_content


def test_delete_leaves_other_keys_working(shard_with_deleted_objects_path):
    with Shard(shard_with_deleted_objects_path) as shard:
        assert shard.lookup(KEY_B) == OBJECT_B


def test_lookup_on_deleted_raises_key_error(shard_with_deleted_objects_path):
    with Shard(shard_with_deleted_objects_path) as shard:
        with pytest.raises(KeyError):
            _ = shard.lookup(KEY_A)
        with pytest.raises(KeyError):
            _ = shard.lookup(KEY_C)


@pytest.fixture
def payload(request):
    size = request.config.getoption("--shard-size")
    path = request.config.getoption("--shard-path")
    if not os.path.exists(path) or os.path.getsize(path) != size * 1024 * 1024:
        os.system(f"dd if=/dev/urandom of={path} count={size} bs=1024k")
    return path


#
# PYTHONMALLOC=malloc valgrind --tool=memcheck .tox/py3/bin/pytest \
#    -k test_build_speed swh/perfecthash/tests/test_hash.py |& tee /tmp/v
#
def test_build_speed(request, tmpdir, payload):
    start = time.time()
    os.system(f"cp {payload} {tmpdir}/shard")
    baseline = time.time() - start
    write_duration, build_duration, _ = shard_build(request, tmpdir, payload)
    duration = write_duration + build_duration
    print(
        f"baseline {baseline}, "
        f"write_duration {write_duration}, "
        f"build_duration {build_duration}, "
        f"total_duration {duration}"
    )
    #
    # According to the docs/benchmarks.rst analysis, the duration is below 5
    # times the baseline time. This assertion is here to ensure we do not not
    # regress in the future... (we use x10 to give a bit of slack otherwise the
    # test is pretty unstable)
    #
    k = 100 if PYPY else 10
    assert duration < baseline * k


def test_lookup_speed(request, tmpdir, payload):
    _, _, objects = shard_build(request, tmpdir, payload)
    for i in range(request.config.getoption("--shard-count")):
        os.system(f"cp {tmpdir}/shard {tmpdir}/shard{i}")
    start = time.time()
    shard_lookups(request, tmpdir, objects)
    duration = time.time() - start

    lookups = request.config.getoption("--lookups")
    key_per_sec = lookups / duration
    print(f"key lookups speed = {key_per_sec:.2f}/s")


def shard_lookups(request, tmpdir, objects):
    shard_path = f"{tmpdir}/shard"
    shards = []
    for i in range(request.config.getoption("--shard-count")):
        shards.append(Shard(f"{shard_path}{i}"))
    lookups = request.config.getoption("--lookups")
    count = 0
    while True:
        for key, object_size in objects.items():
            if count >= lookups:
                return
            for shard in shards:
                object = shard.lookup(key)
                assert len(object) == object_size
                count += 1
    for shard in shards:
        shard.close()


def shard_build(request, tmpdir, payload):
    shard_size = request.config.getoption("--shard-size") * 1024 * 1024
    shard_path = f"{tmpdir}/shard"
    open(shard_path, "w").close()
    os.truncate(shard_path, shard_size * 2)

    object_max_size = request.config.getoption("--object-max-size")
    objects = {}
    count = 0
    size = 0
    keys = []
    with open(payload, "rb") as f:
        while True:
            object = f.read(random.randrange(512, object_max_size))
            if len(object) < 512:
                break
            key = sha256(object).digest()
            objects[key] = len(object)
            size += len(object)
            count += 1
            keys.append(key)

    print(f"number of objects = {count}, total size = {size}")
    assert size <= shard_size
    start = time.time()

    with ShardCreator(shard_path, len(objects)) as shard:
        count = 0
        size = 0
        with open(payload, "rb") as f:
            for key in keys:
                object = f.read(objects[key])
                assert len(object) == objects[key]
                count += 1
                size += len(object)
                shard.write(key, object)
        write_duration = time.time() - start
        start = time.time()

    build_duration = time.time() - start
    return write_duration, build_duration, objects


def test_memleak(request, tmpdir, payload):
    "Naive test for memleak in ShardReader"
    shard_build(request, tmpdir, payload)
    shard_file = str(tmpdir / "shard")
    maxrss = [resource.getrusage(resource.RUSAGE_SELF).ru_maxrss]
    for i in range(100):
        # run the garbage collector first to avoid the memory of one iteration
        # to affect the next one
        gc.collect()
        with Shard(shard_file) as s:
            for key in s:
                obj = s[key]
                assert sha256(obj).digest() == key
        maxrss.append(resource.getrusage(resource.RUSAGE_SELF).ru_maxrss)
    # on pypy, the used rss can still grow significantly during the first gew
    # iterations, but should remain under a reasonable threshold
    assert (maxrss[-1] - maxrss[1]) < 100 * 1024  # in kB
    # but there should be none for the last iterations
    for i in range(99, 90, -1):
        assert (maxrss[i] - maxrss[i - 1]) == 0, maxrss[i] - maxrss[i - 1]
