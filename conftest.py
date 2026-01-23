# Copyright (C) 2021-2022  The Software Heritage developers
# See the AUTHORS file at the top-level directory of this distribution
# License: GNU General Public License version 3, or any later version
# See top-level LICENSE file for more information


def pytest_addoption(parser):
    parser.addoption(
        "--shard-size",
        default=10,
        type=int,
        help="Size of the Read Shard file in MB",
    )
    parser.addoption(
        "--shard-path",
        default="/tmp/payload",
        help="Path of the Read Shard file",
    )
    parser.addoption(
        "--shard-count",
        default=2,
        type=int,
        help="Number of Read Shard files for lookup tests",
    )
    parser.addoption(
        "--object-max-size",
        default=10 * 1024,
        type=int,
        help="Maximum size of an object in bytes",
    )
    parser.addoption(
        "--lookups",
        default=10,
        type=int,
        help="Number of lookups to perform",
    )
