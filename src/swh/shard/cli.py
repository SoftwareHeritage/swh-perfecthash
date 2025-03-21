# Copyright (C) 2025  The Software Heritage developers
# See the AUTHORS file at the top-level directory of this distribution
# License: GNU General Public License version 3, or any later version
# See top-level LICENSE file for more information

import logging

import click

# WARNING: do not import unnecessary things here to keep cli startup time under
# control


logger = logging.getLogger(__name__)

# marker of a deleted/non-populated index entry
NULLKEY = b"\x00" * 32

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
@click.argument(
    "shard", required=True, nargs=-1, type=click.Path(exists=True, dir_okay=False)
)
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
@click.argument(
    "shard", required=True, type=click.Path(exists=False, dir_okay=False, writable=True)
)
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
    import os
    import sys

    from swh.shard import ShardCreator

    if os.path.exists(shard):
        raise click.ClickException(f"Shard file {shard} already exists. Aborted!")

    files = list(files)
    if files == ["-"]:
        # read file names from stdin
        files = [fname.strip() for fname in sys.stdin.read().splitlines()]
    click.echo(f"There are {len(files)} entries")
    hashes = set()
    files_to_add = {}
    with click.progressbar(files, label="Checking files to add") as bfiles:
        for fname in bfiles:
            try:
                with open(fname, "rb") as f:
                    sha256 = hashlib.sha256(f.read()).digest()
                    if sha256 not in hashes:
                        files_to_add[fname] = sha256
                        hashes.add(sha256)
            except OSError:
                continue
    click.echo(f"after deduplication: {len(files_to_add)} entries")

    with ShardCreator(shard, len(files_to_add)) as shard:
        it = files_to_add.items()
        if sort_files:
            it = sorted(it, key=lambda x: x[0][-1::-1])
        with click.progressbar(it, label="Adding files to the shard") as items:
            for fname, sha256 in items:
                with open(fname, "rb") as f:
                    shard.write(sha256, f.read())
    click.echo("Done")


@shard_cli_group.command("ls")
@click.option("--skip-removed", default=False, is_flag=True)
@click.argument("shard", required=True, type=click.Path(exists=True, dir_okay=False))
@click.pass_context
def shard_list(ctx, skip_removed, shard):
    "List objects in a shard file"

    from swh.shard import Shard

    with Shard(shard) as s:
        for key in s:
            if skip_removed and key == NULLKEY:
                continue
            try:
                size = s.getsize(key)
            except KeyError:
                size = "N/A"
            click.echo(f"{key.hex()}: {size} bytes")


@shard_cli_group.command("get")
@click.argument("shard", required=True, type=click.Path(exists=True, dir_okay=False))
@click.argument("keys", required=True, nargs=-1)
@click.pass_context
def shard_get(ctx, shard, keys):
    "List objects in a shard file"

    from swh.shard import Shard

    with Shard(shard) as s:
        for key in keys:
            click.echo(s[bytes.fromhex(key)], nl=False)


@shard_cli_group.command("delete")
@click.argument(
    "shard", required=True, type=click.Path(exists=True, dir_okay=False, writable=True)
)
@click.argument("keys", required=True, nargs=-1)
@click.option(
    "--confirm/--no-confirm",
    default=True,
    help="Ask for confirmation before performing the deletion",
)
@click.pass_context
def shard_delete(ctx, shard, keys, confirm):
    """Delete objects from a shard file

    Keys to delete from the shard file are expected to be given as hex
    representation. If there is only one argument '-', then read the list of
    keys from stdin. Implies --no-confirm.

    If at least one key is missing or invalid, the whole process is aborted.

    """
    import sys

    if keys == ("-",):
        keys = sys.stdin.read().split()
        confirm = False
    if len(set(keys)) < len(keys):
        click.fail("There are duplicate keys, aborting")

    from swh.shard import Shard

    obj_size = {}
    with Shard(shard) as s:
        for key in keys:
            try:
                obj_size[key] = s.getsize(bytes.fromhex(key))
            except ValueError:
                click.secho(f"{key}: key is invalid", fg="red")
            except KeyError:
                click.secho(f"{key}: key not found", fg="red")
    if len(obj_size) < len(keys):
        raise click.ClickException(
            "There have been errors for at least one key, aborting"
        )
    click.echo(f"About to remove these objects from the shard file {shard}")
    for key in keys:
        click.echo(f"{key} ({obj_size[key]} bytes)")
    if confirm:
        click.confirm(
            click.style(
                "Proceed?",
                fg="yellow",
                bold=True,
            ),
            abort=True,
        )
    with click.progressbar(keys, label="Deleting objects from the shard") as barkeys:
        for key in barkeys:
            Shard.delete(shard, bytes.fromhex(key))
    click.echo("Done")


def main():
    # Even though swh() sets up logging, we need an earlier basic logging setup
    # for the next few logging statements
    logging.basicConfig()
    return shard_cli_group(auto_envvar_prefix="SWH")


if __name__ == "__main__":
    main()
