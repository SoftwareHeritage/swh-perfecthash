Read Shard format
=================

The Read Shard has the following structure:

* bytes \[0, ``SHARD_OFFSET_MAGIC``\[: The shard magic
* bytes \[``SHARD_OFFSET_MAGIC``, ``objects_position``\[: The header (``shard_header_t``)
* bytes \[``objects_position``, ``index_position``\[: ``objects_count`` times the size of the object (``u_int64_t``) followed by the content of the object
* bytes \[``index_position``, ``hash_position``\[: An array of index entries. The size of the array is provided by ``cmph_size`` after building the hash function. An index entry is made of the key (of ``SHARD_KEY_LEN`` bytes) and the object position (``u_int64_t``) in the range \[``objects_position``, ``index_position``\[. If the object position is ``UINT64_MAX``, this means the object has been deleted.
* bytes \[``hash_position``, ...\[: The hash function, as written by ``cmph_dump``

In more details:

+--------------------------+------+----------------------------+
| Section                  | pos  | description (length)       |
+==========================+======+============================+
| **SHARD_MAGIC**          | 0    | SHARD_OFFSET_MAGIC (32)    |
+--------------------------+------+----------------------------+
| **header**               | 32   | Header (56)                |
+--------------------------+------+----------------------------+
| ``version``              |      | uint64_t (8)               |
+--------------------------+------+----------------------------+
| ``objects_count``        |      | uint64_t (8)               |
+--------------------------+------+----------------------------+
| ``objects_position`` <op>|      | uint64_t (8)               |
+--------------------------+------+----------------------------+
| ``objects_size``         |      | uint64_t (8)               |
+--------------------------+------+----------------------------+
| ``index_position`` <ip>  |      | uint64_t (8)               |
+--------------------------+------+----------------------------+
| ``index_size``           |      | uint64_t (8)               |
+--------------------------+------+----------------------------+
| ``hash_position`` <hp>   |      | uint64_t (8)               |
+--------------------------+------+----------------------------+
| **Objects**              | <op> |                            |
+--------------------------+------+----------------------------+
| ``object0 size``         |      | uint64_t (8)               |
+--------------------------+------+----------------------------+
| ``object0 data``         |      | bytes (<object0 size>)     |
+--------------------------+------+----------------------------+
| ``object1 size``         |      | uint64_t (8)               |
+--------------------------+------+----------------------------+
| ``object1 data``         |      | bytes (<object1 size>      |
+--------------------------+------+----------------------------+
|   ...                    |      |                            |
+--------------------------+------+----------------------------+
| **Index**                | <ip> |                            |
+--------------------------+------+----------------------------+
| ``object0 key``          |      | SHARD_KEY_LEN (32)         |
+--------------------------+------+----------------------------+
| ``object0 offset``       |      | uint64_t (8)               |
+--------------------------+------+----------------------------+
|   ...                    |      |                            |
+--------------------------+------+----------------------------+
| **Hash map**             | <hp> |                            |
+--------------------------+------+----------------------------+
| ``hash function``        |      | <as written by cmph_dump>  |
+--------------------------+------+----------------------------+


``SHARD_MAGIC`` is the constant ``SWHShard`` (with ``\x00`` padding to 32
characters).

Index entries for deleted content are using the special value
``{key=\x00...\x00, offset=2**64-1}``.
