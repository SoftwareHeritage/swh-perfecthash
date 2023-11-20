# Copyright (C) 2021-2022  The Software Heritage developers
# See the AUTHORS file at the top-level directory of this distribution
# License: GNU General Public License version 3, or any later version
# See top-level LICENSE file for more information

import os
from types import TracebackType
from typing import NewType, Optional, Type, cast

from cffi import FFI

from swh.perfecthash._hash_cffi import lib

Key = NewType("Key", bytes)


class ShardCreator:
    def __init__(self, path: str, object_count: int):
        """Create a Shard.

        The file at ``path`` will be truncated if it already exists.

        ``object_count`` must match the number of objects that will be added
        using the :meth:`write` method. A ``RuntimeError`` will be raised
        on :meth:`finalize` in case of inconsistencies.

        Ideally this should be done using a ``with`` statement, as such:

        .. code-block:: python

            with ShardCreator("shard", len(objects)) as shard:
                for key, object in objects.items():
                    shard.write(key, object)

        Otherwise, :meth:`prepare`, :meth:`write` and :meth:`finalize` must be
        called in sequence.

        Args:
            path: path to the Shard file or device that will be written.
            object_count: number of objects that will be written to the Shard.
        """

        self.ffi = FFI()
        self.path = path
        self.object_count = object_count
        self.shard = None

    def __enter__(self) -> "ShardCreator":
        self.prepare()
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

        self.finalize()

    def __del__(self):
        if self.shard:
            _ = lib.shard_destroy(self.shard)

    def _destroy(self) -> None:
        _ = lib.shard_destroy(self.shard)
        self.shard = None

    def prepare(self) -> None:
        """Initialize the shard.

        Raises:
            RuntimeError: something went wrong while creating the Shard.
        """
        assert self.shard is None, "prepare() has already been called"

        self.shard = lib.shard_init(self.path.encode("utf-8"))

        self.ffi.errno = 0
        ret = lib.shard_prepare(self.shard, self.object_count)
        if ret != 0:
            raise OSError(self.ffi.errno, os.strerror(self.ffi.errno), self.path)
        self.written_object_count = 0

    def finalize(self) -> None:
        """Finalize the Shard.

        Write the index and the perfect hash table
        that will be used to find the content of the objects from
        their key.

        Raises:
            RuntimeError: if the number of written objects does not match ``object_count``,
                or if something went wrong while saving.
        """
        assert self.shard, "prepare() has not been called"

        if self.object_count != self.written_object_count:
            raise RuntimeError(
                f"Only {self.written_object_count} objects were written "
                f"when {self.object_count} were declared."
            )

        self.ffi.errno = 0
        ret = lib.shard_finalize(self.shard)
        if ret != 0:
            errno = self.ffi.errno
            if errno == 0:
                raise RuntimeError(
                    "shard_finalize failed. Was there a duplicate key by any chance?"
                )
            else:
                raise OSError(self.ffi.errno, os.strerror(errno), self.path)
        self._destroy()

    def write(self, key: Key, object: bytes) -> None:
        """Add the key/object pair to the Read Shard.

        Args:
            key: the unique key associated with the object.
            object: the object

        Raises:
            ValueError: if the key length is wrong, or if enough objects
                have already been written.
            RuntimeError: if something wrong happens when writing the object.
        """
        assert self.shard, "prepare() has not been called"

        if len(key) != Shard.key_len():
            raise ValueError(f"key length is {len(key)} instead of {Shard.key_len()}")
        if self.written_object_count >= self.object_count:
            raise ValueError("The declared number of objects has already been written")

        self.ffi.errno = 0
        ret = lib.shard_object_write(self.shard, key, object, len(object))
        if ret != 0:
            raise OSError(self.ffi.errno, os.strerror(self.ffi.errno), self.path)
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

        self.ffi.errno = 0
        ret = lib.shard_load(self.shard)
        if ret != 0:
            raise OSError(self.ffi.errno, os.strerror(self.ffi.errno), self.path)

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

    def lookup(self, key: Key) -> bytes:
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

        if len(key) != Shard.key_len():
            raise ValueError(f"key length is {len(key)} instead of {Shard.key_len()}")

        self.ffi.errno = 0
        object_size_pointer = self.ffi.new("uint64_t*")
        ret = lib.shard_find_object(self.shard, key, object_size_pointer)
        if ret != 0:
            errno = self.ffi.errno
            if errno == 0:
                raise RuntimeError(
                    f"shard_find_object failed. Mismatching key for {key.hex()} in the index?"
                )
            else:
                raise OSError(self.ffi.errno, os.strerror(self.ffi.errno), self.path)
        object_size = object_size_pointer[0]
        object_pointer = self.ffi.new("char[]", object_size)
        self.ffi.errno = 0
        ret = lib.shard_read_object(self.shard, object_pointer, object_size)
        if ret != 0:
            errno = self.ffi.errno
            if errno == 0:
                raise RuntimeError(
                    f"shard_read_object failed. " f"{self.path} might be corrupted."
                )
            else:
                raise OSError(errno, os.strerror(errno), self.path)
        return cast(bytes, self.ffi.unpack(object_pointer, object_size))
