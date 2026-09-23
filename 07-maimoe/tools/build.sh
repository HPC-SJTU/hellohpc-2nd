#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target maimoe maimoe_kernel maimoe_check -j
echo "build ok: build/maimoe build/maimoe_kernel build/maimoe_check"
