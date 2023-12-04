#!/usr/bin/env python3
# Copyright (C) 2021-2023  The Software Heritage developers
# See the AUTHORS file at the top-level directory of this distribution
# License: GNU General Public License version 3, or any later version
# See top-level LICENSE file for more information

from setuptools import setup

setup(
    cffi_modules=["swh/perfecthash/build.py:ffibuilder"],
)
