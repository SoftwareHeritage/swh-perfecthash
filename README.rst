Perfect Hash table for Software Heritage Object Storage
=======================================================

A perfect hash table for software heritage object storage.

Build dependencies
------------------

This packages uses cffi to build the wrapper around the cmph minimal perfect
hashmap library. To build the binary extension, in addition to the python
development tools, you will need cmph, gtest and valgrind. On de Debian
system, you can install these using:

.. code-block:: shell

   sudo apt install build-essential python3-dev libcmph-dev libgtest-dev valgrind lcov

Then you should be able to build the binary extension:

.. code-block:: shell

   python -m build
