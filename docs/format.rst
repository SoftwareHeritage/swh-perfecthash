Read Shard format
=================

The Read Shard has the following structure:

* bytes [0, SHARD_OFFSET_MAGIC[: The shard magic
* bytes [SHARD_OFFSET_MAGIC, objects_position[: The header shard_header_t
* bytes [objects_position, index_position[: `objects_count` times the size of the object (u_int64_t) followed by the content of the object
* bytes [index_position, hash_position[: An array of u_int64_t object positions in the range [objects_position, index_position[. The size of the array is provided by cmph_size after building the hash function.
* bytes [hash_position, ...[: The hash function, as written by cmph_dump.
