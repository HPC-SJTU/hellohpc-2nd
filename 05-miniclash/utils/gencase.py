#!/usr/bin/env python3

import os
import random
import sys

def main():
    if len(sys.argv) not in (3, 4):
        print("usage: gencase.py <count> <dir> [seed]", file=sys.stderr)
        return 2
    count = int(sys.argv[1])
    outdir = sys.argv[2]
    seed = int(sys.argv[3]) if len(sys.argv) == 4 else random.randint(0, 1 << 32)

    rng = random.Random(seed)
    os.makedirs(outdir, exist_ok=True)

    with open(os.path.join(outdir, 'tasks.txt'), 'w') as tasks:
        for i in range(count):
            size = rng.randint(32, 4096)
            name = os.path.join(outdir, 'in%04d.bin' % i)
            with open(name, 'wb') as f:
                f.write(bytes(rng.randrange(256) for _ in range(size)))
            tasks.write("%s %s %s\n" % (
                name,
                os.path.join(outdir, 'out%04d_1.bin' % i),
                os.path.join(outdir, 'out%04d_2.bin' % i)))
    print("wrote %d cases to %s" % (count, outdir))
    return 0


if __name__ == '__main__':
    sys.exit(main())
