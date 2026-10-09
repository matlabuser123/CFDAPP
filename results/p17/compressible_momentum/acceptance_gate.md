# P17.4 — Compressible Momentum: acceptance gate

**Frozen 2026-09-27, after the dry-run and before any production edit.** Every baseline is
*measured* on the unchanged tree and recorded in `logs/00_dryrun.log`. Stop at the first
failure. No threshold may be weakened; a mis-derived criterion is preserved and amended in a
separate file.

---

## 0. What this item changes

```text
ONE production addition:
  assembleDilatationalStressContribution        a new, separate operator
  wired into assembleRelaxedCompressibleMomentumComponent behind an option
  that DEFAULTS TO DISABLED
```

Eight of nine requirements are verification. `audit.md` §6 has the classification.

```text
NOT changed   physics::assembleDiffusionContribution -- shared with the incompressible path
NOT changed   the MMS manufactured forcing (which omits this term by design)
NOT added     gravity or buoyancy on the compressible path (§8 says do not expand it)
NOT added     a case-format key for the new option -- recorded as follow-on, not claimed
```

### Why default-off, restated so the gate can enforce it

```text
1. The MMS test's forcing is hand-derived for `mu lap U` only. Enabling the term
   unconditionally would make a PASSING, VALIDATED test fail for a reason that is not a
   defect.
2. CLAUDE.md §6: prefer a dedicated path; prove existing results unchanged, bitwise where
   claimed. Default-off makes that provable by construction.
3. Precedent: nonOrthogonalCorrections{0} and robustness both default to "exactly the
   previous solver".
```

## 1. Measured baseline — the dry-run record

```text
dilatational hits (grad(div, dilatation, bulkViscosity, twoThirds, Stokes, divU)   ALL 0
production caller of assembleCompressibleMomentumComponent                          0
body force on the compressible path (bodyForce|gravity|buoyancy)                    0
GPU compressible momentum (compressibleMomentum|CompressibleRelaxed|dilatational)   0
existing tests   test_compressible_momentum.cpp 7   test_compressible_mass_flux.cpp 21
```

```text
A  pressure force, linear p (dp/dx = 7):
     interior cells 2, worst relative error vs -V*dp/dx   0          EXACT
     a BOUNDARY cell gives -2.333 against -V*C = -7, because the zero-gradient BC makes
     the Green-Gauss gradient one-sided there. Expected; interior and boundary are
     reported separately for this reason.
B  transient term, rho 1 -> 2, u_old 3, dt 0.25, V 1:
     diagonal 8   == rho_new V/dt          EXACT
     source   12  == rho_old V/dt u_old    EXACT
     the WRONG rho_new V/dt u_old form gives 24 -- a factor of 2 apart
C  body force, rho 1 / 2 / 4 with g = -9.81:
     rhs -9.81 / -19.62 / -39.24           exactly linear in rho
D  convection, irregular flux, varying U:
     diagonal range [5.61, 8.67]           sum of rhs exactly 0
E  THE GAP, U = (x^2/2, 0) so div U = x and grad(div U) = (1, 0) analytically:
     worst |div U - x|                     0         the divergence operator is EXACT here
     grad(div U).x at deep-interior cells  exactly 1
     worst |grad(div U).x - 1| over ALL 12 "interior" cells   2      <- see §2
     magnitude of the missing term         -0.066666666666666666 per cell
                                           == -(2/3) mu * 1 * V exactly
```

## 2. A boundary limitation of the composed operator — measured, and gated accordingly

`grad(div U)` is built by composing two existing tested operators. `div U` is exact
(measured 0 error). But **`grad(div U)` is exact only two or more cells from a boundary.**

```text
cause     there is no physical boundary condition for the derived scalar div U, so
          zero-gradient is used. That makes div U wrong in the boundary-adjacent layer,
          and the Green-Gauss gradient of a wrong neighbour value pollutes one cell
          further in.
measured  deep-interior cells: grad(div U).x == 1 exactly
          worst over all cells nominally "interior": |grad(div U).x - 1| = 2
```

**Consequence for the gate: the new term's accuracy is asserted in the DEEP interior only
(cells at least two cells from any boundary), never globally.** Asserting exactness
everywhere would fail a correct implementation, in the same way asserting
`rho(x)U(x) = const` exactly would have failed P17.3.

This is a real limitation of the term as implemented and is recorded in `summary.md` as
such — not as a defect, and not hidden.

## 3. Group F — the conservative form, term by term  (DETECTION)

| id | criterion | bound |
| --- | --- | --- |
| F1 | `d(rho U)/dt` uses `rho_new u_new` and `rho_old u_old` **separately**. | exact vs hand-derived |
| F2 | It is **not** `rho_new (u_new - u_old)`. A test with **both** rho and u changing must distinguish them. | the two differ by a factor ~2 |
| F3 | It reduces exactly to `implicitEulerTimeDerivative` at constant density. | existing test, **bitwise** |
| F4 | `div(rho U (x) U)` uses the **P17.3 authoritative massFlux**, passed in, not recomputed. | structural |
| F5 | `-grad(p)` equals `-V * grad(p)_component` against an analytical linear field. | **exact** at deep-interior cells |
| F6 | `rho f` is weighted by density: the RHS scales **linearly** with a supplied `rho*f`. | exact |
| F7 | `div(tau)` includes the dilatational term when enabled. | see Group D |

## 4. Group D — the dilatational stress term  (DETECTION)

| id | criterion | bound |
| --- | --- | --- |
| D1 | Disabled (the default) the assembly is **bitwise identical** to today. | `==` on every matrix and RHS entry |
| D2 | Enabled, the contribution equals `-(2/3) mu grad(div U)_component * V_P`. | rel <= 1e-12, deep interior |
| D3 | Enabled, it **changes the answer** for a field with `div U != 0`. | RHS differs |
| D4 | Enabled, it is **exactly zero** for a divergence-free field. | `== 0` |
| D5 | It vanishes in the incompressible limit, so it cannot corrupt that path. | follows from D4 |
| D6 | Invalid inputs are rejected, not silently consumed. | throws |

```text
D2's reference: the dry-run measured -(2/3)*0.1*1*1 = -0.066666666666666666 for
U = (x^2/2, 0) on a unit-volume cell. The analytical grad(div U) = (1,0) is exact in the
deep interior, so the criterion is checked there -- see §2.
D4 is what makes the term safe: for div U = 0 the operator must contribute nothing, so an
incompressible or uniform-flow case cannot be disturbed even with the option on.
```

## 5. Group C — conservation  (DETECTION)

| id | criterion | bound |
| --- | --- | --- |
| C1 | Internal-face convective momentum: the interior telescopes, so the global sum reflects boundary contributions only. | measured, reported |
| C2 | Uniform flow (`rho`, `U`, `p` all constant) gives a momentum residual at round-off. | derived bound |
| C3 | Variable-density constant-mass-flux flow uses the **actual** variable-density flux. | see N4 |
| C4 | The integral momentum balance closes: accumulation + boundary flux = pressure + viscous + body force. | derived bound |
| C5 | Linear pressure field: the net pressure force matches the analytical value. | **exact**, deep interior |

```text
DERIVATION of the C2/C4 bound -- reused from P17.1/P17.3, not re-derived:
  bound = kAccum * eps * N * scale
  N      the case's own cell or face count   -> scales with the mesh
  scale  measured from the case               -> scales with the magnitude
  kAccum = 8, EMPIRICAL and labelled as such
Never an absolute constant.
```

## 6. Group M — continuity/momentum consistency  (STRUCTURAL)

| id | quantity | criterion |
| --- | --- | --- |
| M1 | cell density | both use the P17.1 field — the same object |
| M2 | face density | one source, `evaluateCompressibleFaceDensity`; momentum reaches it only through the massFlux |
| M3 | face velocity | the same `interpolateFace` helper |
| M4 | mass flux | **the same `SurfaceField`, passed as an argument to both** |
| M5 | boundary mass flux | the same entries of that field |
| M6 | corrected flux | the next outer iteration re-assembles from the corrected flux |

All six are structural: `CompressibleSIMPLE` computes **one** `massFlux` and passes it to
both the momentum assembly and the continuity evaluation, so no second reconstruction can
drift. Verified by reading the call sites, not by comparing numbers.

## 7. Group L — low-Mach / incompressible reduction  (PRESERVATION)

| id | criterion |
| --- | --- |
| L1 | The existing low-Mach suite and the system MMS pass **unchanged**. |
| L2 | Its six thresholds are **inherited verbatim**, not re-derived. |
| L3 | The P13-001 SIMPLE fingerprint is **BITWISE identical**. |
| L4 | With the dilatational option off, every compressible result is bitwise unchanged. |
| L5 | `physics::assembleDiffusionContribution` is **untouched**. |

## 8. Group B — boundary momentum  (DETECTION)

| id | criterion |
| --- | --- |
| B1 | A stationary impermeable wall carries **zero normal mass flux** but can still transmit a pressure force and viscous shear. |
| B2 | Inlet and outlet convective momentum carry the expected signs. |
| B3 | Symmetry contributes zero normal momentum flux. |

```text
NOT PRESENT, stated rather than skipped: mass-flow inlet and periodic BCs do not exist in
this codebase at all.
```

## 9. Group V — invalid states  (DETECTION)

| id | criterion |
| --- | --- |
| V1 | The dilatational operator rejects a non-finite viscosity. |
| V2 | It rejects a size mismatch. |
| V3 | The existing assembly still throws `NumericalError` on a non-finite assembled system. |
| V4 | Nothing is clamped. |

EOS and density validation are **not** duplicated here — P17.1 and P17.2 own them, and §18
says not to duplicate them everywhere.

## 10. Group N — non-vacuity controls  (each MUST fail)

| id | control | must break |
| --- | --- | --- |
| N1 | Drop the `-(2/3)` factor (use `-1`). | D2 |
| N2 | Drop the `V_P` factor. | D2 |
| N3 | Use `div U` instead of `grad(div U)`. | D2, D4 — proves it is the gradient that matters |
| N4 | Momentum convection recomputes `rho_f U_f` from the cell density instead of using the passed massFlux. | C3, F4 |
| N5 | Flip the sign of the transient `source` term. | F1, F2 |
| N6 | Make the default **enabled**. | D1 — proves the default-off path is what preserves the baseline |

N6 is the sharpest: it is the control that proves default-off is load-bearing rather than
decorative. It must break the bitwise-identity criterion **and** the SIMPLE fingerprint.

## 11. Group G — preservation gate

| id | criterion |
| --- | --- |
| G1 | The whole compressible suite passes — **161** at the P17.3 baseline, plus new. |
| G2 | P14 **68**, P15 **161**, P16 **315**, frozen narrow regexes. |
| G3 | The P13-001 SIMPLE fingerprint is **BITWISE identical**, empty diff. |
| G4 | Release+CUDA: **2531 + new**, 0 failed. |
| G5 | CPU-only: **2331 + new**, 0 failed. |
| G6 | `clang-format-18` clean on the changed files. |
| G7 | `P17_CODE_FINGERPRINT` moves only by the authorized delta. |
| G8 | **VACUOUS — no GPU compressible momentum path exists.** Recorded as NOT PRESENT, not PASS. |
| G9 | Determinism: repeated assembly is bitwise identical. |

## 12. Gate verdict

P17.4 closes only when **F, D, C, M, L, B, V and G all pass and every N control fails**.

```text
FORBIDDEN
  weakening any bound, including the deep-interior restriction in §2
  asserting grad(div U) exactness at boundary-adjacent cells
  enabling the dilatational term by default
  re-deriving the MMS forcing inside P17.4
  modifying physics::assembleDiffusionContribution
  claiming G8 as PASS
  implementing anything in P17.5-P17.10
```
