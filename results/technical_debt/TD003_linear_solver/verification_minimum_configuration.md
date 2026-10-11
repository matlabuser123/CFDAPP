# TD-003 C3 — the `9.55e-08` minimum came from a 125-cell flame whose own consumption integral is off by 51 %. OPTION B'S PREMISE IS REFUTED, and it was my own argument.

```text
STATUS:      **MY COUNTER-ARGUMENT TO MY OWN RECOMMENDATION IS REFUTED.**
             `verification_gate_provenance.md` section 5 recorded a wrinkle: the historical
             minimum `9.54994e-08` sits 6.8x BELOW the derived floor, so "the floor is NOT
             immovable ... OPTION B is NOT futile". **That reading assumed the minimum came
             from a better-conditioned configuration. It did not.**
             It came from `TheFlameSpeedConvergesUnderMeshRefinement` at **125 cells** -- a
             state whose consumption integral disagrees with its own eigenvalue by **51 %**
             (`S_L = 4.40773`, `S_c = 2.15465`). It is not a better-conditioned flame. It is a
             WORSE SOLUTION on a coarser mesh, and it cannot be cited as evidence that `1e-8`
             is reachable at a resolution that resolves the flame.
             **So Option B's premise is refuted and Option A stands unopposed.**
             I also WITHDRAW a number I derived on the way: the "N ~ 23 cells" extrapolation.
             Section 2.2.
SCOPE:        step 2 of the agreed plan -- identify the configuration behind the minimum. No
              production change, no test change, no threshold change, NO RUN. Log reading only.
INSTRUMENT:  `results/technical_debt/TD005_flame_speed_extraction/logs/03_tests_at_repo_cap.log`
             lines 111+, an existing committed log. Nothing was executed.
EVIDENCE:    this file.
COMMIT:      carried as evidence.
```

---

## 1. WHERE THE MINIMUM CAME FROM

`TD005_flame_speed_extraction/logs/03_tests_at_repo_cap.log`, inside
`G8PremixedFlameTest.TheFlameSpeedConvergesUnderMeshRefinement` — a mesh series, not a
conditioning study:

```text
 cells       S_L       S_c    mismatch   delta mm       |sumY-1|
   125   4.40773   2.15465      51.1 %     0.6256   9.549940e-08   <- THE MINIMUM
   250   4.77424   2.16857      54.6 %     0.5367   2.461000e-07
   500   4.86230   4.90004       0.8 %     1.2289   5.964110e-07
```

### 1.1 The two coarse levels are not flames

A steady flame satisfies `S_c = S_L`: the consumption integral must reproduce the eigenvalue.
At 125 and 250 cells it is out by **51 %** and **55 %**. The 125-cell entry's own log line also
reads `fuel balance = 0.499892` and `final march 334 (NOT steady)`, against `~0.006` and `15283`
for the converged fixture.

And the front thickness **jumps** `0.537 -> 1.229 mm` between 250 and 500 cells, so these are not
the same solution sampled on three meshes — the coarse ones are a different, unresolved state.
Only the 500-cell level is self-consistent, at `0.78 %`.

## 2. SO MY WRINKLE WAS WRONG

```text
WHAT I WROTE   "the floor is NOT immovable. Some recorded configuration reached 9.55e-08,
               within 9.5x of the 1e-8 bound. So an amplification roughly 10x smaller would
               put the bound within reach, which means OPTION B ... is NOT futile."
WHAT IS TRUE   that configuration is a 125-cell flame whose consumption integral is off by a
               factor of two. Its smaller |sum Y - 1| is not a better-conditioned operator --
               it is fewer cells carrying an unresolved, non-steady state.
```

**The premise of Option B was that a well-conditioned configuration exists at usable resolution.
No recorded measurement supports that.** The deviation is smallest exactly where the solution is
worst.

### 2.1 Option B is now in direct conflict with the fixture's own requirement

The fixture derives its mesh in its own comment: the reaction zone is `~0.09 mm`, so
`dx <= delta_reaction/4 = 0.022 mm -> 0.02 mm`, which is what fixes 500 cells over 10 mm.
Reducing the amplification by coarsening is therefore not a trade-off between two goods — it
breaks the resolution the gate exists to measure.

That does not kill Option B outright: a *reformulation* that keeps the operator diagonally
dominant at the final march while holding 500 cells would reduce the amplification honestly. But
no evidence in the repository shows that is achievable, and the datapoint I offered as evidence
for it says nothing of the kind.

### 2.2 AND I WITHDRAW A NUMBER I DERIVED FROM THIS SERIES

While measuring, I computed refinement ratios of `2.577` and `2.423` — exponents `1.366` and
`1.277` — and extrapolated backward to "1e-8 would be met at N ~ 23 cells".

**That extrapolation is WITHDRAWN.** CLAUDE.md section 6 permits an observed-order claim only if
the quantity is iteratively converged, the mesh family is self-similar, and the triplet is in the
asymptotic range. **None of the three holds here**: two of the levels are 51-55 % inconsistent and
the thickness jumps by 2.3x. Extrapolating an exponent from a non-asymptotic series outside its
measured range is a mistake this campaign has already made once, and I am not repeating it.

```text
KEPT       the three measured values and the two ratios, as MEASUREMENTS.
WITHDRAWN  the exponent as an "order", and the N ~ 23 extrapolation built on it.
UNAFFECTED the conclusion in section 2, which needs only "the minimum is at the coarsest,
           least converged level" -- a fact readable directly off the table.
```

## 3. WHAT THIS GIVES OPTION A

A simple `C * eps * N_cells` form -- the shape G3 uses -- does **not** fit:

```text
N=125   |sumY-1| / (eps*N) = 3.441e+06
N=250   |sumY-1| / (eps*N) = 4.433e+06
N=500   |sumY-1| / (eps*N) = 5.372e+06
```

The quotient rises, so the mesh dependence is steeper than linear. **That argues for stating the
bound as `C * eps * ||b||_2 * ||A^-1||_inf` with both factors measured per state**, rather than as
any power of `N`: that form is the derivation (`error <= ||A^-1|| * ||r||`, with the RHS closing to
`eps*||b||`), and it carries the mesh dependence automatically instead of fitting it.

**This is a form, not a constant.** Section 5.3 forbids deriving the replacement threshold from
observed output, so `C` must come from counting the summed terms in the assembly, and the
amendment must then be dry-run.

## 4. THE DECISION, AS IT NOW STANDS

```text
OPTION A   amend to C * eps * ||b|| * ||A^-1||_inf, derived and dry-run per section 5.3.
           RECOMMENDED, and now unopposed by evidence.
OPTION B   its premise -- that a better-conditioned configuration exists at usable resolution --
           is REFUTED by section 2. It survives only as "reformulate the final march", for
           which there is no supporting evidence and which would move every recorded S_L.
OPTION C   enforce sum Y = 1 explicitly. Still FORBIDDEN by the project's no-clipping policy.
```

Nothing was amended. Section 5.3 step 4 — authorization — remains the user's.

## 5. WHAT IS NOT ESTABLISHED

```text
NOT established  that a reformulation keeping 500 cells AND a dominant operator is impossible.
                 Section 2.1 says only that no evidence supports it and that the datapoint I
                 cited does not.
NOT established  the correct C for Option A. Section 3 gives the FORM; section 5.3 requires C to
                 be derived from the assembly's term count, not read off these three numbers.
NOT established  that |sum Y - 1| scales as any particular power of N. Section 2.2 withdraws
                 that claim; the three values are recorded as measurements only.
NOT re-measured  this series. It comes from a committed log on an EARLIER tree -- its 500-cell
                 value is 5.96e-07 where the current tree gives 8.82e-07. The comparison used
                 here is WITHIN that one run, so the tree difference does not affect it, but
                 no level was re-run.
NOT examined     why the 125- and 250-cell levels are 51-55 % inconsistent. That is a
                 resolution finding about TheFlameSpeedConvergesUnderMeshRefinement, not about
                 TD-003, and it is out of scope here. It may be worth its own item: a test
                 named "converges under mesh refinement" whose two coarse levels are not
                 converged states is at least oddly calibrated.
```
