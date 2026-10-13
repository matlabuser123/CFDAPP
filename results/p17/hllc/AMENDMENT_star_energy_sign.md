# P17.7 AMENDMENT — a REAL SIGN ERROR in the HLLC star energy, found by P17.8

**This is a correctness defect in shipped P17.7 code, not a documentation fix.** It was present in the
C++ *and* in the frozen independent reference, which is precisely why P17.7's own gate could not see
it. Found by P17.8's inviscid-wall test.

---

## 1. The defect

```text
WRONG, as P17.7 shipped it:
    rhoE*_K = rho*_K [ rhoE_K/rho_K + (S* - u_nK)(S* - p_K/(rho_K(S_K - u_nK))) ]
CORRECT:
    rhoE*_K = rho*_K [ rhoE_K/rho_K + (S* - u_nK)(S* + p_K/(rho_K(S_K - u_nK))) ]
                                                     ^ PLUS
```

Present in **both**:

```text
include/cfd/compressible/HllcFlux.hpp        the C++ star energy
results/p17/hllc/tools/exact_riemann.py      the FROZEN 60-digit independent reference
```

## 2. The derivation, so the correction is not another guess

The star state must satisfy the Rankine–Hugoniot conditions across the `S_K` wave, with the HLLC
assumptions `u*_L = u*_R = S*` and `p*_L = p*_R = p*`. The energy condition is

```text
  S_K (rhoE*_K - rhoE_K) = F*_K,energy - F_K,energy ,      F*_K,energy = S* (rhoE*_K + p*)
```

and the momentum condition gives `p* = p_K + rho_K(S_K - u_nK)(S* - u_nK)`. Writing
`D = S_K - u_nK` and `rho*_K = rho_K D/(S_K - S*)`, and solving for the bracket `X` in
`rhoE*_K = rho*_K X`:

```text
  X = [S_K rhoE_K + S* p* - u_nK(rhoE_K + p_K)] / (rho_K D)
    = rhoE_K/rho_K + p_K(S* - u_nK)/(rho_K D) + S*(S* - u_nK)
    = rhoE_K/rho_K + (S* - u_nK) [ S* + p_K/(rho_K D) ]        <-- a PLUS
```

## 3. Numerical proof, computed independently of CFDApp

`tools/../density_based_solver/tools/check_star_energy_sign.py`, mpmath at 50 digits, no CFDApp code.
The state is a mirrored inviscid wall (`rho = 1.2`, `T = 320 K`, `u_n = 120 m/s`), for which the exact
answer is known: `S* = 0`, so the energy flux must be `S*(rhoE* + p*) = 0`.

```text
S_L = -304.51598759   S_R = 304.51598759   S* = 0
p   = 110227.2        p* (Rankine-Hugoniot) = 171357.502213     (a collision, so p* > p)

form                      rhoE*_L         F*_energy        S*(rhoE* + p*)
MINUS (as P17.7 shipped)  354468.7465     26454528.0       0.0        <-- WRONG by 2.6e+07 W/m^2
PLUS  (RH-derived)        441342.7665     0.0              0.0        <-- correct
```

## 4. WHY P17.7's GATE DID NOT CATCH IT — the part that matters

This is the instructive half. P17.7's gate had a criterion aimed squarely at this (X1: HLLC must equal
the exact Godunov flux where it is algebraically exact) and a second one comparing the implementation
to an independent 60-digit reference (X2). Both passed. Neither could have failed.

```text
X2 COULD NOT CATCH IT: the C++ and the reference were written from the SAME misremembered formula, so
   they agreed with each other to 3.25e-16. Comparing two artefacts with a common author is not
   independence, however different their languages and precisions. The reference was independent of
   CFDApp's CODE; it was not independent of my UNDERSTANDING.

X1 COULD NOT CATCH IT: the erroneous term is (S* - u_nK) x (...). Every one of the eight cases where
   HLLC is algebraically exact -- uniform x, uniform oblique, stationary contact, moving contact,
   supersonic left, supersonic right, near-sonic, strong density ratio -- has EITHER a supersonic
   branch (the star state is never used) OR S* = u_nK (the factor is exactly zero). So the defect was
   multiplied by zero in every gated case.
   X1's coverage blind spot was exactly aligned with the defect's support.

AND THE ERROR HID IN A NUMBER I REPORTED AS PHYSICS: the strong-discontinuity rows were REPORTED and
   deliberately not gated, on the correct ground that no tight tolerance is derivable for them. The
   inflated energy error sat there and I attributed it to the wave-speed estimator.
```

### What it cost, quantified

The corrected reference changes only the **energy** component, and only where `S* != u_n`:

```text
|diff| / scale vs the EXACT Godunov flux        mass      x-momentum    ENERGY
sod_literature            before the fix        0.0061    0.0609        0.2961
                          after  the fix        0.0061    0.0609        0.01253   <-- 24x better
strong_pressure_ratio     before                0.0768    0.1026        0.3177
                          after                 0.0768    0.1026        0.02538   <-- 13x better
low_pressure              before                0.0301    0.0617        0.2565
                          after                 0.0301    0.0617        0.006715  <-- 38x better
every algebraically exact case, before and after: 0.0
```

So most of what P17.7's `verification.md` §8 called "the scheme's own error" in energy was **this
bug**. The mass and momentum figures stand; the energy figures in P17.7's evidence are corrected to
the right-hand column above.

## 5. How P17.8 found it

P17.8's inviscid wall builds the exterior state by mirroring the normal velocity, which makes the
Riemann problem symmetric: no mass and no energy may cross. The mass flux came out at 2.84e-14 —
correct — and the energy flux at **2.645e+07 W/m²**.

```text
A wall is the one configuration where S* = 0 while u_n != 0, so (S* - u_n) is NOT zero and the
erroneous term is NOT multiplied away. It is the cheapest possible probe for this defect, and it was
absent from P17.7 because P17.7 had no boundary conditions -- the flux layer deliberately had none.
The defect therefore lived exactly in the gap between the two phases' scopes.
```

## 6. The frozen-document consequence, disclosed

`tools/exact_riemann.py` was FROZEN as part of P17.7's gate. Correcting it moves that hash, and a
frozen reference cannot be silently edited:

```text
frozen at the P17.7 freeze   cddf94e5cc227eb6a53906cefecb03670e3e6d9efb2156b378d86af24843d390
after the sign correction    63f2ea5a721bddbb2b1f260ec5f0fe62dcb2135f6525962fe588368baf2dec6b
after correcting its docstring to match   a50242580f620c42280df042a3bd4802739c87d6dfcaa163b70236a54609d95c
```

The gate document itself (`acceptance_gate.md`, `1f4ccd8a…`) is **unchanged**: no criterion, threshold
or prediction moved. What changed is a reference implementation that was wrong, and the change makes
the reference *more* independent of the code rather than less — it now satisfies a property
(`F*_K,energy == S*(rhoE*_K + p*)`) that is checkable without reference to either artefact.

## 7. What was done

```text
1  the sign corrected in HllcFlux.hpp, with the derivation in the comment
2  the sign corrected in exact_riemann.py, with the same note
3  tests/unit/compressible/hllc_reference_data.hpp REGENERATED from the corrected reference
4  P17.7's full suite re-run: 35/35 CPU + 4/4 GPU still pass. X1 and X2 unchanged at 3.254e-16 --
   as predicted, because those cases never exercised the term
5  P17.8's wall energy flux: 2.645e+07  ->  7.45e-09 W/m^2   (a factor of 3.5e+15)
6  P17.8's closed-domain conservation, which had also failed, now passes: mass 8.31e-16,
   energy 1.70e-16 relative over 37 steps
7  a PERMANENT test added so this cannot regress -- P17.8's InviscidWallCarriesNoMassOrEnergy
   asserts the wall energy flux against a derived round-off bound
```

## 8. The lesson, stated for the next phase

```text
AN INDEPENDENT REFERENCE MUST BE INDEPENDENT OF THE AUTHOR'S UNDERSTANDING, NOT JUST OF THE CODE.
Two implementations of the same remembered formula agree perfectly and prove nothing. The defence is
to assert a DEFINING PROPERTY that neither artefact can satisfy by accident -- here,
F*_K,energy == S*(rhoE*_K + p*), which follows from Rankine-Hugoniot and is independent of both. That
assertion is now in the header comment and is what the correction was verified against.

AND: A CRITERION'S NON-VACUITY IS NOT THE SAME AS ITS COVERAGE. X1 was genuinely non-vacuous -- the
controls proved defects that break it. But every case it covered had the defect's factor equal to
zero. Ask separately: "does this criterion FIRE on a planted defect?" and "does its case set REACH
the term I am worried about?"
```
