# P18.2 — Species Transport & Diffusion — SUMMARY

```text
P18.2 STATUS: COMPLETE

GATE        results/p18/gates/P18_GATES.md
            sha256 921616544e3631d40de3ea629a1617e8ec014f89fcb1d7b8a728db7132d618f3
            re-verified UNCHANGED at open and at close. No threshold in it was edited.
G4          PASS, all four criteria (a) (b) (c) (d) -- transport / diffusion correctness.
G3          EVIDENCE TOWARD. Per-species and mixture balances close; G3's phase is P18.7.
G1          PASS, the regression half. Both Release configurations reproduce the baseline exactly.
G2          PRESERVED THROUGH TRANSPORT -- new evidence for it, not a re-claim.
G10         the assembly/closure layer only, bitwise. NOT the solver-level claim.
NOT CLAIMED G5 G6 G7 G8 G9 G11 -- none measurable from non-reacting transport (section 40).

AMENDMENT   diffusion_closure_A1.md, frozen with its own dry-run and non-vacuity checks.
            diffusion_closure.md was AMENDED, NEVER EDITED: its sha256
            3ee2221d39e7805e0679df4b679aa55af810de7e930c71bab0909be589ae88dd is the one A1
            recorded before implementation and is unchanged at close.
TESTS       131 / 131 in CFDSpeciesTests. 34 new.
```

---

## 1. Formulation

```text
transported quantity   rho Y_i (conservative). The solved unknown stays Y_i, because that is what
                       P18.1's invariants are stated on and what boundaries prescribe.
transient term         a_P = rho^{n+1} V/dt  ·  b_P = rho^n Y^n V/dt
                       verified against HAND ARITHMETIC, not the code's own output; the
                       rho^{n+1} Y^n error is a factor of 2.5 away and so is detectable (N1).
convective flux        mdot_f Y_{i,f}, first-order upwind, on the AUTHORITATIVE mass flux.
                       P18.2 forms no rho_f, no U_f, no mdot_f -- 0 flux-forming calls, verified.
diffusion model        J_i^0 = -rho D_i grad(Y_i), assembled by P14 with Gamma_i = rho D_i per cell
                       D_i [m^2/s] · rho [kg/m^3] · Gamma_i [kg/(m s)] · J_i [kg/(m^2 s)]
diffusion correction   J_i = J_i^0 - Y_i sum_j J_j^0, assembled IMPLICITLY as convection by the
                       correction velocity (amendment A1)
source                 omega_i = 0, an explicit and verifiably exact zero
```

## 2. Diffusion closure

```text
species diffusivities  per-species Gamma_i = rho D_i. J_B/J_A = -(D_B/D_A) EXACTLY, so each species
                       provably receives its own D_i.
raw J_i                genuinely UNCLOSED with unequal D_i -- asserted, so the correction is not
                       credited for nothing (NV1).
corrected J_i          sum_i J_i = 0 by an algebraic identity conditional only on sum_i Y_i = 1.
max |sum_i J_i|        6.7762635780344027e-21   against tolerance 1.7540254962763639e-19
                       (8 eps N_species max|J^0|, derived before measurement)
species sum of the     4.2012834183813297e-19   with the closure
assembled system       4.3200000000000047e-05   WITHOUT it -- a ratio of 9.7e-15
                       for scale, max_f |Sigma_f| = 3.7028571428571485e-05
uniform composition    J_i = 0 EXACTLY, even with D_i != D_j
```

## 3. State preservation

```text
max |sum Y_i - 1|   per step   2.886579864025407e-15   bound 1.7763568394002505e-14
                    20 steps   4.7073456244106637e-14  bound 3.5527136788005009e-13
minimum Y_i         >= 0, reported not imposed
maximum Y_i         <= 1, reported not imposed
clipping used       NO -- 0 clamp/clip in the source
normalization used  NO -- normalizeMassFractions is called 0 times on any path
```

```text
THE PER-STEP BOUND IS DERIVED, AND IT IS NOT G2(a). G2(a) bounds the round-off of summing N doubles;
one implicit step changes sum(Y) mainly through N INDEPENDENT LINEAR SOLVES, and a solver tolerance
bounds the RESIDUAL, not the error (A e = r, so e carries ||A^-1||).

    perStepSumBound(N, tau) = 4 eps N  +  8 N tau

The 8 is INHERITED from P17.8's conservation floor, which G3 cites verbatim. The measured ratio was
1.65; adopting that would have been fitting a constant to an observation.

PROVEN to track the mechanism rather than asserted:
    tau = 1e-15  ->  2.886579864025407e-15   inside its bound
    tau = 1e-08  ->  3.290559935642534e-08   BREACHES the tight bound by 6 orders, inside its own
```

## 4. Conservation

```text
closed 6x6 enclosure, rho = 1.2, D = {1e-4, 4e-4}, dt = 0.5, 20 steps, 2 species
  per-species relative drift    within 8 eps N_cells (G3's frozen form) for BOTH species
  sum_i M_i vs mixture mass     within 8 eps N_cells, the mixture mass computed independently
                                from rho alone
variable density, rho = 0.8 + 1.4x (ratio 2.75, asserted non-vacuous)
  per-species relative drift    within the same bound -- so rho*Y, not Y, is what is conserved
open domain, steady, inlet 0.3 / 0.7
  accumulation · convective in · convective out · diffusive boundary · source
  reported as FIVE SEPARATE NUMBERS (section 12 forbids one residual); imbalance <= 1e-10 relative
```

## 5. Boundary treatment

```text
fixed composition    Dirichlet per species; the cases prescribe complements summing to EXACTLY 1,
                     which the closure needs at a Dirichlet face
zero gradient        J_i . n EXACTLY 0 for every species at every boundary face
fixed flux           NOT SUPPORTED -- no such boundary type exists. Recorded as absent capability,
                     not as a passing criterion.
inlet                composition prescribed and valid; no reliance on downstream normalisation
outlet               zero-gradient: zero diffusive contribution, convective flux on the
                     authoritative mass flux, no artificial source
wall                 impermeable: mdot_f = 0 and J_i . n = 0, both exactly
BOUNDARY LAG         FIXED on the species path. G4(c): <= 2 outer iterations against P18.0's 335.
```

## 6. Canonical validation

```text
pure advection          mass conserved within 8 eps N_cells, bounds held, sum(Y) within the derived
                        bound. Zero numerical diffusion NOT required (section 25).
pure diffusion          G4(a) 1D slab against the analytical linear profile, 3 grids (8/16/32),
                        l2 and linf < 1e-6 -- tolerances INHERITED VERBATIM from
                        tests/integration/species/test_species_diffusion.cpp. Plus a non-vacuity
                        control proving the same norm rejects a wrong profile.
convection-diffusion    manufactured solution Y = 0.5 + 0.3 cos(pi x), exercising transient,
                        convection and diffusion, with S_B = -S_A so the MMS source cannot itself
                        drive sum(Y) off 1. The MMS source is labelled a MATHEMATICAL forcing,
                        NOT omega_i, which stays 0.
                        Criterion: the OBSERVED ORDER on 16->32->64, >= 0.9, derived because P14's
                        convection is first-order upwind and an absolute tolerance would be
                        meaningless. Errors decrease monotonically -- the precondition for an order
                        to mean anything -- and both orders meet it.
variable-density        transient case with rho(x); a solver advancing only Y fails it (N1)
```

## 7. Negative controls

```text
N1 advance Y not rho Y        DETECTED -- the transient RHS scales by exactly the density ratio 1.10
N2 rho out of the mass flux   COVERED BY CONSTRUCTION -- P18.2 forms no mass flux at all (0 calls)
N3 wrong diffusion sign       DETECTED -- AROSE AS A REAL DEFECT, caught at 1.29e-3
N4 one diffusivity for all    DETECTED -- Sigma_f 1e-8-scale -> <1e-18; corrected fluxes differ
N5 remove the closure         DETECTED -- species sum of the assembled system moves by 1e14
N6 reintroduce the lag        DETECTED -- G4(c) fails; a PERMANENT test on the ITERATION COUNT
N7 omit a boundary flux       DETECTED -- the open-domain balance reports each term separately
N8 sum(Y) drift unnoticed     DETECTED -- and the drift SCALES with the solver tolerance
```

Two arose as real defects rather than as written controls, which is the strongest non-vacuity
available: the criterion caught the mistake before a control was written for it.

## 8. Determinism

```text
repeat count               20
field difference           raw and corrected face fluxes BITWISE identical
flux difference            BITWISE identical
assembled diagonal / RHS   BITWISE identical
conservation difference    the inner iteration reaches the same fixed point in 5 passes
SCOPE                      assembly and closure only. G10's solver-level, cross-process claim is
                           P18.8/P18.9's and is NOT made.
```

## 9. Regression

```text
focused P18.2   131 / 131 passed (34 new · 60 P18.1 · 37 pre-P18)
Release+CUDA    2857 selected · 2806 Passed · 0 FAILED · 2 Skipped · 49 DISABLED · 2121 s · exit 0
CPU-only        2634 selected · 2587 Passed · 0 FAILED · 0 Skipped · 47 DISABLED · 1455 s · exit 0
Debug+GUI       2919 selected · 2864 Passed · 4 FAILED · 2 Skipped · 49 DISABLED · 8670 s · exit 8
P14             transport 22/22 · characterization 3/3 · migration 3/3
P15             thermal 163/163
P16             turbulence 246/246
P17             carried inside the three full configurations; no new failure anywhere
```

```text
BOTH RELEASE CONFIGURATIONS ARE EXACT: +34 selected, +34 Passed, nothing else moved.

DEBUG+GUI: 4 FAILED, against a prediction of "3 OR 4" declared BEFORE the run. The marginal
DriverSeegmiller.MeshSequence timed out this time, having passed at 7123 s (98.9% of the cap) in the
P18.1 run. That CONFIRMS the P18.1 finding -- the inherited set is three reliable timeouts plus one
marginal test -- rather than being a new one. It passes in Release in this same run at 1253 s, 5.7x
under the cap, so it is a time budget and not a correctness defect. The 7200 s timeout was NOT
raised. No fifth failure and no new identity, in any configuration.
```

## 10. Repository changes

```text
production added      include/cfd/species/SpeciesTransport.hpp · src/species/SpeciesTransport.cpp
production modified   include/cfd/transport/ScalarTransport.hpp · src/transport/ScalarTransport.cpp
                        -- an OPT-IN GradientBoundaryTreatment selector defaulting to the pre-P18.2
                           behaviour, so every existing caller is bitwise unchanged
                      include/cfd/species/MassFractionState.hpp
                        -- fields() added, so an iterate can be worked on; the invariant is NOT
                           weakened, construction still validates everything
                      src/CMakeLists.txt (+1 target_sources block)
tests added           tests/unit/species/test_species_transport.cpp (34 tests)
test files modified   tests/unit/species/CMakeLists.txt (+1 line)
evidence              results/p18/species_transport/ -- 11 docs, 21 logs, 8 tools
gate amendments       diffusion_closure_A1.md (a DESIGN amendment; the frozen GATE is untouched)
threshold changes     NONE to any frozen gate. One new bound, perStepSumBound, is a named
                      composition of G2(a) with the solver tolerance and P17.8's margin factor.
```

## 11. Integrity

```text
git status              130 M / 145 ?? -- dominated by the pre-existing uncommitted P14-P17 tree
git diff --check        260 warnings, 0 in a P18.2 file (259 in three pre-existing CMakeLists,
                        1 in a TODO.md line P18.0 wrote). PUSH BLOCKER, already in TODO.md.
unexpected changes      none
frozen documents        P18_GATES.md, diffusion_closure.md, formulation.md, audit.md -- all
                        re-hashed at close, all unchanged
validation noise        50 files rewritten by the Debug run, RESTORED; wall_shear changes 0,
                        matching the known L36 attribution. 0 modified after.
later-phase physics     no Arrhenius, no stoichiometry, no reaction rate, no mixture properties.
                        The three non-zero greps are COMMENTS DISCLAIMING those things, resolved by
                        inspection in logs/13_final_integrity.log.
duplicate assemblers    none. The species transport CALLS four P14 assemblers and defines none.
silent clipping         0 clamp/clip.   silent normalization: 0 calls.
deleted tests           none.   DISABLED_ added: 0.
commit / push           NONE. Not authorized.
```

## 12. Next

```text
P18.3 — Mixture Properties
NOT AUTHORIZED YET
```

Carried forward:

```text
1. CASE-FILE REACHABILITY. The new conservative transport is LIBRARY-LEVEL. It is not reachable from
   a case file, because that needs the carrier-species decision: a shipped species field is a passive
   TRACER, not a composition, and read as a one-species mixture it is correctly rejected. The
   pre-P18 passive path remains the production path and is unchanged. STATED AS A LIMIT.
2. TURBULENCE STILL CARRIES THE LAGGED BOUNDARY. Flipping it to Exact fixes the same defect for all
   six turbulence equations but needs its own frozen gate and a re-derivation of the P14 migration
   oracle, whose frozen values encode the lagged behaviour.
3. test_transport_characterization.cpp has ZERO FixedGradient coverage -- which is why the lag
   survived P14's own migration gates.
4. A multi-species boundary type would let sum(Y_i) = 1 be enforced across a shared patch; today the
   per-species boundary sets are independent objects with no cross-species validation.
5. 28 pre-existing clang-format violations and 259 git diff --check warnings, both inherited.
```
