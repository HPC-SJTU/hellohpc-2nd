#!/usr/bin/env python3

import hashlib
import sys

def check(infn, out1, out2):
    with open(infn, 'rb') as f:
        prefix = f.read()
    with open(out1, 'rb') as f:
        a = f.read()
    with open(out2, 'rb') as f:
        b = f.read()

    if a == b:
        return "outputs identical"
    if not a.startswith(prefix):
        return "%s is not prefixed by %s" % (out1, infn)
    if not b.startswith(prefix):
        return "%s is not prefixed by %s" % (out2, infn)
    ha = hashlib.md5(a).hexdigest()
    hb = hashlib.md5(b).hexdigest()
    if ha != hb:
        return "md5 mismatch %s != %s" % (ha, hb)
    return None


def main():
    if len(sys.argv) != 2:
        print("usage: verify.py <tasks.txt>", file=sys.stderr)
        return 2

    failures = 0
    total = 0
    with open(sys.argv[1]) as f:
        for lineno, line in enumerate(f, 1):
            parts = line.split()
            if not parts:
                continue
            total += 1
            if len(parts) != 3:
                print("line %d: malformed" % lineno)
                failures += 1
                continue
            try:
                err = check(*parts)
            except OSError as e:
                err = str(e)
            if err:
                print("line %d (%s): %s" % (lineno, parts[0], err))
                failures += 1

    print("%d/%d collisions valid" % (total - failures, total))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
