#!/usr/bin/env python3
"""P12-DIFF-002-UC-001: the centreline-sampling parity artifact, and the exact per-grid
reference constants the migrated gate needs.

ANALYTICAL REFERENCE ONLY -- derived here, not read from CFDApp. CFDApp numbers appear once, at
the end, clearly labelled, as the subject of the comparison.

DIFF-002's discrete fully-developed solution equals the continuum parabola AT THE CELL CENTRES:

    u_j = -(G/2) y_j (H - y_j),   y_j = (j + 1/2) h,   h = H/ny,   G = (dp/dx)/mu
    dp/dx = dp/dx_exact * 2ny^2 / (2ny^2 + 1)

The tests do not read a cell value at y = H/2; they read
`interpolateProfile(profile, H/2)`, a LINEAR interpolation of the cell-centre samples (with
u = 0 anchors at both walls). That sampling has a different error constant depending on the
parity of ny:

  * ny ODD  -- (ny-1)/2 + 1/2 = ny/2 means y = H/2 IS a cell centre, so the sample is the
    discrete solution itself:
        u_c = -(G/2)(H/2)(H/2) = -G H^2/8
    and, with 1.5 U = -G_exact H^2/8,
        1.5 - u_c = 1.5 * (1 - 2ny^2/(2ny^2+1)) = 1.5 / (2ny^2 + 1).

  * ny EVEN -- y = H/2 falls exactly midway between the centres y = H/2 -+ h/2, so the sample is
    the CHORD midpoint of the parabola, which undershoots the parabola's apex by (G/2)(h/2)^2:
        u_c = -G H^2/8 + (G/2)(h/2)^2 = -G H^2/8 + G H^2/(8 ny^2)
    Writing it as a deficit from the continuum apex and using G = G_exact * 2ny^2/(2ny^2+1):
        1.5 - u_c = 1.5/(2ny^2+1) + 1.5 * (2/(2ny^2+1) ... )  -- evaluated exactly below.

So the two parities have DIFFERENT error constants. A three-grid order study whose ny values do
not all share a parity is therefore not a clean h-refinement sequence for the centreline, and its
observed order is meaningless.
"""

from fractions import Fraction
import math

H = Fraction(1)
MU = Fraction(1, 10)
U = Fraction(1)
DPDX_EXACT = Fraction(-6, 5)          # -12 mu U / H^2
APEX_EXACT = Fraction(3, 2)           # 1.5 U


def dpdx(ny):
    return DPDX_EXACT * Fraction(2 * ny * ny, 2 * ny * ny + 1)


def g(ny):
    return dpdx(ny) / MU


def cell_centre_u(ny, j):
    """The discrete solution at cell centre j (exact rational)."""
    y = Fraction(2 * j + 1, 2 * ny) * H
    return -g(ny) / 2 * y * (H - y)


def sampled_centreline(ny):
    """What interpolateProfile(profile, H/2) returns, by parity."""
    if ny % 2 == 1:
        j = (ny - 1) // 2
        assert Fraction(2 * j + 1, 2 * ny) == Fraction(1, 2), "odd ny: H/2 is a cell centre"
        return cell_centre_u(ny, j)
    lo = ny // 2 - 1
    hi = ny // 2
    # y_lo = H/2 - h/2 and y_hi = H/2 + h/2 are equidistant from H/2, so linear interpolation is
    # the plain average.
    assert Fraction(2 * lo + 1, 2 * ny) + Fraction(2 * hi + 1, 2 * ny) == 1
    return (cell_centre_u(ny, lo) + cell_centre_u(ny, hi)) / 2


print(__doc__)
print("=" * 100)
print("A. EXACT SAMPLED CENTRELINE AND ITS DEFICIT FROM THE CONTINUUM APEX 1.5")
print("=" * 100)
print()
print(f"  {'ny':>4} {'par':>4}  {'sampled u_c (exact)':>22} {'u_c':>18}   "
      f"{'1.5 - u_c (exact)':>20} {'value':>13}")
for ny in (8, 12, 16, 18, 24, 27, 32, 36):
    uc = sampled_centreline(ny)
    deficit = APEX_EXACT - uc
    par = "even" if ny % 2 == 0 else "odd"
    print(f"  {ny:>4} {par:>4}  {str(uc):>22} {float(uc):18.15f}   {str(deficit):>20} "
          f"{float(deficit):13.6e}")

print()
print("  Closed forms for the deficit, verified as exact rationals below:")
print("     ny odd  :  1.5 / (2ny^2 + 1)")
print("     ny even :  4.5 / (2ny^2 + 1)      <-- exactly 3x the odd constant")
print()
for ny in (8, 12, 16, 18, 24, 27, 32, 36):
    deficit = APEX_EXACT - sampled_centreline(ny)
    predicted = (Fraction(3, 2) if ny % 2 else Fraction(9, 2)) / (2 * ny * ny + 1)
    assert deficit == predicted, (ny, deficit, predicted)
    print(f"    ny={ny:3d} ({'even' if ny % 2 == 0 else 'odd '}): deficit {str(deficit):>14} "
          f"== predicted {str(predicted):>14}  OK")
print()
print("  => The even-ny centreline deficit is EXACTLY 3x the odd-ny one at the same ny.")
print("     Mixing parities inside a refinement triplet injects a factor-3 jump that the")
print("     Richardson estimator reads as a change of order.")


def order(label, coarse, medium, fine, r=Fraction(3, 2), formal=2.0):
    phi1, phi2, phi3 = float(fine), float(medium), float(coarse)
    e21 = phi2 - phi1
    e32 = phi3 - phi2
    ratio = e32 / e21
    if ratio <= 1.0:
        print(f"  {label:<44} eps32/eps21 = {ratio:.4f} <= 1: no positive order")
        return
    p = math.log(ratio) / math.log(float(r))
    rf = float(r) ** formal
    u21, u32 = abs(e21) / (rf - 1.0), abs(e32) / (rf - 1.0)
    asym = u32 / (rf * u21)
    status = "asymptotic" if abs(asym - 1.0) <= 0.1 else "monotonic_not_asymptotic"
    print(f"  {label:<44} p = {p:7.4f}   asymptotic ratio = {asym:7.4f}   {status}")


print()
print("=" * 100)
print("B. CENTRELINE ORDER OF THE EXACT REFERENCE, BY TRIPLET PARITY")
print("=" * 100)
print()
for trip in ((8, 12, 18), (16, 24, 36), (12, 18, 27)):
    pars = {n % 2 for n in trip}
    tag = "all even" if pars == {0} else ("all odd" if pars == {1} else "MIXED PARITY")
    order(f"{trip}  [{tag}]", *[sampled_centreline(n) for n in trip])
print()
print("  The mixed triplet (12, 18, 27) is corrupted purely by the sampling parity -- these are")
print("  EXACT reference values, with no solver, no iteration and no round-off involved.")

print()
print("=" * 100)
print("C. CFDApp PRODUCTION RESULT vs the exact reference (subject of the comparison)")
print("=" * 100)
print()
PRODUCTION_CENTRELINE = {
    8: 1.465119668846815,
    12: 1.484429036247831,
    18: 1.493066251845519,
    27: 1.498971906785702,
}
for ny, prod in PRODUCTION_CENTRELINE.items():
    ref = float(sampled_centreline(ny))
    print(f"  ny={ny:3d} ({'even' if ny % 2 == 0 else 'odd '})  production {prod:.15f}   "
          f"reference {ref:.15f}   |diff| {abs(prod - ref):.3e}")
print()
print("  Production tracks the exact reference to <= 4.1e-06 on every grid INCLUDING the odd one,")
print("  so the 0.9376 order the 12/18/27 triplet produces is reproduced by the reference itself")
print("  and is a property of the SAMPLING, not of CFDApp.")
print()
print("=" * 100)
print("D. EXACT CONSTANTS FOR THE MIGRATED GATE (derived from ny alone)")
print("=" * 100)
print()
print(f"  {'ny':>4}  {'dp/dx (exact)':>16} {'dp/dx':>18}  {'rel err vs continuum':>22}  "
      f"{'drop ratio':>12}")
for ny in (8, 12, 18, 27):
    d = dpdx(ny)
    rel = abs(d - DPDX_EXACT) / abs(DPDX_EXACT)
    ratio = Fraction(2 * ny * ny, 2 * ny * ny + 1)
    assert rel == Fraction(1, 2 * ny * ny + 1), "rel err must be exactly 1/(2ny^2+1)"
    print(f"  {ny:>4}  {str(d):>16} {float(d):18.15f}  {str(rel):>10} = {float(rel):.6e}  "
          f"{str(ratio):>12}")
print()
print("  dp/dx relative error vs the continuum is EXACTLY 1/(2ny^2 + 1); the pressure-drop ratio")
print("  over any fully-developed segment is EXACTLY 2ny^2/(2ny^2 + 1).")
print("  (The superseded two-point forms were 2/(ny^2+2) and ny^2/(ny^2+2).)")
