# P17.8 AMENDMENT — criterion A6 is NOT met as written, and this records why

`acceptance_gate.md` is **unchanged** (sha256
`a88738e481b3964d2a962e9eb83d7dda92c0d9142f997547673dc3cddabe615e`). Its §15 sends post-freeze
corrections here. A6 stays on the page exactly as frozen.

---

## 1. The criterion, and the fact

```text
A6  every P17.1-P17.7 production source UNTOUCHED        threshold: 0 TOUCHED
```

```text
include/cfd/compressible/HllcFlux.hpp
  at the P17.7 close   98885f49b987d1ec09b93492aa12231277296b1cb6f34bb4a1e92cf08b5c0cdd
  now                  fc4a8db5f98568923ffe320b6adb3b6574f855cf538bb238ba9385bd046b61f0
```

**A6 as literally written is NOT met.** One P17.7 production source changed. Reporting "0 TOUCHED"
by redefining the file as out of scope, or by quietly folding it into the allowed set, would be
exactly the reinterpretation this project's gate protocol exists to stop. So it is reported on its
own line in `tools/structural.sh`, counted neither as untouched nor as a silent failure, and
explained here.

## 2. What changed, and why it is not a P17.8 convenience

Two separate edits, in this order:

```text
1  THE SIGN CORRECTION  (made while P17.8's inviscid-wall test was failing)
   rhoE*_K = rho*_K [ rhoE_K/rho_K + (S* - u_nK)(S* MINUS p_K/(rho_K(S_K - u_nK))) ]   WRONG
   rhoE*_K = rho*_K [ rhoE_K/rho_K + (S* - u_nK)(S* PLUS  p_K/(rho_K(S_K - u_nK))) ]   CORRECT
   Derived from the energy Rankine-Hugoniot condition across S_K, written out in
   results/p17/hllc/AMENDMENT_star_energy_sign.md §2.

2  CLANG-FORMAT  (this session)
   The sign correction's explanatory comment exceeded the column limit, so
   `clang-format-18 --dry-run --Werror` failed on lines 348-352. Formatted, and PROVEN
   formatting-only: token multiset identical (2844) and string-literal content identical
   (38 chars). logs/13_format_verified.log.
```

Edit 1 is a **correctness defect in shipped P17.7 code**, not a P17.8 preference. It was found
because P17.8 built the first configuration in the project's history where `S* = 0` while
`u_n != 0` — an inviscid wall — which is the only cheap place the erroneous term is not multiplied
by zero. P17.7 had no boundary conditions at all, so the defect lived exactly in the gap between the
two phases' scopes.

## 3. Why leaving it alone was not an option

```text
* P17.8's own criterion (§15 of the authorization, "mass flux = 0, energy flux = 0, pressure force
  retained") CANNOT be met with the wrong sign: the wall energy flux was 2.645e+07 W/m^2 against a
  derived bound near round-off.
* G1 closed-domain conservation ALSO failed with the wrong sign. The energy in a sealed box was not
  conserved.
* Building P17.8 on top of it and reporting the failures as "the scheme's own error" is precisely
  what P17.7 did with its strong-discontinuity rows, and is the mistake its amendment §4 records.
```

After the correction: wall energy flux `7.45e-09 W/m²` (a factor of `3.5e+15`), and closed-domain
conservation `8.310e-16` mass / `1.696e-16` energy relative over 37 steps.

## 4. What was done to keep the change accountable

```text
1  the derivation written out, from Rankine-Hugoniot, in the P17.7 amendment -- not "the sign
   looked wrong"
2  an INDEPENDENT numerical check at 50 digits with no CFDApp code
   (density_based_solver/tools/check_star_energy_sign.py)
3  the DEFINING PROPERTY  F*_K,energy == S*(rhoE*_K + p*)  asserted, which neither the C++ nor any
   reference can satisfy by accident. P17.8's own R2 reference script REFUSES to emit a value unless
   it holds, so the property is enforced on every run and not only once.
4  P17.7's frozen independent reference (exact_riemann.py) corrected too, and
   tests/unit/compressible/hllc_reference_data.hpp REGENERATED from it -- the reference carried the
   same error, which is why P17.7's X2 criterion agreed to 3.25e-16 while both were wrong
5  P17.7's full suite re-run: 35/35 CPU + 4/4 GPU still pass, X1 and X2 unchanged at 3.254e-16
6  a PERMANENT test so it cannot regress: DensityBasedConservationTest.InviscidWallCarriesNoMassOrEnergy
7  this file, so A6's literal failure is on the record rather than absorbed
```

## 5. What is NOT claimed

```text
* NOT claimed: that A6 passed. It did not. One upstream file changed.
* NOT claimed: that P17.7's evidence is unaffected. Its verification.md energy figures for the
  strong-discontinuity rows were wrong and are corrected in the P17.7 amendment's table -- 0.2961 ->
  0.01253 for Sod, and similarly for the other two. Its mass and momentum figures stand.
* NOT claimed: that P17.7 should be reopened. The correction is recorded against P17.7's evidence
  and re-verified; reopening the phase is a decision for the user, and it is raised in summary.md's
  next-step section rather than taken here.
* NOT changed: any other P17.1-P17.7 production source. 27 files verified UNTOUCHED by SHA-256.
```
