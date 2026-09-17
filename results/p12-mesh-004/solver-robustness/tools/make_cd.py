import math, sys
n = int(sys.argv[1]); scale = float(sys.argv[2]); pe = 0.5
rows = []
for i in range(n):
    r = []
    if i > 0: r.append((i - 1, -(1.0 + pe) * scale))
    r.append((i, (2.0 + pe) * scale))
    if i + 1 < n: r.append((i + 1, -1.0 * scale))
    rows.append(r)
nnz = sum(len(r) for r in rows)
out = ["# 1D upwind convection-diffusion n=%d scale %g" % (n, scale), "%d %d" % (n, nnz)]
off = 0; offs = [0]
for r in rows:
    off += len(r); offs.append(off)
out += [str(o) for o in offs]
out += [str(c) for r in rows for c, _ in r]
out += ["%.17g" % v for r in rows for _, v in r]
out += ["%.17g" % (math.sin(0.1 * i) * scale) for i in range(n)]
out += ["0"] * n
open(sys.argv[3], "w").write("\n".join(out) + "\n")
