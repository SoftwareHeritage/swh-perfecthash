# Copyright (C) 2021-2025  The Software Heritage developers
# See the AUTHORS file at the top-level directory of this distribution
# License: GNU General Public License version 3, or any later version
# See top-level LICENSE file for more information

from ._shard import ShardCreator, ShardReader

__all__ = ["Shard", "ShardCreator"]


class Shard(ShardReader):
    # for BW compat reason, implement the context manager protocol
    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        self.close()

    def __iter__(self):
        # iterate of the keys
        for i in range(self.header.index_size // (32 + 8)):  # KEY_LEN + uint64
            idx = self.getindex(i)
            if idx.object_offset < (2**64 - 1):
                yield idx.key
