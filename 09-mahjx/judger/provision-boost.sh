#!/usr/bin/env bash
set -euo pipefail

module load hpckit/26.1.RC1
module load gcc/compiler12.3.1/gccmodule

prefix=/vault/public/xflops/boost
if [[ -e "$prefix" ]]; then
  printf 'Toolchain already exists: %s\n' "$prefix" >&2
  exit 1
fi
work=$(mktemp -d "${TMPDIR:-/tmp}/mahjx-boost-XXXXXX")
trap 'rm -rf "$work"' EXIT
curl --fail --location --output "$work/boost.tar.bz2" \
  https://archives.boost.io/release/1.84.0/source/boost_1_84_0.tar.bz2
printf '%s  %s\n' \
  cc4b893acf645c9d4b698e9a0f08ca8846aa5d6c68275c14c3e7949c24109454 \
  "$work/boost.tar.bz2" | sha256sum --check -
tar -xjf "$work/boost.tar.bz2" -C "$work"
cd "$work/boost_1_84_0"
./bootstrap.sh --with-toolset=gcc --with-libraries=system --prefix="$prefix"
./b2 -j "$(nproc)" variant=release link=static threading=multi install
chmod -R a+rX,go-w "$prefix"
printf 'Installed Boost 1.84.0: %s\n' "$prefix"
