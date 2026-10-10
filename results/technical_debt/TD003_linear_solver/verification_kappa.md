# TD-003 — the kappa measurement REFUTES my own mechanism, and reframes criterion 2

```text
STATUS:      **MY MECHANISM IS REFUTED IN MAGNITUDE.** kappa for the flame's species matrix is
             **26.28**, not the 1000 my audit assumed -- wrong by a factor of 38. kappa x
             residual = 2.63e-08 against an observed 8.823e-07, a **33.6x shortfall**. The 12 %
             "agreement" in audit.md §3.4 was coincidence, exactly as that section refused to
             bank it.
             THE DIRECTION SURVIVES: 2.63e-08 still exceeds the 1e-8 gate by 2.6x, so tolerance
             alone cannot reach the gate. Required residual <= 3.8e-10, not the 1e-11 I claimed.
             AND CRITERION 2 IS REFRAMED: **neither the flame nor the production reacting case
             is in the r > 1 regime my probe characterised.** Both are strictly diagonally
             dominant. The likely real fix is to wire an EXISTING policy, not to build a solver.
SCOPE:       the one measurement audit.md §5 named as unblocking. No production change, no test
             change, no gate frozen. Supersedes audit.md §3.4 going forward (§4 of this file).
INSTRUMENT:  an exact Thomas-algorithm inverse of the reconstructed matrix -- 500 tridiagonal
             solves against unit vectors, giving ||A^-1||_inf with no iteration and no tolerance.
             FIDELITY CHECK: the reconstruction reproduces the solver's own time step to
             5e-05 relative (9.4248e-06 against the recorded 9.4243e-06) -- §1.1.
EVIDENCE:    this file. Every input is a cited constant and the computation is reproducible
             from them; no solver was run.
COMMIT:      carried as evidence.
```

---

## 1. THE MEASUREMENT

Reconstructed from the fixture's own constants — `kConductivity` 0.05, `kSpecificHeat` 1200,
`kWeight` 0.029, `kPressure` 101325, `lewisNumber` 1.0 (`PremixedFlame.hpp:178`),
`domainLength` 0.010 with 500 cells, and `maximumCellDiffusionNumber` 5.0:

```text
dx                2.0000e-05 m
rho_u / rho_b     1.1781 / 0.19635 kg/m^3
alphaMax          2.1221e-04 m^2/s        (:478, burnt side)
dt                9.4248e-06 s            (:718 diffusion limit)
F = rho_u S_L     0.05838 kg/(m^2 s)      at the measured S_L = 4.95533 cm/s
gamma = lam/(cp Le) 4.1667e-05            (:1061)
D = gamma/dx      2.0833
```

The species matrix is tridiagonal: `a_W = -(D+F)`, `a_E = -D`, `a_P = rho_i V/dt + F + 2D`.
The chemistry's Patankar `Sp` is set to ZERO here, which makes `a_P` smaller and therefore
**overstates** kappa — the conservative direction for testing my claim.

```text
dominance a_P/(|a_W|+|a_E|)   cell 0  3.228    mid  1.099    last  1.195
WEAKEST interior dominance            1.0986   -> STRICTLY diagonally dominant

||A||_inf       10.9500
||A^-1||_inf     2.400000
kappa_inf       26.280
```

### 1.1 The reconstruction is faithful, and this is how that is known

`dt` is not an input here — it is *derived* from `maximumCellDiffusionNumber * dx^2 / alphaMax`,
through `alphaMax = lambda(T_ad)/(rho_burnt * cp)` and `rho_burnt = pW/(R T_ad)`. It comes out
**9.4248e-06 s** against the **9.4243e-06** on record from the solver itself: agreement to
`5e-05` relative, through four chained constants. A reconstruction that got the mesh, the
densities, the diffusivity or the stability rule wrong would not land there.

**That is the check, and it is not proof the assembled matrix is identical.** See §5.

## 2. THE REFUTATION

```text
kappa x 1e-9                     2.628e-08
OBSERVED |sum Y - 1|             8.823e-07
SHORTFALL                        33.6x

audit.md §3.4 assumed kappa = 2N = 1000  ->  1.0e-06, "12 % agreement"
measured                         kappa = 26.280        ->  2.63e-08
the assumed value was too large by a factor of 38
```

My audit assumed `r = 1` and applied `kappa = 2n`, a law derived for a **bidiagonal** matrix, to
the flame's **tridiagonal** one. §3.4 named both premises as unverified and declined to bank the
agreement. Both were wrong, and the agreement was coincidence.

### 2.1 What survives, and it is not nothing

```text
DIRECTION SURVIVES   2.63e-08 still EXCEEDS the 1e-8 gate by 2.6x. So even with the correct
                     kappa, a 1e-9 accepted residual cannot deliver |sum Y - 1| < 1e-8.
                     Criterion 3 does require a tolerance change.
MAGNITUDE CHANGES    the requirement is a residual <= 1e-8 / 26.28 = 3.8e-10, which is 2.6x
                     tighter than today's 1e-9 -- NOT the 100x to 1e-11 the audit claimed.
                     The relativeTolerance of 1e-11 at :625 is far more than needed.
AND 33.6x IS LEFT    the linear-solve error accounts for about 3 % of the observed deviation.
                     A DOMINANT SOURCE EXISTS AND IS NOT IDENTIFIED.
```

### 2.2 A candidate for the missing 33.6x — NOT established, stated so it can be tested

`sum_i Y_i = 1` needs the N species systems to sum to the mixture continuity equation
(`:615`-`:619`). They share one matrix at `Le = 1` (`:1061`-`:1064` assigns the same `gamma` to
every species), so `A (sum_i Y_i) = sum_i b_i`, and the invariant holds only if `sum_i b_i` is
exactly the right-hand side of `sum Y = 1`. The transient and inlet parts sum correctly by
construction. **The source term does not obviously:** it requires `sum_i (Su_i + Sp_i Y_i) = 0`,
i.e. the Patankar split to preserve `sum_i omega_i = 0` species-by-species, not merely in the
mechanism's stoichiometry.

**This is a hypothesis and nothing here measures it.** The test is cheap and direct — sum the
assembled `Su_i` and `Sp_i` across species per cell and check against zero — and it dumps the raw
quantity rather than adding another derived counter, which is what this campaign's rule asks for
after a refutation.

## 3. CRITERION 2 IS REFRAMED, and my probe characterised a regime nothing runs in

```text
                            dominance a_P/(|a_W|+|a_E|)   cell Peclet F/D
flame species matrix                 1.0986                    0.028
production reacting case            10.92                    250.0
```

Both are **strictly diagonally dominant**, for different reasons: the flame because diffusion
dominates convection (`2D/F = 71`), the production case because its transient term dominates
convection 10x (`rho V/dt = 2.5e-02` against `F = 2.5e-03`). The case's numbers come from its own
files — `physics.json` density 1.0 and `diffusivity` 1.0e-5, `boundaries.json` inlet 0.05 m/s,
`mesh.json` 20x4 over `geometry.json`'s 1.0 x 0.2 m, `time_step` 0.1.

**Neither is in the `r > 1` regime where my probe found BiCGSTAB breaking down.** At `r <= 1` the
probe shows BiCGSTAB converging (19-311 iterations) with true errors near 1e-11. So criterion 2's
characterisation in `audit.md` §2 is correct mathematics about a class that the reacting path does
not appear to enter.

### 3.1 The P18.7 diagnosis was a different setup, and a different failure mode

```text
its log: 10 cells, cell volume 2.000000e-01, diag0 = 2.0000
rho V/dt with rho = 1, V = 0.2, dt = 0.1  =  2.0   -> the printed diagonal is the TRANSIENT
                                                      term alone
the committed case has 80 cells and V = 2.5e-03, so that log is NOT this case
and 'it 1 species: status=2 iters=2' is a BREAKDOWN AT ITERATION 2
```

A breakdown after two iterations is the `(rHat0, v) == 0` structural breakdown, which
`test_bicgstab_breakdown.cpp` already treats as a phenomenon **distinct from ill-conditioning** —
its dyadic 3x3 reproducers break down exactly, at full precision, on well-conditioned matrices. So
the originating failure is plausibly structural, not a conditioning cliff.

### 3.2 THE LIKELY REAL FIX: a policy that already exists and is not wired in

```text
src/algebra/LinearSolverFallback.cpp:106   isFallbackEligible() is TRUE for exactly
                                           SolverStatus::Breakdown (and NonFiniteResidual)
                                      :115 fallbackCandidates(BiCGSTAB, ...) -> {GMRES}
wired into                                 SIMPLE.cpp, CompressibleSIMPLE.cpp
NOT wired into                             the reacting path -- ProjectRunner.cpp:816
                                           constructs a BARE `BiCGSTAB reactingLinearSolver`
```

**The repository already has the escalation for the exact status P18.7 recorded, and the reacting
path is the one path that does not use it.** My probe supports GMRES as an adequate escalation
target *for these matrices*: at `r <= 1` GMRES converged in every case. It is NOT adequate in
general — at `r = 1.05` it stagnated at 5000 iterations — but both real matrices are at `r <= 1`.

So criterion 2 may be satisfiable by wiring an existing policy rather than by building the direct
triangular solver `audit.md` §2.2 called for. That is a materially cheaper and more
architecture-consistent answer, and it is **not yet demonstrated**.

## 4. WHAT THIS SUPERSEDES

`audit.md` is committed (0654a96), so it is corrected forward, not rewritten:

```text
audit.md §3.4   its CAVEAT is upheld and its NUMBER is withdrawn. kappa = 26.28, not 1000;
                the 12 % agreement was coincidence. §3.4 said it "may be coincidence" and
                declined to bank it, which is why nothing downstream depended on it.
audit.md §3.3   the required tolerance is 3.8e-10, not 1e-11. The claim that :625's 1e-11 is
                "exactly what the gate needs" is WITHDRAWN -- it is 26x more than needed.
audit.md §2     unchanged as mathematics, but its RELEVANCE is narrowed by §3 here: no
                reacting matrix measured so far is in the regime it characterises.
audit.md §5     its first NOT-measured item ("the FLAME's own kappa") is now MEASURED.
```

## 5. WHAT IS NOT ESTABLISHED

```text
NOT established  that the reconstructed matrix is the assembled one. §1.1's dt agreement to
                 5e-05 constrains the inputs, not the assembler. The decisive version calls
                 `speciesTransportAssembly` (a free function, SpeciesTransport.hpp:175) or
                 dumps the matrix from an instrumented copy outside the repo. kappa is small
                 and the dominance margin is wide (1.0986), so the CONCLUSION is robust to a
                 modest error here -- a 33.6x shortfall does not close from a factor of 1.1.
NOT established  the missing 33.6x. §2.2 is a hypothesis with a named cheap test, nothing more.
NOT established  that Sp = 0 is conservative in the way claimed. Chemistry's Patankar sink
                 raises a_P and should lower kappa further, but a sink that varies by orders of
                 magnitude across the front could instead worsen the dominance in some cell.
                 Not measured.
NOT established  that wiring LinearSolverFallback into the reacting path fixes anything. §3.2
                 shows the mechanism exists, matches the status, and is unused there. It does
                 not show it recovers the P18.7 failure, which would need that failure
                 reproduced first -- and §3.1 shows the recorded setup is not the committed case.
NOT established  that r <= 1 holds for the reacting path at ALL states. Two matrices were
                 computed at one state each; the flame's front moves and its densities change.
NOT reproduced   the P18.7 breakdown itself. Its setup is not the committed case (§3.1) and no
                 run in this measurement attempted it.
NOT measured     the flame's ACTUAL accepted residual per solve. TD-003 records
                 worstLinearResidual "pinned at exactly 1e-9"; that is carried forward here,
                 and §2's arithmetic depends on it.
```
