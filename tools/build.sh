#!/bin/bash
# Builds and installs tde-tmatrix inside the tools/ Docker image:
#   docker build -t tde-tmatrix-build tools/
#   docker run --rm -v "$PWD":/src tde-tmatrix-build /src/tools/build.sh
set -e
mkdir -p /tmp/build && cd /tmp/build
cmake "${SRC:-/src}" -DCMAKE_INSTALL_PREFIX=/opt/trinity -DCMAKE_CXX_FLAGS=-Wall \
      -DBUILD_DOC=${BUILD_DOC:-ON} -DBUILD_TESTS=${BUILD_TESTS:-ON} >/dev/null
make -j"$(nproc)" 2>&1 | grep -E "error|warning|Error" -A6 || true
make install >/dev/null 2>&1 || { echo "BUILD FAILED"; exit 1; }
echo "build ok"
