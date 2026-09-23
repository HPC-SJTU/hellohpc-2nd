#!/usr/bin/env bash
set -euo pipefail

module load hpckit/26.1.RC1
module load gcc/compiler12.3.1/gccmodule
export CPATH="/vault/public/xflops/boost/include${CPATH:+:$CPATH}"
export LIBRARY_PATH="/vault/public/xflops/boost/lib${LIBRARY_PATH:+:$LIBRARY_PATH}"

make -C src -j "$(nproc)"
