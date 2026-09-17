"""Search small integer systems for EXACT BiCGSTAB breakdowns (exact rational arithmetic), keeping
only cases whose every intermediate value is dyadic (power-of-two denominator) so IEEE double
arithmetic reproduces the zero exactly. Targets: rho = (rHat, r_k) = 0 with r_k != 0 at k >= 1, and
(t, s) = 0 with t != 0 (omega breakdown), in a NONSINGULAR matrix, x0 = 0, no preconditioner."""
from fractions import Fraction as F
import itertools


def dyadic(q):
    d = q.denominator
    return d & (d - 1) == 0 and abs(q.numerator) < 2**40


def mat_vec(A, x):
    return [sum(a * b for a, b in zip(row, x)) for row in A]


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def det3(A):
    return (A[0][0] * (A[1][1] * A[2][2] - A[1][2] * A[2][1]) - A[0][1] * (A[1][0] * A[2][2] - A[1][2] * A[2][0])
            + A[0][2] * (A[1][0] * A[2][1] - A[1][1] * A[2][0]))


def run(A, b, max_it=3):
    n = len(b)
    x = [F(0)] * n
    r = list(b)
    rh = list(r)
    rho_old = alpha = omega = F(1)
    v = [F(0)] * n
    p = [F(0)] * n
    for it in range(1, max_it + 1):
        rho = dot(rh, r)
        if rho == 0:
            return ("rho", it) if any(r) else None
        beta = (rho / rho_old) * (alpha / omega)
        p = [ri + (pi - vi * omega) * beta for ri, pi, vi in zip(r, p, v)]
        v = mat_vec(A, p)
        den = dot(rh, v)
        if den == 0:
            return ("rhat.v", it)
        alpha = rho / den
        s = [ri - vi * alpha for ri, vi in zip(r, v)]
        if not any(s):
            return None
        t = mat_vec(A, s)
        ts = dot(t, s)
        if ts == 0:
            return ("omega", it) if any(t) else None
        omega = ts / dot(t, t)
        for q in [rho, beta, den, alpha, ts, omega] + p + v + s + t:
            if not dyadic(q):
                return "nondyadic"
        x = [xi + pi * alpha + si * omega for xi, pi, si in zip(x, p, s)]
        r = [si - ti * omega for si, ti in zip(s, t)]
        if not any(r):
            return None
        rho_old = rho
    return None


found = {}
vals = [-2, -1, 0, 1, 2]
for entries in itertools.product(vals, repeat=9):
    A = [[F(entries[3 * i + j]) for j in range(3)] for i in range(3)]
    if det3(A) == 0:
        continue
    for b in ([F(1), F(0), F(0)], [F(1), F(1), F(0)], [F(1), F(0), F(1)], [F(1), F(1), F(1)]):
        out = run(A, b)
        if isinstance(out, tuple) and out[1] >= 1:
            key = out
            if key not in found and not (out[0] == "rhat.v" and out[1] == 1):
                found[key] = (entries, [int(q) for q in b])
                print(out, "A =", [list(entries[i * 3:i * 3 + 3]) for i in range(3)], "b =", [int(q) for q in b],
                      "det =", det3(A))
    if len(found) >= 4:
        break
