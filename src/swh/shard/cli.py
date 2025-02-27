# Copyright (C) 2025  The Software Heritage developers
# See the AUTHORS file at the top-level directory of this distribution
# License: GNU General Public License version 3, or any later version
# See top-level LICENSE file for more information

import logging

import click

# WARNING: do not import unnecessary things here to keep cli startup time under
# control


logger = logging.getLogger(__name__)

CONTEXT_SETTINGS = dict(help_option_names=["-h", "--help"])

try:
    # make this cli usable both from the swh.core's 'swh' cli group and from
    # direct swh-shard command (since swh-shard does not depend on swh.core)
    from swh.core.cli import swh

    cli_group = swh.group
except (ImportError, ModuleNotFoundError):
    cli_group = click.group


@cli_group(name="shard", context_settings=CONTEXT_SETTINGS)
@click.pass_context
def shard_cli_group(ctx):
    """Software Heritage Shard tools."""


@shard_cli_group.command("info")
@click.argument("shard", required=True, nargs=-1)
@click.pass_context
def shard_info(ctx, shard):
    "Display shard file information"

    from swh.shard import Shard

    for shardfile in shard:
        with Shard(shardfile) as s:
            h = s.header
            click.echo(f"Shard {shardfile}")
            click.echo(f"├─version:    {h.version}")
            click.echo(f"├─objects:    {h.objects_count}")
            click.echo(f"│ ├─position: {h.objects_position}")
            click.echo(f"│ └─size:     {h.objects_size}")
            click.echo("├─index")
            click.echo(f"│ ├─position: {h.index_position}")
            click.echo(f"│ └─size:     {h.index_size}")
            click.echo("└─hash")
            click.echo(f"  └─position: {h.hash_position}")


@shard_cli_group.command("create")
@click.argument("shard", required=True)
@click.argument("files", metavar="files", required=True, nargs=-1)
@click.option(
    "--sorted/--no-sorted",
    "sort_files",
    default=False,
    help=(
        "Sort files by inversed filename before adding them to the shard; "
        "it may help having better compression ratio when compressing "
        "the shard file"
    ),
)
@click.pass_context
def shard_create(ctx, shard, files, sort_files):
    "Create a shard file from given files"

    import hashlib
    import sys

    from swh.shard import ShardCreator

    files = list(files)
    if files == ["-"]:
        # read file names from stdin
        files = [fname.strip() for fname in sys.stdin.read().splitlines()]
    click.echo(f"There are {len(files)} entries")
    hashes = set()
    files_to_add = {}
    for fname in files:
        try:
            data = open(fname, "rb").read()
        except OSError:
            continue
        sha256 = hashlib.sha256(data).digest()
        if sha256 not in hashes:
            files_to_add[fname] = sha256
            hashes.add(sha256)
    click.echo(f"after deduplication: {len(files_to_add)} entries")

    with ShardCreator(shard, len(files_to_add)) as shard:
        it = files_to_add.items()
        if sort_files:
            it = sorted(it, key=lambda x: x[0][-1::-1])
        for fname, sha256 in it:
            data = open(fname, "rb").read()
            shard.write(sha256, data)
    click.echo("Done")


@shard_cli_group.command("ls")
@click.argument("shard", required=True)
@click.pass_context
def shard_list(ctx, shard):
    "List objects in a shard file"

    from swh.shard import Shard

    with Shard(shard) as s:
        for key in s:
            size = s.getsize(key)
            click.echo(f"{key.hex()}: {size} bytes")


@shard_cli_group.command("get")
@click.argument("shard", required=True)
@click.argument("keys", required=True, nargs=-1)
@click.pass_context
def shard_get(ctx, shard, keys):
    "List objects in a shard file"

    from swh.shard import Shard

    with Shard(shard) as s:
        for key in keys:
            click.echo(s[bytes.fromhex(key)], nl=False)


def main():
    # Even though swh() sets up logging, we need an earlier basic logging setup
    # for the next few logging statements
    logging.basicConfig()
    return shard_cli_group(auto_envvar_prefix="SWH")


if __name__ == "__main__":
    main()
