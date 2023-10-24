# Copyright (C) 2021-2022  The Software Heritage developers
# See the AUTHORS file at the top-level directory of this distribution
# License: GNU General Public License version 3, or any later version
# See top-level LICENSE file for more information

import logging
import os
from pathlib import Path
import random
import resource
import time

import pytest

from swh.perfecthash import Shard, ShardCreator

logger = logging.getLogger(__name__)


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


def test_all(tmpdir):
    f = f"{tmpdir}/shard"
    open(f, "w").close()
    os.truncate(f, 10 * 1024 * 1024)

    with ShardCreator(f, 2) as s:
        keyA = b"A" * Shard.key_len()
        objectA = b"AAAA"
        s.write(keyA, objectA)
        keyB = b"B" * Shard.key_len()
        objectB = b"BBBB"
        s.write(keyB, objectB)

    with Shard(f) as s:
        assert s.lookup(keyA) == objectA
        assert s.lookup(keyB) == objectB


def test_creator_open_without_permission(tmpdir):
    path = Path(tmpdir / "no-perm")
    path.touch()
    # Remove all permissions
    path.chmod(0o000)
    shard = ShardCreator(str(path), 1)
    with pytest.raises(PermissionError, match="no-perm"):
        shard.prepare()


@pytest.mark.setrlimit((resource.RLIMIT_FSIZE, (64_000, -1)))
def test_write_above_rlimit_fsize(tmpdir):
    shard = ShardCreator(f"{tmpdir}/test-shard", 1)
    shard.prepare()
    with pytest.raises(OSError, match=r"File too large.*test-shard"):
        shard.write(b"A" * Shard.key_len(), b"A" * 72_000)


def test_write_errors_if_too_many(tmpdir):
    shard = ShardCreator(f"{tmpdir}/shard", 1)
    shard.prepare()
    shard.write(b"A" * Shard.key_len(), b"AAAA")
    with pytest.raises(ValueError):
        shard.write(b"B" * Shard.key_len(), b"BBBB")


def test_write_errors_for_wrong_key_len(tmpdir):
    shard = ShardCreator(f"{tmpdir}/shard", 1)
    shard.prepare()
    with pytest.raises(ValueError):
        shard.write(b"A", b"AAAA")


def test_creator_context_does_not_run_finalize_on_error(tmpdir, mocker):
    import contextlib

    mock_method = mocker.patch.object(ShardCreator, "finalize")
    with contextlib.suppress(KeyError):
        with ShardCreator(f"{tmpdir}/shard", 1) as _:
            raise KeyError(42)
    mock_method.assert_not_called()


@pytest.mark.setrlimit((resource.RLIMIT_FSIZE, (64_000, -1)))
def test_finalize_above_rlimit_fsize(tmpdir):
    path = f"{tmpdir}/shard"
    shard = ShardCreator(path, 1)
    shard.prepare()
    shard.write(b"A" * Shard.key_len(), b"A" * 63_500)
    with pytest.raises(OSError, match="File too large"):
        shard.finalize()


def test_creator_errors_with_duplicate_key(tmpdir):
    shard = ShardCreator(f"{tmpdir}/shard", 2)
    shard.prepare()
    shard.write(b"A" * Shard.key_len(), b"AAAA")
    shard.write(b"A" * Shard.key_len(), b"AAAA")
    with pytest.raises(RuntimeError, match="duplicate"):
        shard.finalize()


def test_load_non_existing():
    with pytest.raises(FileNotFoundError, match="/nonexistent"):
        _ = Shard("/nonexistent")


@pytest.fixture
def corrupted_shard_path(tmpdir):
    # taken from hash.h
    SHARD_OFFSET_HEADER = 512
    path = f"{tmpdir}/corrupted"
    with ShardCreator(path, 1) as s:
        s.write(b"A" * Shard.key_len(), b"AAAA")
    with open(path, "rb+") as f:
        f.seek(SHARD_OFFSET_HEADER)
        # replace the object size (uint64_t) by something larger than file size
        f.write(b"\x00\x00\x00\x00\x00\x00\xFF\xFF")
    return path


def test_lookup_failure(corrupted_shard_path):
    with Shard(corrupted_shard_path) as shard:
        with pytest.raises(RuntimeError, match=r"failed.*/corrupted"):
            shard.lookup(b"A" * Shard.key_len())


def test_lookup_errors_for_wrong_key_len(tmpdir):
    shard = ShardCreator(f"{tmpdir}/shard", 1)
    shard.prepare()
    with pytest.raises(ValueError):
        shard.write(b"A", b"AAAA")


@pytest.fixture
def shard_with_mismatched_key(tmp_path):
    path = tmp_path / "mismatched"
    with ShardCreator(str(path), 1) as s:
        s.write(b"A" * Shard.key_len(), b"AAAA")
    # Replace the key in the index
    content = path.read_bytes()
    path.write_bytes(content.replace(b"A" * Shard.key_len(), b"B" * Shard.key_len()))
    return str(path)


def test_lookup_errors_for_mismatched_key(shard_with_mismatched_key):
    with Shard(shard_with_mismatched_key) as shard:
        with pytest.raises(RuntimeError, match=r"Mismatch"):
            shard.lookup(b"A" * Shard.key_len())


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
    # According to the docs/benchmarks.rst analysis, the duration is
    # below 5 times the baseline time This assertion is here to ensure
    # we do not not regress in the future...
    #
    assert duration < baseline * 5


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
    with open(payload, "rb") as f:
        while True:
            key = f.read(Shard.key_len())
            if len(key) < Shard.key_len():
                break
            assert key not in objects
            object = f.read(random.randrange(512, object_max_size))
            if len(object) < 512:
                break
            objects[key] = len(object)
            size += len(object)
            count += 1

    print(f"number of objects = {count}, total size = {size}")
    assert size < shard_size
    start = time.time()

    shard = ShardCreator(shard_path, len(objects))
    shard.prepare()

    count = 0
    size = 0
    with open(payload, "rb") as f:
        while True:
            key = f.read(Shard.key_len())
            if len(key) < Shard.key_len():
                break
            if key not in objects:
                break
            object = f.read(objects[key])
            assert len(object) == objects[key]
            count += 1
            size += len(object)
            shard.write(key, object)
    write_duration = time.time() - start
    start = time.time()
    shard.finalize()
    build_duration = time.time() - start
    return write_duration, build_duration, objects
