# P17.3 — Compressible Continuity: acceptance gate

**Frozen 2026-09-26, after the dry-run and before any production edit.** Every baseline is
*measured* on the unchanged tree and recorded in `logs/00_dryrun.log`, never inferred. Stop
at the first failure. No threshold may be weakened; a mis-derived criterion is preserved
and amended in a separate file.

`eps` means the double epsilon, `2.2204460492503131e-16`.

---

## 0. What this item changes

```text
TWO production additions, both to the compressible transient diagnostic:
  CompressibleContinuityResult::globalNetFlux      the boundary mass-flux sum
  evaluateCompressibleContinuity                   value preconditions on its inputs
```

Everything else is verification, testing or documentation. `audit.md` §3 classifies all
nine requirements; seven are already satisfied.

```text
NOT changed   physics::evaluateContinuity -- shared with the incompressible path; a change
              there would put P17.3 in the way of the constant-density fingerprint for no
              P17.3 benefit. The value-validation asymmetry is recorded as debt.
NOT added     a production caller for the transient diagnostic. That is a transient
              compressible SOLVER and no P17 item has authorized one.
NOT touched   the mass-flux construction (P17.1), the EOS (P17.2), momentum (P17.4)
```

## 1. Measured baseline — the dry-run record

```text
globalNetFlux in CompressibleContinuityResult      0 hits   ABSENT
value checks in evaluateCompressibleContinuity     0        ABSENT
value checks in physics::evaluateContinuity        0        ABSENT
production callers of evaluateCompressibleContinuity 0      DIAGNOSTIC ONLY
existing tests   test_compressible_continuity.cpp  6        test_compressible_mass_flux.cpp 20
```

Row-varying construction, 4x3 mesh, rho 2:1, u = (2,0):

```text
steady maxCellImbalance          0            totalAbsoluteImbalance 0    globalNetFlux 0
transient (rhoOld == rhoNew)     0            totalAbsoluteImbalance 0
totalMassOld / totalMassNew      20 / 20
internal faces                   17           worst |(+F) + (-F)| = 0
MassBalance   inflow 10   outflow 10   net 0   relativeImbalance 0
              fluxScale 10   normalizedContinuity 0   maxCellImbalance 0   rms 0
boundary by patch (outward-positive)
  left  -10 (inflow)   right +10 (outflow)   bottom 0   top 0 (walls)
steady vs transient cellImbalance   0 of 12 cells differ bitwise, max diff 0
```

`rho(x) U(x) = const`, refinement:

```text
nx     maxCellImbalance          normalizedContinuity      ratio vs previous
8      0.0075960497835497187     0.010418324324846793      -
16     0.0019244223593570531     0.0037897426111169735     3.947
32     0.00048458280630511719    0.0013599913865735792     3.971
64     0.00012160083245853909    0.00048451148048800531    3.985
```

### The gap, measured — and the NaN trap

```text
input                     maxCellImbalance   totalMassNew   outcome
-------------------------------------------------------------------------
rhoNew[0] = -2.0                 4                0         RETURNED
rhoNew[0] =  0.0                 2                2         RETURNED
rhoNew[0] =  NaN                 0              nan         RETURNED
rhoNew[0] = +Inf               inf              inf         RETURNED
massFlux[0] = NaN                0                -         RETURNED
```

All five returned, so the criteria below are non-vacuous. **Two results are worse than
"invalid input is accepted":**

```text
1. A NaN makes maxCellImbalance report EXACTLY 0 -- "perfectly conserved".
   std::max(a, b) is (a < b) ? b : a. With a = 0 and b = NaN, `0 < NaN` is false, so the
   running max stays 0 and the NaN is silently skipped. A solver monitoring
   maxCellImbalance < tol would declare convergence on a NaN state.
   (totalAbsoluteImbalance does become NaN, so the two metrics disagree -- a split brain.)
2. A NEGATIVE density makes totalMassNew exactly 0 -- finite, plausible, and catastrophic.
   The mirror of P17.1's finding: there finite-but-wrong evaded allFinite(); here NaN
   evades the max-reduction.
```

## 2. Group S — the steady form  (DETECTION)

| id | criterion | bound |
| --- | --- | --- |
| S1 | `div(rho U) = 0` holds per cell on the row-varying construction, with rho genuinely varying 2:1. | **exactly 0** |
| S2 | `globalNetFlux` is zero for that case. | **exactly 0** |
| S3 | The implementation is `div(rho U)`, **not** `rho div(U)`. A constant-density implementation must fail. | see N4 |
| S4 | Interior-face contributions cancel in the global sum. | **exactly 0** |

S1/S2/S4 are gated **exact** because the dry-run measured exactly 0: the construction makes
a cell's left and right faces take the same `rho_f`, so their fluxes cancel bit for bit,
and y-normal faces carry zero against an x-only velocity.

## 3. Group T — the transient form  (DETECTION)

| id | criterion | bound |
| --- | --- | --- |
| T1 | The discrete operator is `(rho^{n+1} - rho^n) V_P/dt + sum_f(m_dot_f)`, verified against a hand-derived value. | rel <= 1e-12 |
| T2 | With `rhoOld == rhoNew` the operator reduces exactly to the steady sum. | **exactly 0** difference |
| T3 | Density genuinely changing in time, not a constant-rho test. | `max|rhoNew - rhoOld| > 0` |
| T4 | `totalMassOld` and `totalMassNew` equal `sum(rho_i V_i)` at each level. | rel <= 4 eps |
| T5 | **The integral relation** `M(t2) - M(t1) + dt * sum_boundary(m_dot) ~= 0` for a constructed case. | derived bound, §6 |

T5 is the criterion `globalNetFlux` exists to serve. Before this item a caller had to
re-loop the boundary patches and re-derive the sign convention by hand, which the dry-run
probe demonstrably had to do.

```text
DERIVATION of T4's 4 eps: totalMass is a sum of N products rho_i*V_i, so the accumulated
relative error is of order N*eps; for the small meshes used here N <= 64, giving ~64 eps
worst case in principle but far less in practice. 4 eps bounds the measured value (the
dry-run gave totalMass exactly 20 on a 12-cell mesh). If a larger mesh is used the bound
must scale with N -- stated so a future reader does not apply 4 eps to a 10^6-cell case.
```

## 4. Group F — one authoritative face mass flux  (DETECTION)

| id | criterion | bound |
| --- | --- | --- |
| F1 | `m_dot_f = rho_f (U_f . n_f) A_f`, against an independent expectation. | rel <= 1e-12 |
| F2 | `areaVector` carries normal **and** area, so no separate area factor is applied twice. | exact |
| F3 | Internal-face antisymmetry: the owner receives `+F` and the neighbour `-F`. | **exactly 0** sum |
| F4 | Reversing the velocity reverses every flux exactly. | **exact** negation |
| F5 | Continuity, the pressure-correction `D_f` and the diagnostics all consume the **same** `rho_f` field — one canonical source, not three evaluations. | structural |

F5 is structural, not numerical: `evaluateCompressibleFaceDensity` returns one
`SurfaceField` that `calculateCompressibleMassFlux` computes and
`assembleCompressiblePressureCorrection` is **handed**. Verified by reading the call sites,
not by comparing numbers.

## 5. Group B — boundary mass fluxes  (DETECTION)

| id | criterion | bound |
| --- | --- | --- |
| B1 | Stationary impermeable wall: `U . n = 0` so `m_dot = 0`. | **exactly 0** |
| B2 | Inlet carries mass **inward**: sum over the patch is negative under the outward-positive convention. | sign |
| B3 | Outlet carries mass **outward**: positive. | sign |
| B4 | Inlet and outlet balance for a steady uniform case. | **exactly 0** sum |
| B5 | A pressure-type boundary takes its density from the **EOS at that face's own state**, not a stale cell value. | rel <= 1e-12 |
| B6 | Symmetry: zero normal component, hence zero flux. | **exactly 0** |

Measured baseline: `left -10, right +10, bottom 0, top 0`. Signs already correct.

```text
NOT COVERED, and stated rather than silently skipped:
  mass-flow inlet   no such BC class exists in this codebase
  periodic          no Periodic BC class exists at all -- not a compressible gap
  moving wall       MovingWall exists; its tangential velocity gives zero normal flux on
                    an axis-aligned face, so it is covered by the B1 argument
```

## 6. Group C — global conservation, using the ESTABLISHED metric  (DETECTION)

`physics::computeMassBalance` already provides `inflow, outflow, net, relativeImbalance,
fluxScale, normalizedContinuity, maxCellImbalance, rmsCellImbalance`. §8 of the
authorization says to use the project's established diagnostic if one exists. **It does, and
P17.3 reuses it. No new metric is invented.**

| id | criterion | bound |
| --- | --- | --- |
| C1 | Steady through-flow: `net = 0`, `relativeImbalance = 0`. | **exactly 0** |
| C2 | Closed box, quiescent: every boundary flux zero, `net = 0`, per-cell imbalance 0. | **exactly 0** |
| C3 | Variable-density case: the same, with rho varying 2:1. | **exactly 0** |
| C4 | Transient integral relation (T5). | derived below |

```text
DERIVATION of the C4 / T5 bound -- reused from P17.1, not re-derived:
  bound = kAccum * eps * N_f * max|m_dot_f|
  eps         the double epsilon
  N_f         the case's own face count      -> the bound SCALES with the mesh
  max|m_dot|  measured from the case          -> and with the flux magnitude
  kAccum = 8  EMPIRICAL, labelled as such: a safety factor on summation order, not a
              value fitted to an observed result.
Never an absolute constant. Identical in form to the existing GlobalMassImbalanceIsSmall,
which also bounds against a computed flux scale.
```

**A closed box with a non-zero uniform interior velocity is NOT a valid C2 case.** The
dry-run measured `net = 0` but `maxCellImbalance = 4` there, because a uniform velocity is
not solenoidal inside impermeable walls — the imposed field is inconsistent, not the code.
C2 therefore specifies a **quiescent** closed box, where both the boundary sum and the
per-cell imbalance are exactly zero.

## 7. Group M — manufactured and canonical tests  (DETECTION)

| id | criterion | bound |
| --- | --- | --- |
| M1 | Uniform `rho`, uniform `U`: `div(rho U) = 0`. | **exactly 0** |
| M2 | Row-varying `rho` with x-only velocity: `div(rho U) = 0` with rho genuinely varying. | **exactly 0** |
| M3 | The operator fed a constructed divergence-free `massFlux` returns zero imbalance. | **exactly 0** |
| M4 | `rho(x) U(x) = const`: the imbalance **decreases at second order** under refinement. | ratio > 3.5 at the finest pair |
| M5 | Closed box, quiescent. | **exactly 0** |
| M6 | Inlet/outlet balance. | **exactly 0** |

```text
M4 IS NOT AN EXACTNESS CLAIM, and that is the point.
  With linear face interpolation rho_f U_f = mean(rho) mean(U) != mean(rho U), so the
  imbalance is O(h^2), NOT zero. A test asserting exact zero there would fail a CORRECT
  implementation.
  DERIVATION of the 3.5 threshold: second order gives a ratio of 4 per halving of h;
  first order gives 2. A threshold of 3.5 admits pre-asymptotic behaviour while excluding
  first order by a wide margin. Measured: 3.947, 3.971, 3.985 -- converging to 4.
  Gated on the FINEST pair, which is the most asymptotic.
```

## 8. Group O — summation-order consistency  (DETECTION)

| id | criterion | bound |
| --- | --- | --- |
| O1 | `evaluateCompressibleContinuity` with `rhoOld == rhoNew` agrees with `physics::evaluateContinuity` on the row-varying case. | **bitwise** |
| O2 | The two agree on an **irregular** flux field, where every face carries a distinct magnitude so summation order actually matters. | bitwise, **or** the difference reported and bounded at `4 * eps * max|m_dot|` |

```text
WHY O2 EXISTS. The two evaluators sum the same terms in DIFFERENT loop orders:
  physics::evaluateContinuity      per cell, over cell.faceIds()
  evaluateCompressibleContinuity   per face, into owner and neighbour
Floating-point addition is not associative, so agreement is not guaranteed in general.
The dry-run measured them BITWISE EQUAL on the row-varying case -- but there most face
contributions are equal-and-opposite or zero, so that case cannot discriminate.
This is not a theoretical worry: cuda/kernels/DevicePressureCorrectionKernel.cu:28 and
:229 record that the GPU port deliberately gathers "in cell.faceIds() order" because those
are "the two orders the CPU actually uses" -- the project already treats continuity
summation order as load-bearing.
O2 is therefore MEASURED, and the gate accepts EITHER outcome provided it is reported:
bitwise agreement, or a bounded difference. What it does not accept is the question going
unasked.
```

## 9. Group L — low-Mach reduction  (PRESERVATION)

| id | criterion |
| --- | --- |
| L1 | The existing low-Mach suite passes unchanged — 7 cases plus the system MMS. |
| L2 | `deltaRhoOverRho < 1e-2`, `maxEosError < 1e-9`, `maxRelativeMassFluxDifference < 0.15`, `globalMassImbalance <= its computed bound` — **inherited verbatim**, not re-derived. |
| L3 | As `Ma -> 0` with `rho -> const`, `div(rho U) = 0` reduces to `div(U) = 0`: the compressible mass flux at uniform density equals `physics::calculateMassFlux` **bitwise**. |
| L4 | The MMS order gate holds: 1.0 in band [0.55, 1.30]. |

L3 is the concrete form of the reduction and was measured bitwise-equal in P17.1's
dry-run (0 differing faces). **Bitwise is required here** because at uniform density the
two formulas are the same arithmetic, not merely close — this is not a comparison between
physically different formulations.

## 10. Group V — invalid states  (DETECTION)

| id | criterion |
| --- | --- |
| V1 | A non-finite or non-positive `densityOld` / `densityNew` is rejected. NEW |
| V2 | A non-finite `massFlux` entry is rejected. NEW |
| V3 | The message names the offending **cell** or **face** index. NEW |
| V4 | The existing size and `dt` checks still hold. |
| V5 | Nothing is clamped. |

```text
massFlux is validated for FINITENESS ONLY. A face flux is legitimately negative (inflow)
and legitimately zero (a wall), so a positivity rule would be WRONG for it -- the same
distinction P17.1 drew for gauge pressure.
```

## 11. Group N — non-vacuity controls  (each MUST fail)

| id | control | must break |
| --- | --- | --- |
| N1 | Drop the density-value validation. | V1 |
| N2 | Drop the mass-flux finiteness validation. | V2 |
| N3 | `globalNetFlux` sums **all** faces, not only boundary faces. | T5, C1 — proves it is the boundary sum, not a total |
| N4 | Face density replaced by a single constant. | S3, M2, M4 — a `rho div(U)` implementation |
| N5 | Flip the neighbour sign (`+F` instead of `-F`). | F3, S1, S4 |
| N6 | Drop the `V_P/dt` factor from the time term. | T1, T5 |

N4 is the control that proves S3: it turns the operator into the constant-density form, and
M2 (row-varying, exact) must catch it. N5 attacks the orientation convention directly.

## 12. Group G — preservation gate

| id | criterion |
| --- | --- |
| G1 | The whole compressible suite passes — **149** at the P17.2 baseline, plus new. |
| G2 | P14 **68**, P15 **161**, P16 **315**, frozen narrow regexes. |
| G3 | The P13-001 SIMPLE fingerprint is **BITWISE identical**, 10 cases / 130 lines, empty diff. |
| G4 | Release+CUDA regression: **2519 + new**, 0 failed. |
| G5 | CPU-only regression: **2319 + new**, 0 failed. |
| G6 | `clang-format-18` clean on the files P17.3 changes. |
| G7 | `P17_CODE_FINGERPRINT` moves only by the authorized delta, attributed file by file. |
| G8 | **VACUOUS — no GPU compressible continuity path exists.** The GPU has an *incompressible* continuity gather (`DevicePressureCorrectionKernel.cu`); there is no compressible one. Recorded as NOT PRESENT, not as PASS. |
| G9 | Determinism: repeated evaluation is bitwise identical. |

## 13. Gate verdict

P17.3 closes only when **S, T, F, B, C, M, O, L, V and G all pass and every N control
fails**, with the delta attributed file by file and both fingerprints recorded.

```text
FORBIDDEN
  weakening any bound above, including M4's 3.5 ratio and C4's kAccum
  gating M4 as exact (the discrete operator is only O(h^2) there)
  gating C2 with a non-solenoidal interior field
  claiming G8 as PASS -- it is vacuous
  modifying physics::evaluateContinuity
  giving the transient diagnostic a production caller
  implementing anything in P17.4-P17.10
```
