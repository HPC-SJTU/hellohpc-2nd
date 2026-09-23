#!/usr/bin/env bash

set -u

if [ "$#" -ne 1 ]; then
	echo "usage: $0 <tasks.txt>" >&2
	exit 2
fi

TASKS=$1
BIN=$(dirname "$0")/md5fastcoll

if [ ! -r "$TASKS" ]; then
	echo "$0: cannot read task file: $TASKS" >&2
	exit 1
fi

if [ ! -x "$BIN" ]; then
	echo "$0: $BIN not found or not executable; run 'make all' first" >&2
	exit 1
fi

status=0
n=0

# Fields are space-separated: <input_file> <output_file1> <output_file2>.
while read -r infile out1 out2 rest; do
	# Skip blank lines.
	[ -n "${infile:-}" ] || continue

	if [ -z "${out2:-}" ] || [ -n "${rest:-}" ]; then
		echo "$0: malformed line: $infile ${out1:-} ${out2:-} ${rest:-}" >&2
		status=1
		continue
	fi

	# -q quiets the per-collision banner, -p sets the prefix file (which is also
	# copied into both outputs), -o names the two outputs and must come last.
	if ! "$BIN" -q -p "$infile" -o "$out1" "$out2" >/dev/null; then
		echo "$0: failed on $infile" >&2
		status=1
		continue
	fi

	n=$((n + 1))
done < "$TASKS"

echo "$0: generated $n collisions"
exit "$status"
