Read Shard format
=================

The Read Shard has the following structure:

* bytes \[0, ``SHARD_OFFSET_MAGIC``\[: The shard magic
* bytes \[``SHARD_OFFSET_MAGIC``, ``objects_position``\[: The header (``shard_header_t``)
* bytes \[``objects_position``, ``index_position``\[: ``objects_count`` times the size of the object (``u_int64_t``) followed by the content of the object
* bytes \[``index_position``, ``hash_position``\[: An array of index entries. The size of the array is provided by ``cmph_size`` after building the hash function. An index entry is made of the key (of ``SHARD_KEY_LEN`` bytes) and the object position (``u_int64_t``) in the range \[``objects_position``, ``index_position``\[. If the object position is ``UINT64_MAX``, this means the object has been deleted.
* bytes \[``hash_position``, ...\[: The hash function, as written by ``cmph_dump``
