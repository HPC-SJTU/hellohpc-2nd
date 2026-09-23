#!/usr/bin/env bash
set -euo pipefail

module load hpckit/26.1.RC1
module load gcc/compiler12.3.1/gccmodule

setup_file=${1:?usage: run.sh <setup-file>}

exec ./src/system full_analyze "$setup_file" 15
