# P17.7 — acceptance gate

**FROZEN before any implementation source was written.** Every threshold below is either exact
(bitwise / boolean) or derived from a stated round-off analysis. None is fitted to an observed error.

`eps = 2.2204460492503131e-16` (double). `scale[i]` is the characteristic flux scale of
`tools/exact_riemann.py::flux_scales`: `rho_m a_m` for mass, `rho_m a_m^2 + p_m` for each momentum
component, `rho_m a_m^3 + p_m a_m` for energy, with `rho_m, a_m, p_m` the max over the two states.

---

## 0. THE FORMULATION, fixed here so no criterion can be reinterpreted later

```text
wave speeds   Toro 3rd ed. eqs. 10.59-10.61 -- a PRESSURE-BASED estimate with a shock factor:
                p_pvrs = 0.5(p_L + p_R) - 0.125 (u_nR - u_nL)(rho_L + rho_R)(a_L + a_R)
                p*     = max(0, p_pvrs)
                q_K    = 1                                           if p* <= p_K
                       = sqrt(1 + (gamma+1)/(2 gamma) (p*/p_K - 1))   if p* >  p_K
                S_L    = u_nL - a_L q_L        S_R = u_nR + a_R q_R
contact       Toro 3rd ed. eq. 10.37:
                S* = [ p_R - p_L + rho_L u_nL (S_L - u_nL) - rho_R u_nR (S_R - u_nR) ]
                     / [ rho_L (S_L - u_nL) - rho_R (S_R - u_nR) ]
star states   Toro 3rd ed. eq. 10.39, normal/tangential form:
                rho*_K    = rho_K (S_K - u_nK) / (S_K - S*)
                (rhoU)*_K = rho*_K [ U_K + (S* - u_nK) n ]        tangential preserved exactly
                (rhoE)*_K = rho*_K [ rhoE_K/rho_K
                                     + (S* - u_nK)(S* - p_K/(rho_K (S_K - u_nK))) ]
flux          F*_K = F_K + S_K (Q*_K - Q_K)
normal        UNIT normal + a SEPARATE scalar area. The flux returned is PER UNIT AREA.
```

### Why Davis (10.48) was rejected, by measurement

The authorization names `S_L = min(u_nL - a_L, u_nR - a_R)`, `S_R = max(...)` as an admissible
baseline. Measured against the independent exact solver **before writing any code**, it **fails the
signal-bounding property in 8 of 14 reference cases** — for Sod it gives `S_R = 1.1831` while the true
right shock travels at `1.7521`, so signal escapes the control volume. Toro's pressure-based estimate
passes **14 of 14**. Davis is therefore retained **only as the non-vacuity control for W4**.

```text
The denominator bound is unaffected by the choice: q_K >= 1 gives (S_L - u_nL) = -a_L q_L <= -a_L and
(S_R - u_nR) = +a_R q_R >= +a_R, so the W3 bound holds for BOTH estimators and is only looser for
Toro's. Nothing is given up.
```

## 1. Group D — the authoritative state definitions (§2, §3)

| id | criterion | threshold |
| --- | --- | --- |
| D1 | `ConservedState`, `PrimitiveState`, `FluxVector` exist with **units documented per component**; no bare `E` | structural |
| D2 | **No EOS inside the flux layer.** 0 occurrences of `p = rho R T`, `(gamma-1)*`, `sqrt(gamma*R*` or any pressure/sound-speed formula in `HllcFlux.hpp`/`.cpp` | exactly 0 |
| D3 | `TotalEnergyState::conservedTotalEnergy()` agrees with `p/(gamma-1) + 0.5 rho |U|^2` under the **zero** reference | `<= 8 eps * |rhoE|` — the two are algebraically identical there but are different expression trees |
| D4 | `speedOfSound()` is the **sole** sound-speed source; `recover()` is the **sole** conserved→primitive path | exactly 1 call site each in the factory, 0 elsewhere |

## 2. Group F — the directional physical flux (§4, §5)

| id | criterion | threshold |
| --- | --- | --- |
| F1 | `eulerFlux` at `n = (1,0,0)` equals the textbook 1D Euler formula | **BITWISE** |
| F2 | `eulerFlux` at `n = (0,1,0)` and `(0,0,1)` equals the x-version with components permuted | **BITWISE** — axis normals are exactly representable |
| F3 | `eulerFlux` matches the independent reference, all 5 components, 5 states, 4 normals | `<= 8 eps * scale[i]` |

## 3. Group C — consistency, G6(a) (§5, §16A)

| id | criterion | threshold |
| --- | --- | --- |
| C1 | `F_HLLC(Q,Q,n) == eulerFlux(Q,n)`, **supersonic** uniform state | **BITWISE** — a pure branch selection returns `F_L`/`F_R` unmodified |
| C2 | same, **subsonic** uniform state | `<= 32 eps * scale[i]` — DERIVED below |
| C3 | C1/C2 hold for `n` = x, y, z and the oblique `(2,-3,6)/7`, over >= 5 thermodynamic states | as above |

```text
DERIVATION OF THE 32 eps IN C2. For Q_L = Q_R the contact numerator is
    num = 0 + q_L u_n - q_R u_n     computed as fl(q_L u_n) - fl(q_R u_n)
so S* = u_n only up to round-off. The amplification is bounded: q_L <= -rho a < 0 and
q_R >= +rho a > 0, so |q_L - q_R| = |q_L| + |q_R| and the condition number of the subtraction is
EXACTLY 1 -- there is no cancellation. Hence |S* - u_n| <~ 3 eps |u_n|.
Propagating: rho*_L = rho (S_L - u_n)/(S_L - S*), and perturbing S* by u_n delta changes the
denominator relatively by |u_n| delta / |S_L - u_n| = M delta with M = |u_n|/a <= 1 in the subsonic
branch. So rho*_L = rho (1 + O(M eps)) and
    |F*_L - F_L| = |S_L| |Q*_L - Q_L| ~ (a + |u_n|) M eps |Q| ,
which against |F| ~ (a + |u_n|) |Q| is O(M eps) <= O(eps). Allowing 3 eps for S*, a factor for the
five components and a factor 2 of headroom gives 32 eps. It is a BOUND, not a fit: nothing has been
measured in double precision at the time of freezing.
```

## 4. Group W — wave speeds and non-degeneracy (§9, §10, §25)

| id | criterion | threshold |
| --- | --- | --- |
| W1 | `S_L`, `S*`, `S_R` match the independent reference | `<= 8 eps * max(|S|, a_m)` |
| W2 | `S_L < S* < S_R` **strictly**, every reference case | boolean |
| W3 | `|denom| >= (rho_L a_L + rho_R a_R)(1 - 4 eps)` | DERIVED; the bound is **attained** when `u_nL = u_nR` and `a_L = a_R`, so the `4 eps` slack is for the equality case only |
| W4 | **SIGNAL BOUNDING**: `S_L <= ` exact leftmost wave and `S_R >= ` exact rightmost wave | inequality with `4 eps * a_m` slack, every case with a 1D exact solution |
| W5 | near-sonic (`S_L` within 1e-9 of 0) selects the correct branch and stays finite | boolean + finite |

```text
W3 IS THE ANSWER TO SECTION 10. The contact denominator is provably bounded away from zero for
admissible states, so NO epsilon guard and NO fallback flux is introduced for it. Section 10 forbids
"an arbitrary large epsilon without justification"; the justification here is that none is needed.
W4 IS THE GATED PART OF G6(f) that IS derivable -- see section 9.
```

## 5. Group B — all four branches, G6(b) (§13, §16F, §16G)

| id | criterion | threshold |
| --- | --- | --- |
| B1 | `S_L >= 0` returns `eulerFlux(L)` | **BITWISE** |
| B2 | `S_R <= 0` returns `eulerFlux(R)` | **BITWISE** |
| B3 | `S_L < 0 <= S*` returns `F*_L` | `<= 32 eps * scale[i]` vs the reference |
| B4 | `S* < 0 <= S_R` returns `F*_R` | `<= 32 eps * scale[i]` vs the reference |
| B5 | each of the four branches entered by >= 1 **deterministically constructed** case, and the selected branch matches the reference | exact string match on the branch |

## 6. Group S — star states and positivity, G6(e) (§11, §14)

| id | criterion | threshold |
| --- | --- | --- |
| S1 | `rho*_L > 0` and `rho*_R > 0` | boolean, no tolerance |
| S2 | every star component finite | boolean |
| S3 | star specific internal energy `> 0` (thermodynamically admissible) | boolean |
| S4 | `rho*_K` matches the reference | `<= 32 eps * max(rho_L, rho_R)` |
| S5 | the diagnostic counters `invalidStateDetections` and `fallbackActivations` read **exactly 0** over every admissible test | exactly 0 |

```text
WHAT IS AND IS NOT CLAIMED, per section 14:
  CLAIMED    admissible left/right states are validated and refused if not
  CLAIMED    admissible star states for admissible inputs, proven (W3) and tested (S1-S4)
  NOT CLAIMED  a positivity-preserving TIME UPDATE. That depends on CFL and reconstruction and
               belongs partly to P17.8. Any wording that blurs this is a gate failure.
```

## 7. Group T — the contact wave (§12, §21)

| id | criterion | threshold |
| --- | --- | --- |
| T1 | **stationary contact** (`u_n = 0`, `p_L = p_R`, `rho_L != rho_R`): mass flux and energy flux are **exactly 0**, momentum flux is **exactly** `p n` | **BITWISE** — DERIVED below |
| T2 | **moving contact** (`u_nL = u_nR != 0`, `p_L = p_R`): matches the reference | `<= 32 eps * scale[i]` |
| T3 | **tangential shear** (`u_n = 0`, `p` equal, `U_t` differs): mass flux and **tangential** momentum flux exactly 0 | **BITWISE** |
| T4 | no spurious pressure or velocity jump is introduced at a contact | reference agreement, as T1/T2 |

```text
DERIVATION OF THE BITWISE CLAIM IN T1. With p_L = p_R the contact numerator's pressure term is
exactly 0, and with u_nL = u_nR = 0 both velocity terms are exactly 0, so num = 0 and S* = 0/denom = 0
EXACTLY. Then rho*_L = rho_L (S_L - 0)/(S_L - 0) = rho_L (S_L/S_L) = rho_L exactly, so Q*_L = Q_L in
mass, and the mass flux is F_L^mass + S_L (rho*_L - rho_L) = 0 + S_L * 0 = exactly 0. Bitwise is
therefore the CORRECT requirement here and any tolerance would be a concession.
```

## 8. Groups R and N — rotation G6(d) and conservation G6(c) (§19, §20, §33)

| id | criterion | threshold |
| --- | --- | --- |
| R1 | rotating state and normal together by a **dyadic** angle (90/180/270 deg about an axis) gives exactly the rotated flux | **BITWISE** — G6(d) requires this explicitly |
| R2 | the same for the oblique normal `(2,-3,6)/7` under a general rotation | `<= 32 eps * scale[i]` |
| R3 | scaling the face **area** scales the integrated flux **linearly** and leaves the wave speeds **unchanged** | flux linear to **BITWISE** for a power-of-two area; wave speeds **BITWISE** identical |
| N1 | `F(Q_L,Q_R,n) == -F(Q_R,Q_L,-n)`, all 5 components | **BITWISE** — DERIVED below |

```text
DERIVATION OF THE BITWISE CLAIM IN N1. Under the swap, negating a double is exact, and min/max of
negated arguments maps exactly onto max/min, so S_L' = -S_R and S_R' = -S_L bitwise. Then
q_L' = -q_R and q_R' = -q_L, giving denom' = q_L' - q_R' = -q_R + q_L = denom bitwise, and
num' = (p_L - p_R) + q_L' u_nL' - q_R' u_nR' = -(p_R - p_L) + q_R u_nR - q_L u_nL = -num with the
SAME operation order. So S*' = -S* bitwise, and every downstream term carries a single exact sign
flip. BITWISE is the correct requirement, not round-off.
```

## 9. Group X — the independent exact solver, G6(f) (§17, §18)

The reference is `tools/exact_riemann.py`: **mpmath at 60 decimal digits, containing no CFDApp code**,
as CLAUDE.md §6 and G6(f) require. Adopting CFDApp output as the reference is forbidden and is not
done. The script computes two independent things and the gate keeps them apart.

| id | criterion | threshold |
| --- | --- | --- |
| X1 | HLLC equals the **exact Godunov flux** for the cases where it is algebraically exact — uniform (x and oblique), stationary contact, moving contact, supersonic left and right, near-sonic, strong density ratio | `<= 32 eps * scale[i]`. The reference gives **exactly 0** for all of these (`7.97e-65` for the one case where 60-digit round-off shows) |
| X2 | HLLC matches the **60-digit HLLC** for every case, including the strong discontinuities | `<= 32 eps * scale[i]` — this measures IMPLEMENTATION error only |
| X3 | Sod (literature and atmospheric), strong pressure ratio, low pressure: `S_L`, `S*`, `S_R`, `rho*_L`, `rho*_R`, the branch and the full flux vector are **REPORTED**, and gated only on X2, W2, W4, S1–S3 and finiteness | see below |
| X4 | Sod initial state: `S_L < S* < S_R`, both star densities positive, energy flux finite | boolean |

```text
WHY X3 CARRIES NO ACCURACY TOLERANCE, and why that is the honest reading of G6(f).
G6(f) says the tolerance must be "derived from the wave-speed estimator's own truncation behaviour,
not from the observed error". Measured against the exact solution at 60 digits, the estimator's
truncation behaviour for a strong discontinuity is O(1):
      Sod literature        |diff|/scale = 0.0061 mass, 0.0609 momentum, 0.2961 energy
      strong pressure ratio                0.0768        0.1026           0.3177
      low pressure                         0.0301        0.0617           0.2565
A single HLLC interface flux is simply NOT close to the exact Godunov flux for a strong jump, and no
tolerance derived from the estimator would be tight. Inventing one fitted to 0.0061 would be exactly
the "derive from the observed error" that G6(f) forbids.
So the DERIVABLE parts of (f) are gated -- X1 (where HLLC is exact), X2 (implementation error), and
W4 (the estimator must BOUND the true signal speeds, which is the actual correctness requirement on
it) -- and the scheme's approximation error is REPORTED as a characterisation. Claiming Sod "passes"
on flux accuracy is explicitly NOT done; §18 also forbids it, and full Sod validation is P17.9's.
```

## 10. Group P — invalid and extreme states (§8, §31, §32)

| id | criterion | threshold |
| --- | --- | --- |
| P1 | throws `InvalidArgumentError` for `rho <= 0`, `p <= 0`, NaN/Inf in `rho`, `p`, `U`, `rhoE`, non-finite sound speed | throws, every case |
| P2 | throws for an invalid normal: zero length, non-unit length, NaN component | throws |
| P3 | the error message **names the offending quantity** | substring match |
| P4 | extreme but valid states (pressure ratio 1e6, density ratio 1e6, Mach ~ 2.4, `p` down to 1e-3) produce finite flux and admissible star states | boolean |
| P5 | **no clamping, no energy floor, no silent fallback** anywhere in the layer | 0 occurrences of a clamp/floor idiom; S5's counters exactly 0 |

## 11. Group G — CPU/GPU, G8 (§27, §28, §29, §30)

| id | criterion | threshold |
| --- | --- | --- |
| G1 | the device kernel calls the **same** `CFD_HOST_DEVICE` functions — one source, no duplicated math | structural: 0 re-expressed formulas in the `.cu` |
| G2 | CPU vs GPU over the §29 matrix: `S_L`, `S*`, `S_R`, branch, `rho*_K`, all 5 flux components | **BITWISE**, built with `--fmad=false` |
| G3 | the same comparison with FMA contraction **on**, reported as characterisation | reported, not gated |
| G4 | determinism: repeated evaluation of identical inputs, CPU and GPU independently | **BITWISE**, including branch and counters |

```text
G8 says "where the operation order is identical, BITWISE is the correct requirement and anything
looser is a concession that must be argued for". One shared source makes the order identical, so
BITWISE is demanded. The single obstacle is FMA contraction, which nvcc applies by default and the
host x86-64 build does not; `--fmad=false` on this one kernel removes it. That is a compile flag on
new code, not a change to shared semantics, and G3 quantifies what the flag is worth.
NOTE ON SCOPE: P17_GATES.md marks G8 vacuous and says GPU compressible work is outside P17 scope.
The P17.7 authorization (§27-§29, §42) requires it, and is newer and more specific, so it governs
this item. Flagged in audit.md §5 rather than silently overridden.
```

## 12. Group E — preservation, G1/G10 (§34, §35, §39)

| id | criterion | threshold |
| --- | --- | --- |
| E1 | P13-001 SIMPLE fingerprint | **BITWISE IDENTICAL**, 0-byte diff |
| E2 | every upstream file UNTOUCHED, by **SHA-256** (never `git diff HEAD`) | 0 TOUCHED |
| E3 | `CompressibleSIMPLE` is **not** routed through HLLC; the two architectures coexist | 0 references to HLLC from any SIMPLE path |
| E4 | full regression, Release+CUDA and CPU-only | counts match the prediction, 0 failed |
| E5 | clang-format clean on every file P17.7 touches | exit 0 |
| E6 | P14 / P15 / P16 / P17.1–P17.6 suites at their frozen counts | exact |

## 13. Group V — non-vacuity controls

Each must **FAIL** the named criteria and be restored with SHA-256 verification.

| id | planted defect | must break | must still pass |
| --- | --- | --- | --- |
| V1 | wave speeds switched to **Davis (10.48)** | **W4** in >= 5 cases | C1/C2 (consistency), N1, R1 — this is the measured 8-of-14 failure |
| V2 | the **contact wave deleted** (HLLC -> HLL) | **T1** (spurious mass flux ~243 kg/(m^2 s) where the truth is 0), **T2**, **X1** | **C1/C2** — G6's own stated non-vacuity requirement |
| V3 | tangential velocity taken from the **opposite** side in the star state | T3, R2, X1 | C1/C2 |
| V4 | `S*` replaced by `0.5(u_nL + u_nR)` | X1 on the moving contact, T2 | C1/C2 on a symmetric state |
| V5 | the `p n` term dropped from `eulerFlux` | F1, F3, C1, T1 | nothing — a total failure is expected |
| V6 | the normal used **unnormalised** (area folded into it) | R3, W1 | C1 when the area is 1 |
| V7 | star density denominator `(S_K - S*)` replaced by `(S_K - u_nK)` | S4, X1, T2 | C1/C2 |

```text
V1 and V2 are the backbone. V2 is named by the frozen G6 criterion itself; V1 exists because the
authorization's own suggested estimator is the one that fails, which makes it the strongest available
control -- a defect that a careless reading of the scope would have shipped.
```

## 14. Execution policy — FROZEN, inherited verbatim

```text
OMP_NUM_THREADS=1 ctest -j 8 --timeout 7200
5 exclusion patterns, 13 tests, unchanged since P13's G10 closeout:
  SSTAutomaticWallChannel|SSTCompositeWallChannel|SSTMomentumFluxChannel|
  WallArchitectureDiagnostic|DriverSeegmiller\.CorrectedMeshSequence
build trees: build/qual-final (Release+CUDA+OpenMP), build/qual-cpuonly (CUDA OFF)
build/qual-debug (Debug+GUI) is NOT run -- deferred to P17.11 by standing decision
```

## 15. Count predictions

```text
baseline, the P17.6 CLOSE, measured:
  Release+CUDA   2657 registered / 2595 executed
  CPU-only       2455 registered / 2395 executed
The delta is declared in logs/02_freeze.log once the test count is fixed, and is pure arithmetic:
a test count is a consequence of the suite, never a tolerance.
```

## 16. Failure policy

```text
STOP at the first failing criterion. Preserve the log. Diagnose. Fix the CODE, never the criterion.
G6 failure means: DO NOT WIRE THE FLUX TO A SOLVER (the frozen gate's own instruction).
No threshold in this document may be changed to obtain a PASS. Post-freeze corrections go in an
AMENDMENT_*.md with the reason and the measurement, and the frozen file is left untouched.
```

## 17. Coverage map

A criterion -> test -> "is it actually asserted?" map is required, in `coverage_map.md`, kept OUTSIDE
this frozen file so completing it cannot move this hash. P17.5 embedded a `TO BE FILLED` table in its
frozen gate and could not complete it; P17.6 found a criterion (A3) listed with a test name that did
not assert it. The "asserted?" column exists because of that.
