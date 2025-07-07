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
        ctx.fail("There are duplicate keys, aborting")

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


@shard_cli_group.command("check")
@click.argument(
    "shard", required=True, nargs=-1, type=click.Path(exists=True, dir_okay=False)
)
@click.option(
    "--with-hash", is_flag=True, default=False, help="Also check objects hashes (slow!)"
)
@click.pass_context
def shard_check(ctx, shard, with_hash):
    "Check a shard file for data corruption or inconsistencies"

    import hashlib
    import struct

    from swh.shard import Shard

    errors = []
    sizes = []
    for shardfile in shard:
        with Shard(shardfile) as s:
            h = s.header
            n_entries = h.index_size // 40  # 32 + 8
            idx = h.index_position
            obj_pos = h.objects_position
            logger.debug("IDX=%s", idx)
            logger.debug("OBJS=%s", obj_pos)
            logger.debug("ENTRIES=%s", n_entries)

            # for most of the checks we only use direct access to the file,
            # without using the C extension _shard, but for checking the
            # hashmap; so we have the shard file open twice here: one as a
            # regular python (binary) file object, and one via the Shard object
            # encapsulating the C extension.
            with open(shardfile, "rb") as fob:
                # build the list of object offsets as built in the *objects*
                # section of the shard file
                obj_offsets = []
                fob.seek(h.objects_position)
                with click.progressbar(
                    length=h.objects_size, label="listing objects"
                ) as bar:
                    while True:
                        obj_offsets.append(fob.tell())
                        size = struct.unpack(">Q", fob.read(8))[0]
                        sizes.append(size)
                        fob.seek(size, 1)
                        bar.update(8 + size)
                        if (fob.tell() - h.objects_position) >= h.objects_size:
                            break
                # build the list of object offsets as defined in the *Index*
                # section of the shard file. If asked for, also check each object's
                # sha256 sum while iterating on the index entries.
                idx_offsets = []
                fob.seek(h.index_position)
                with click.progressbar(
                    length=h.index_size, label="listing indexes"
                ) as bar:
                    while True:
                        key = fob.read(32)
                        offset = fob.read(8)
                        if key != NULLKEY:
                            idx_offsets.append(struct.unpack(">Q", offset)[0])
                            pos = fob.tell()
                            fob.seek(idx_offsets[-1])
                            size = struct.unpack(">Q", fob.read(8))[0]
                            try:
                                # calling Shard.getsize() will use the hashmap,
                                # thus checking it's OK for the key
                                s.getsize(key)
                            except Exception as exc:
                                errors.append(
                                    f"{key.hex()}: Shard.getsize() error ({exc})"
                                )
                            if with_hash:
                                content_hash = hashlib.sha256(fob.read(size)).digest()
                                if content_hash != key:
                                    errors.append(
                                        f"{key.hex()}: hash mismatch (computed: {content_hash.hex()})"  # noqa
                                    )
                            fob.seek(pos)
                        # next position (key (32) + offset (8))
                        bar.update(40)
                        if (fob.tell() - h.index_position) >= h.index_size:
                            break
        # compare the 2 offset lists; once sorted, they should match exactly
        if len(idx_offsets) != len(obj_offsets):
            errors.append(
                f"Offset lists len mismatch: {len(idx_offsets)} vs. {len(obj_offsets)}"
            )
        if sorted(idx_offsets) != obj_offsets:
            errors.append("Offset lists mismatch")

    if (sum(sizes) + h.objects_count * 8) != h.objects_size:
        errors.append(
            f"Total size mismatch: {sum(sizes)} + ({h.objects_count} * 8) computed "
            f"vs. {h.objects_size} advertised"
        )
    if errors:
        click.echo(
            "check failed with the following error(s):\n"
            + "\n".join(f"- {err}" for err in errors),
            err=True,
        )
        ctx.exit(1)


def main():
    # Even though swh() sets up logging, we need an earlier basic logging setup
    # for the next few logging statements
    logging.basicConfig()
    return shard_cli_group(auto_envvar_prefix="SWH")


if __name__ == "__main__":
    main()
