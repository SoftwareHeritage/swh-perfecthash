#!/usr/bin/env bash

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
