#!/usr/bin/env bash
set -euo pipefail

module load hpckit/26.1.RC1
module load gcc/compiler12.3.1/gccmodule

source_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
for component in checker interactor; do
  env -u LD_LIBRARY_PATH cmake -S "$source_root/$component" -B "$source_root/$component/build" \
    -G 'Unix Makefiles' -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
    -DCMAKE_CXX_COMPILER=g++ \
    -DCMAKE_EXE_LINKER_FLAGS='-static-libstdc++ -static-libgcc'
  env -u LD_LIBRARY_PATH cmake --build "$source_root/$component/build" -j "$(nproc)"
done
