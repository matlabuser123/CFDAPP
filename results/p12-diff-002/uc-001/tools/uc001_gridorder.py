#!/usr/bin/env python3
"""P12-DIFF-002-UC-001 Step 6/8: why PoiseuilleValidation.ProductionGridConvergence reports an
observed dp/dx order of 2.2652 when the DIFF-002 discrete system's own dp/dx sequence is
second order.

Nothing here reads CFDApp source. The reference values come from the closed form derived in
uc001_reference.py:

    two-point wall treatment :  dp/dx(ny) = dp/dx_exact *  ny^2 / ( ny^2 + 2)
    DIFF-002 wall treatment  :  dp/dx(ny) = dp/dx_exact * 2ny^2 / (2ny^2 + 1)

The three CFDApp-produced numbers used below are clearly labelled PRODUCTION and are used only
as the subject of the comparison, never as a reference.
"""

from fractions import Fraction
import math

R = Fraction(3, 2)          # grid refinement ratio (ny 8 -> 12 -> 18)
NYS = (8, 12, 18)           # coarse, medium, fine
DPDX_EXACT = Fraction(-6, 5)  # -1.2 = -12 mu U / H^2 with mu = 1/10, U = 1, H = 1

# --- ANALYTICAL REFERENCE (no CFDApp) ---------------------------------------------------------
def dpdx_two_point(ny):
    return DPDX_EXACT * Fraction(ny * ny, ny * ny + 2)


def dpdx_diff002(ny):
    return DPDX_EXACT * Fraction(2 * ny * ny, 2 * ny * ny + 1)


# --- CFDApp PRODUCTION RESULT (results/validation/production/poiseuille.json) ------------------
PRODUCTION_DPDX = {
    8: -1.1897049876603984,
    12: -1.1957414482678574,
    18: -1.1981507887470666,
}
PRODUCTION_ODD_EVEN = {8: 0.041464293258892226, 12: 0.0025581025493615073, 18: 2.971381385385996e-06}


def solve_observed_order(r21, r32, ratio, maximum=8.0):
    """The project's own estimator (src/validation/GridConvergence.cpp): solve
    ratio = (r21^p - 1) / (r32^p - 1) * ... -- for equal ratios r21 == r32 == r this reduces to
    p = ln(ratio) / ln(r). Equal ratios are all this study uses, so use the closed form and
    assert the fixed point."""
    assert abs(r21 - r32) < 1e-15, "this study uses a constant refinement ratio"
    if ratio <= 1.0:
        return None
    return math.log(ratio) / math.log(r21)


def analyse(label, phi_coarse, phi_medium, phi_fine, formal=2.0):
    """Reproduces GridConvergence.cpp: grid 1 = FINEST, epsilon21 = phi2 - phi1,
    epsilon32 = phi3 - phi2, asymptoticRatio = u32 / (r21^pf * u21)."""
    phi1, phi2, phi3 = float(phi_fine), float(phi_medium), float(phi_coarse)
    e21 = phi2 - phi1
    e32 = phi3 - phi2
    r = float(R)
    ratio = e32 / e21
    p = solve_observed_order(r, r, ratio)
    rf = r ** formal
    u21 = abs(e21) / (rf - 1.0)
    u32 = abs(e32) / (rf - 1.0)
    asym = u32 / (rf * u21)
    status = "asymptotic" if abs(asym - 1.0) <= 0.1 else "monotonic_not_asymptotic"
    print(f"  {label:<34} p = {p:7.4f}   asymptotic ratio = {asym:7.4f}   {status}")
    return p, asym, status


print(__doc__)
print("=" * 98)
print("A. ANALYTICAL REFERENCE -- the discrete systems' own dp/dx sequences")
print("=" * 98)
for name, fn in (("two-point (pre-DIFF-002)", dpdx_two_point), ("DIFF-002", dpdx_diff002)):
    print(f"\n  {name}:")
    for ny in NYS:
        v = fn(ny)
        rel = abs(v - DPDX_EXACT) / abs(DPDX_EXACT)
        print(f"    ny={ny:3d}  dp/dx = {str(v):>16} = {float(v):.15f}   rel err vs continuum "
              f"{str(rel):>12} = {float(rel):.6e}")
print()
p_2pt = analyse("two-point reference sequence", dpdx_two_point(8), dpdx_two_point(12),
                dpdx_two_point(18))
p_d2 = analyse("DIFF-002 reference sequence", dpdx_diff002(8), dpdx_diff002(12),
               dpdx_diff002(18))

print()
print("  Both discrete systems are second order in dp/dx: the exact sequences give observed")
print(f"  orders {p_2pt[0]:.4f} and {p_d2[0]:.4f}, both with asymptotic ratio within 1 +/- 0.1.")
print("  => The DIFF-002 OPERATOR is not the cause of the 2.2652 the test reports.")

print()
print("=" * 98)
print("B. CFDApp PRODUCTION RESULT -- the measured dp/dx sequence")
print("=" * 98)
print()
for ny in NYS:
    ref = float(dpdx_diff002(ny))
    prod = PRODUCTION_DPDX[ny]
    off = prod - ref
    oe = PRODUCTION_ODD_EVEN[ny]
    print(f"  ny={ny:3d}  production {prod:.15f}   reference {ref:.15f}")
    print(f"         extraction offset {off:+.6e}   odd-even amplitude {oe:.6e}"
          f"   offset/amplitude {off / oe:+.4f}")
print()
analyse("production measured sequence", PRODUCTION_DPDX[8], PRODUCTION_DPDX[12],
        PRODUCTION_DPDX[18])
print("  (the test reports observed order 2.2652, asymptotic ratio 1.1135 -- reproduced above)")

print()
print("=" * 98)
print("C. ATTRIBUTION -- inject the measured extraction offset into the exact reference")
print("=" * 98)
print()
offsets = {ny: PRODUCTION_DPDX[ny] - float(dpdx_diff002(ny)) for ny in NYS}
analyse("DIFF-002 reference, clean", dpdx_diff002(8), dpdx_diff002(12), dpdx_diff002(18))
analyse("DIFF-002 reference + offsets",
        float(dpdx_diff002(8)) + offsets[8], float(dpdx_diff002(12)) + offsets[12],
        float(dpdx_diff002(18)) + offsets[18])
analyse("two-point reference + same offsets",
        float(dpdx_two_point(8)) + offsets[8], float(dpdx_two_point(12)) + offsets[12],
        float(dpdx_two_point(18)) + offsets[18])
print()
print("  The SAME extraction offsets applied to the two-point sequence leave it asymptotic, and")
print("  applied to the DIFF-002 sequence push it out of the band. The reason is the ratio of")
print("  the offset to the grid-to-grid difference the order estimator divides by:")
print()
for name, fn in (("two-point", dpdx_two_point), ("DIFF-002", dpdx_diff002)):
    e32 = float(fn(8)) - float(fn(12))
    e21 = float(fn(12)) - float(fn(18))
    print(f"    {name:<10}  eps32 (8->12) {e32:+.6e}   eps21 (12->18) {e21:+.6e}")
    print(f"                offset/eps32 {offsets[8] / e32:+.4f}   offset/eps21 "
          f"{offsets[12] / e21:+.4f}")
print()
print("  DIFF-002 shrank the dp/dx discretisation error by 4x (Step 6) without changing the")
print("  pressure-EXTRACTION error, so the same fixed estimator bias is now a 4x larger")
print("  fraction of the differences the Richardson estimator consumes.")
print()
print("=" * 98)
print("D. THE TEST'S DECLARED NOISE FLOOR")
print("=" * 98)
print()
print("  test_poiseuille_production_validation.cpp sets, for BOTH quantities,")
print("      options.absoluteNoise = 1e-5")
print("  via `QuantitySpec gradient = centerline;`. The measured dp/dx extraction offsets are")
for ny in NYS:
    print(f"      ny={ny:3d}: {abs(offsets[ny]):.3e}"
          f"   ({abs(offsets[ny]) / 1e-5:8.1f} x the declared 1e-5)")
print()
print("  The file's own `limitations` section already documents this estimator as biased by the")
print("  odd-even pressure mode. The declared 1e-5 noise floor is therefore wrong for dp/dx by")
print(f"  up to {max(abs(v) for v in offsets.values()) / 1e-5:.0f}x, independently of DIFF-002.")
