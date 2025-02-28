Shard File Format for the Software Heritage Object Storage
==========================================================

This module implement the support and tooling to manipulate SWH Shard files
based on a perfect hash table, typically used by the software heritage object
storage.

It is both a Python extension that can be used as a library to manuipulate SWH
shard files, and a set of command line tools.

Quick Start
-----------

This packages uses pybind11 to build the wrapper around the cmph minimal perfect
hashmap library. To build the binary extension, in addition to the python
development tools, you will need cmph, gtest and valgrind. On de Debian
system, you can install these using:

.. code-block:: shell

   sudo apt install build-essential python3-dev libcmph-dev libgtest-dev valgrind lcov


Command Line Tool
~~~~~~~~~~~~~~~~~

You may use several methods to install swh-shard, e.g. using `uv`_ or `pip`_.

For example:

.. code-block:: console

   $ uv tool install swh-shard
   [...]
   Installed 1 executable: swh-shard

   $ swh-shard
   Usage: swh-shard [OPTIONS] COMMAND [ARGS]...

     Software Heritage Shard tools.

   Options:
     -C, --config-file FILE  Configuration file.
     -h, --help              Show this message and exit.

   Commands:
     create  Create a shard file from given files
     get     List objects in a shard file
     info    Display shard file information
     ls      List objects in a shard file

Then you can create a shard file from local files:

.. code-block:: console

   $ swh-shard create volume.shard *.py
   There are 3 entries
   Checking files to add  [####################################]  100%
   after deduplication: 3 entries
   Adding files to the shard  [####################################]  100%
   Done

This will use the sha256 checksum of each file content given as argument as key
in the shard file.

Then you can check the header of the shard file:

.. code-block:: console

   $ swh-shard info volume.shard
   Shard volume.shard
   ├─version:    1
   ├─objects:    3
   │ ├─position: 512
   │ └─size:     5633
   ├─index
   │ ├─position: 6145
   │ └─size:     440
   └─hash
     └─position: 6585

List the content of a shard:

.. code-block:: console

   $ swh-shard ls volume.shard
   8bb71bce4885c526bb4114295f5b2b9a23a50e4a8d554c17418d1874b1a233ac: 834 bytes
   06340a7a5fa9e18d72a587a69e4dc7e79f4d6a56632ea6900c22575dc207b07f: 4210 bytes
   d39790a3af51286d2d10d73e72e2447cf97b149ff2d8e275b200a1ee33e4a3c5: 565 bytes

Retrieve an object from a shard:

.. code-block:: console

   $ swh-shard get volume.shard 06340a7a5fa9e18d72a587a69e4dc7e79f4d6a56632ea6900c22575dc207b07f | sha256sum
   06340a7a5fa9e18d72a587a69e4dc7e79f4d6a56632ea6900c22575dc207b07f  -

And delete one or more objects from a shard:

.. code-block:: console

   $ swh-shard delete volume.shard 06340a7a5fa9e18d72a587a69e4dc7e79f4d6a56632ea6900c22575dc207b07f
   About to remove these objects from the shard file misc/volume.shard
   06340a7a5fa9e18d72a587a69e4dc7e79f4d6a56632ea6900c22575dc207b07f (4210 bytes)
   Proceed? [y/N]: y
   Deleting objects from the shard  [####################################]  100%
   Done


.. _`uv`: https://docs.astral.sh/uv/
.. _`pip`: https://pip.pypa.io/
