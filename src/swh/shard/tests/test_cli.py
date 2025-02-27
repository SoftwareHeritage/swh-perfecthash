# Copyright (C) 2025 The Software Heritage developers
# See the AUTHORS file at the top-level directory of this distribution
# License: GNU General Public License version 3, or any later version
# See top-level LICENSE file for more information

from hashlib import sha256

from click.testing import CliRunner
import pytest

from swh.shard import Shard, ShardCreator, cli


@pytest.fixture
def small_shard(tmp_path):
    with ShardCreator(str(tmp_path / "small.shard"), 16) as shard:
        for i in range(16):
            shard.write(bytes.fromhex(f"{i:-064X}"), bytes((65 + i,)) * 42)
    return tmp_path / "small.shard"


def test_cli():
    runner = CliRunner()
    result = runner.invoke(cli.shard_cli_group)
    assert result.exit_code == 0
    assert "Software Heritage Shard tools" in result.output


def test_cli_info(small_shard):
    runner = CliRunner()
    result = runner.invoke(cli.shard_info, [str(small_shard)])
    assert result.exit_code == 0
    assert (
        result.output
        == f"""\
Shard {small_shard}
├─version:    1
├─objects:    16
│ ├─position: 512
│ └─size:     800
├─index
│ ├─position: 1312
│ └─size:     680
└─hash
  └─position: 1992
"""
    )


def test_cli_ls(small_shard):
    runner = CliRunner()
    result = runner.invoke(cli.shard_list, [str(small_shard)])
    assert result.exit_code == 0
    assert (
        result.output
        == """\
000000000000000000000000000000000000000000000000000000000000000c: 42 bytes
0000000000000000000000000000000000000000000000000000000000000005: 42 bytes
000000000000000000000000000000000000000000000000000000000000000e: 42 bytes
0000000000000000000000000000000000000000000000000000000000000001: 42 bytes
0000000000000000000000000000000000000000000000000000000000000004: 42 bytes
000000000000000000000000000000000000000000000000000000000000000d: 42 bytes
0000000000000000000000000000000000000000000000000000000000000000: 42 bytes
000000000000000000000000000000000000000000000000000000000000000a: 42 bytes
0000000000000000000000000000000000000000000000000000000000000006: 42 bytes
0000000000000000000000000000000000000000000000000000000000000009: 42 bytes
0000000000000000000000000000000000000000000000000000000000000003: 42 bytes
0000000000000000000000000000000000000000000000000000000000000008: 42 bytes
000000000000000000000000000000000000000000000000000000000000000f: 42 bytes
000000000000000000000000000000000000000000000000000000000000000b: 42 bytes
0000000000000000000000000000000000000000000000000000000000000002: 42 bytes
0000000000000000000000000000000000000000000000000000000000000007: 42 bytes
"""
    )


def test_cli_get(small_shard):
    runner = CliRunner()
    for i in range(16):
        result = runner.invoke(cli.shard_get, [str(small_shard), f"{i:-064x}"])
        assert result.exit_code == 0
        assert result.output == chr(65 + i) * 42


def test_cli_create(tmp_path):
    runner = CliRunner()

    files = []
    hashes = []
    for i in range(16):
        f = tmp_path / f"file_{i}"
        data = f"file {i}".encode()
        f.write_bytes(data)
        files.append(str(f))
        hashes.append(sha256(data).digest())
    shard = tmp_path / "shard"
    result = runner.invoke(cli.shard_create, [str(shard), *files])
    assert result.exit_code == 0
    assert result.output.strip().endswith("Done")
    with Shard(str(shard)) as s:
        assert s.header.objects_count == 16
        # check stored sha256 digests are as expected
        assert sorted(list(s)) == sorted(hashes)
