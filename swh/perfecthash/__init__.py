# Copyright (C) 2021-2022  The Software Heritage developers
# See the AUTHORS file at the top-level directory of this distribution
# License: GNU General Public License version 3, or any later version
# See top-level LICENSE file for more information

from types import TracebackType
from typing import NewType, Optional, Type, cast

from cffi import FFI

from swh.perfecthash._hash_cffi import lib

Key = NewType("Key", bytes)
HashObject = NewType("HashObject", bytes)


class ShardCreator:
    def __init__(self, path: str, object_count: int):
        """Create a Shard.

        The file at ``path`` will be truncated if it already exists.

        ``object_count`` must match the number of objects that will be added
        using the :meth:`write` method. A ``RuntimeError`` will be raised
        on :meth:`save` in case of inconsistencies.

        Ideally this should be done using a ``with`` statement, as such:

        .. code-block:: python

            with ShardCreator("shard", len(objects)) as shard:
                for key, object in objects.items():
                    shard.write(key, object)

        Otherwise, :meth:`create`, :meth:`write` and :meth:`save` must be
        called in sequence.

        Args:
            path: path to the Shard file or device that will be written.
            object_count: number of objects that will be written to the Shard.
        """

        self.path = path
        self.object_count = object_count
        self.shard = None

    def __enter__(self) -> "ShardCreator":
        self.create()
        return self

    def __exit__(
        self,
        exc_type: Optional[Type[BaseException]],
        exc_val: Optional[BaseException],
        exc_tb: Optional[TracebackType],
    ) -> None:
        if exc_type is not None:
            self._destroy()
            return

        self.save()

    def __del__(self):
        if self.shard:
            _ = lib.shard_destroy(self.shard)

    def _destroy(self) -> None:
        _ = lib.shard_destroy(self.shard)
        self.shard = None

    def create(self) -> None:
        """Initialize the shard.

        Raises:
            RuntimeError: something went wrong while creating the Shard.
        """
        assert self.shard is None, "create() has already been called"

        self.shard = lib.shard_init(self.path.encode("utf-8"))
        ret = lib.shard_create(self.shard, self.object_count)
        if ret == -1:
            raise RuntimeError(f"Something went wrong when creating the Shard at {self.path}")
        self.written_object_count = 0

    def save(self) -> None:
        """Save the Shard.

        Finalize the Shard by creating the perfect hash table
        that will be used to find the content of the objects from
        their key.

        Raises:
            RuntimeError: if the number of written objects does not match ``object_count``,
                or if something went wrong while saving.
        """
        assert self.shard, "create() has not been called"

        if self.object_count != self.written_object_count:
            raise RuntimeError(
                f"Only {self.written_object_count} objects were written "
                f"when {self.object_count} were declared."
            )
        ret = lib.shard_save(self.shard)
        if ret == -1:
            raise RuntimeError("Something went wrong while saving the Shard")
        self._destroy()

    def write(self, key: Key, object: HashObject) -> None:
        """Add the key/object pair to the Read Shard.

        Args:
            key: the unique key associated with the object.
            object: the object

        Raises:
            ValueError: if the key length is wrong, or if enough objects
                have already been written.
            RuntimeError: if something wrong happens when writing the object.
        """
        assert self.shard, "create() has not been called"

        if len(key) != Shard.key_len():
            raise ValueError(f"key length is {len(key)} instead of {Shard.key_len()}")
        if self.written_object_count >= self.object_count:
            raise ValueError("The declared number of objects has already been written")
        ret = lib.shard_object_write(self.shard, key, object, len(object))
        if ret != 0:
            raise RuntimeError("Something went wrong when in `shard_object_write`")
        self.written_object_count += 1


class Shard:
    """Files storing objects indexed with a perfect hash table.

    This class allows creating a Read Shard by adding key/object pairs
    and looking up the content of an object when given the key.

    This class can act as a context manager, like so:

    .. code-block:: python

        with Shard("shard") as shard:
            return shard.lookup(key)
    """

    def __init__(self, path: str):
        """Open an existing Read Shard.

        Args:
            path: path to an existing Read Shard file or device

        """
        self.ffi = FFI()
        self.path = path
        self.shard = lib.shard_init(self.path.encode("utf-8"))
        ret = lib.shard_load(self.shard)
        if ret == -1:
            raise RuntimeError(f"Something went wrong while loading the Shard at {self.path}")

    def __del__(self) -> None:
        if self.shard:
            _ = lib.shard_destroy(self.shard)

    def close(self) -> None:
        assert self.shard, "Shard has been closed already"

        _ = lib.shard_destroy(self.shard)
        self.shard = None

    def __enter__(self) -> "Shard":
        return self

    def __exit__(
        self,
        exc_type: Optional[Type[BaseException]],
        exc_val: Optional[BaseException],
        exc_tb: Optional[TracebackType],
    ) -> None:
        self.close()

    @staticmethod
    def key_len():
        return lib.shard_key_len

    def lookup(self, key: Key) -> HashObject:
        """Fetch the object matching the key in the Read Shard.

        Fetching an object is O(1): one lookup in the index to obtain
        the offset of the object in the Read Shard and one read to get
        the payload.

        Args:
            key: the key associated with the object to retrieve.

        Returns:
           the object as bytes.
        """
        assert self.shard, "Shard has been closed already"

        object_size_pointer = self.ffi.new("uint64_t*")
        ret = lib.shard_lookup_object_size(self.shard, key, object_size_pointer)
        if ret == -1:
            raise RuntimeError("Something went wrong while looking up object size")
        object_size = object_size_pointer[0]
        object_pointer = self.ffi.new("char[]", object_size)
        ret = lib.shard_lookup_object(self.shard, object_pointer, object_size)
        if ret == -1:
            raise RuntimeError("Something went wrong while reading the object data")
        return cast(HashObject, self.ffi.unpack(object_pointer, object_size))
