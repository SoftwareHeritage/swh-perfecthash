#!/usr/bin/env bash
# Copyright (C) 2021-2025  The Software Heritage developers
# See the AUTHORS file at the top-level directory of this distribution
# License: GNU General Public License version 3, or any later version
# See top-level LICENSE file for more information
#
# This script is used by cibuildwheel to install and compile the cmph library
# when building manylinux wheels

set -e

CMPH_VERSION=2.0.2
PREFIX="$(readlink -f $(dirname $0))/cmph"

rm -rf "$PREFIX"
mkdir "$PREFIX"
cd "$PREFIX"
wget https://deac-ams.dl.sourceforge.net/project/cmph/v${CMPH_VERSION}/cmph-${CMPH_VERSION}.tar.gz -O cmph.tar.gz
tar xf cmph.tar.gz

cd cmph-${CMPH_VERSION}

./configure --prefix="$PREFIX"
make -j8
make install
