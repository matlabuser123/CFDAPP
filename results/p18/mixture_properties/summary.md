# P18.3 — Mixture Properties — SUMMARY

```text
P18.3 STATUS: COMPLETE

GATE        results/p18/gates/P18_GATES.md
            sha256 921616544e3631d40de3ea629a1617e8ec014f89fcb1d7b8a728db7132d618f3
            re-verified UNCHANGED at open and at close. No threshold in it was edited.
G5          PASS -- the principal gate. Every identity within 8 eps (two exactly 0), the
            single-species limit BITWISE with 0 mismatches, and all three of G5's own negative
            controls detecting.
G1          PASS -- P18.3 modifies no existing production source; measured across P14/P15/P16/P17/
            P18.1/P18.2 and the three-configuration regression.
G2          REUSED, not re-implemented. Control N7 asserts the refusals.
G10         PASS for the property layer, bitwise. NOT the solver-level claim.
NOT CLAIMED G6 G7 G8 G9 G11 -- none measurable from thermodynamic closure (section 29).
            G3 and G4 are P18.2's and are not re-claimed.
TESTS       165 / 165 in CFDSpeciesTests. 34 new.
```

---

## 1. Mixture model

```text
M_mix formula      1 / M_mix = sum_i ( Y_i / M_i )        the MASS-FRACTION (harmonic) relation
R_mix formula      R_mix     = sum_i Y_i R_i,  R_i = R_u / M_i
                   cross-checked against R_u / M_mix
cp_mix formula     cp_mix    = sum_i Y_i cp_i(T)          MASS-weighted; cp_i(T) from P15
cv_mix formula     cv_mix    = cp_mix - R_mix             derived by ThermodynamicProperties
gamma_mix formula  gamma_mix = cp_mix / cv_mix            derived by ThermodynamicProperties
EOS formula        rho = p / (R_mix T)                    via the existing IdealGasEOS
h_mix / e_mix      h_mix = sum_i Y_i h_i(T)  ·  e_mix = h_mix - R_mix T
R_u                8.31446261815324 J/(mol K), CODATA 2018
```

```text
ONLY TWO QUANTITIES ARE COMPUTED HERE -- R_mix and cp_mix. Everything else comes from constructing
the EXISTING cfd::compressible::ThermodynamicProperties from them, which already derives cv and
gamma and already owns an IdealGasEOS. Consequences:
  * no second EOS and no second enthalpy stack (sections 1, 9);
  * cp, cv and R cannot drift apart -- only two are stored (section 6);
  * gamma EMERGES and is never interpolated (section 7);
  * the single-species limit is bitwise for cv, gamma and density AUTOMATICALLY once it is bitwise
    for R and cp.
```

## 2. Quantitative closure

```text
max |cp - cv - R| / R                 2.207671e-16     (0.99 eps)
max gamma consistency error           0.000000e+00     exactly zero -- gamma IS cp/cv, not stored
max M_mix single-species error        1.238495e-16     (0.56 eps)
max R_mix closure error (two forms)   2.205243e-16     (0.99 eps)
max cv discrepancy (closure vs sum)   1.745889e-16     (0.79 eps)
EOS rho error vs p/(R_mix T)          0.000000e+00     exactly zero -- same IdealGasEOS
EOS p round-trip error                2.872325e-16     (1.29 eps)
EOS T round-trip error                3.031649e-16     (1.37 eps)
G5 allowance                          8 eps = 1.7763568394002505e-15
```

Measured over 9 compositions × 5 temperatures × 3 pressures, plus a separate `cp(T)` case.
**The two exact zeros are architectural, not luck** — they are the evidence that no duplicate
closure and no duplicate density formula exist.

## 3. Single-species compatibility

```text
M difference        1.238495e-16 relative -- MEASURED, NOT CLAIMED BITWISE (see below)
R difference        0   BITWISE
cp difference       0   BITWISE
cv difference       0   BITWISE
gamma difference    0   BITWISE
EOS difference      0   BITWISE (density, speed of sound, p(rho,T), T(p,rho))
h difference        0   BITWISE
P17 result          0 bitwise mismatches over 5 temperatures x 3 pressures x 5 quantities,
                    against a ThermodynamicProperties built DIRECTLY the way P17 builds it
```

```text
WHY M IS NOT CLAIMED BITWISE, stated rather than glossed: M_mix = 1/(1/M_1) carries a reciprocal
pair, and 1/(1/x) is not bitwise for 17.198% of random doubles. P17 has NO MOLECULAR WEIGHT AT ALL
-- 0 occurrences in the repository before P18.3 -- so G5's bitwise clause, which binds "P17's
EXISTING single-species value", does not reach M. Claiming bitwise here would be claiming a property
of IEEE arithmetic that does not hold.
```

### The measurement that decided the formulation

```text
G5 requires a BITWISE single-species limit. Three algebraically identical routes to R_mix exist and
they are NOT equivalent in floating point. Measured BEFORE writing any code:

    1 / (1 / x) == x          FALSE for 34397 / 200000 = 17.198% of random doubles
    R_u * (1/M) == R_u / M    FALSE for M = 0.032 kg/mol -- OXYGEN
    sum_i Y_i R_i at Y_1 = 1  = 1.0 * R_1 = R_1  EXACTLY

So R_mix had to be sum_i Y_i R_i, not the textbook R_u * sum(Y_i/M_i).

HOW NEARLY THIS WAS MISSED: air, CO2, H2 and CH4 ALL round-trip fine through R_u*(1/M). A test built
from a plausible species list would have passed while the formulation was wrong for oxygen. It was
found by random sampling, not by trying species.
```

## 4. Composition sensitivity

```text
binary mixtures           50/50 and 25/75 H2/CO2, against an independent 50-digit reference
ternary mixtures          0.2/0.3/0.5 and 1/3 each
near-pure mixtures        1 - 2e-9 H2 with 1e-9 of each other species
spatially varying         8-cell strip, Y_H2 graded 0.05 -> 0.95: R_mix, cp_mix, rho and M_mix all
                          vary by > 5x, and EVERY cell's field value is bitwise equal to a direct
                          evaluation of that cell's own composition
temperature-dependent     a two-term cp(T) = a0 + a1 T per species, verified at 300/600/1200/2400 K
                          with the closure re-checked at every temperature; gamma(300) != gamma(2400)
sweep                     41 points of Y_A from 0 to 1. Monotonicity asserted ONLY where DERIVED --
                          R_mix is linear in Y so it is monotone, rho follows; gamma is a ratio of
                          two affine functions and is NOT asserted monotone (section 14).
```

## 5. Negative controls — 8/8, with margins

```text
N1 arithmetic-average M       DETECTED   wrong/correct = 5.9690x on 50/50 H2/CO2
N2 R_mix left constant        DETECTED   21.83x spread; and the 50/50 value must be the exact mean,
                                         a DERIVED linearity property, not just a spread
N3 cp wrong weighting basis   DETECTED   simple mean / mass-weighted = 1.7995x
N4 cv computed inconsistently DETECTED   the two forms agree to 1.745889e-16; a wrong weighting
                                         in either would break it
N5 gamma averaged directly    DETECTED   1.346709 vs 1.397972 = 3.67% error
N6 density uses constant R    DETECTED   rho(CO2)/rho(H2) = 21.8304x at identical p and T, and every
                                         rho checked against p/(R_mix T)
N7 invalid composition        DETECTED   refused by P18.1's type, three ways including one with an
                                         exactly-1 sum so only the bounds check can fire
N8 P17 result changes         DETECTED   0 bitwise mismatches over a 5 x 3 x 5 matrix
```

```text
N1'S VACUITY TRAP IS ASSERTED AS ITS OWN TEST. Near a pure species BOTH molecular-weight formulas
agree to a ratio of 1.0000, so a control built on a near-pure composition would pass while detecting
nothing. N1 therefore uses 50/50, and the near-pure agreement is asserted explicitly so the reason
lives in the suite rather than only in a document.

The species were chosen for this: H2 and CO2 differ by 21.8x in molecular weight and 17.0x in cp.
```

## 6. Determinism

```text
repeat count        1000 (probe) and 50 (test)
max difference      0
bitwise             YES, for M_mix, R_mix, cp_mix, cv_mix, gamma_mix, rho and h_mix
mechanisms          fixed ascending species order · no unordered iteration · no parallel reduction ·
                    no cached or mutable state · cv and gamma derived, never stored
```

## 7. Regression

```text
focused P18.3   165 / 165 passed (34 new · 34 P18.2 · 60 P18.1 · 37 pre-P18)
P18.1           60 / 60          P18.2   34 / 34
P15             163 / 163  (Enthalpy 8/8, TemperatureProperty 5/5)
P17             215 / 215  (ThermodynamicProperties 10/10, IdealGas 30/30, TotalEnergy 34/34)
P14 / P16       22 / 22  ·  246 / 246

Release+CUDA    2891 selected · 2840 Passed · 0 FAILED · 2 Skipped · 49 DISABLED · 2141 s · exit 0
CPU-only        2668 selected · 2621 Passed · 0 FAILED · 0 Skipped · 47 DISABLED · 1402 s · exit 0
Debug+GUI       2953 selected · 2899 Passed · 3 FAILED · 2 Skipped · 49 DISABLED · 7973 s · exit 8
```

```text
BOTH RELEASE CONFIGURATIONS ARE EXACT: +34 selected, +34 Passed, nothing else moved.

DEBUG+GUI: 3 FAILED against a prediction of "3 or 4" declared BEFORE the run. The marginal
DriverSeegmiller.MeshSequence PASSED this time at 6764.75 s, 94.0% of the frozen 7200 s cap. Three
measurements of the same test now exist -- P18.1 passed at 7123 s, P18.2 timed out at 7201.94 s,
P18.3 passed at 6764.75 s -- on trees differing only by additive changes it cannot reach.

THE CHARACTERISATION IS SETTLED: three reliable timeouts plus one marginal test. The timeout was NOT
raised. No fifth failure and no new identity, in any configuration.
```

## 8. Repository changes

```text
production added      include/cfd/species/MixtureThermodynamics.hpp
                      src/species/MixtureThermodynamics.cpp
production modified   NONE. src/CMakeLists.txt gained one target_sources block (8 lines).
tests added           tests/unit/species/test_mixture_thermodynamics.cpp (34 tests)
test files modified   tests/unit/species/CMakeLists.txt (+1 line)
evidence              results/p18/mixture_properties/ -- 13 docs, 10 logs, 6 tools, 1 data file
gate amendments       NONE. P18.3 needed no amendment.
threshold changes     NONE. Every bound is G5's, inherited verbatim.
```

## 9. Integrity

```text
git status              130 M / 148 ?? -- dominated by the pre-existing uncommitted P14-P17 tree
git diff --check        260 warnings, 0 in a P18.3 file (259 in three pre-existing CMakeLists,
                        1 in a TODO.md line P18.0 wrote). PUSH BLOCKER, already in TODO.md.
unexpected changes      none
frozen gate             re-hashed at close, unchanged
validation noise        50 files rewritten by the Debug run, RESTORED; wall_shear changes 0. 0 after.
reaction code early     NONE -- 0 for reactionRate, omega_i, heatRelease, heatOfFormation,
                        formationEnthalpy. The Arrhenius and stoichiom hits are ONE COMMENT
                        disclaiming them, resolved by inspection in logs/05_final_integrity.log.
heat release early      NONE. P15's referenceEnthalpy was NOT repurposed as a formation enthalpy --
                        the P18.0 audit warns specifically against that.
P18.2 transport         UNTOUCHED -- 0 changes; its hash is recorded in the integrity log.
P17 shock               UNTOUCHED -- HllcFlux, TotalEnergy, IdealGasEOS, ThermodynamicProperties
                        hashes recorded. P17 G6/G10 NOT reopened.
duplicated EOS logic    NONE -- 0 EOS classes defined, 0 density formulas written.
duplicated P15 logic    NONE -- 0 polynomial evaluators written; SensibleEnthalpy reused.
silent normalization    NONE -- the one 'normali' hit is a COMMENT saying nothing is renormalised.
threshold weakening     NONE.
deleted tests           NONE. DISABLED_ added: 0.
commit / push           NONE. Not authorized.
```

## 10. Next

```text
P18.4 — Reaction Source Framework
NOT AUTHORIZED YET
```

Carried forward:

```text
1. THE TotalEnergyState cp(T) REFUSAL. P18.3 supplies the prerequisite it names -- gamma now varies
   with both T and Y -- and DOCUMENTS the exact extension point, but does not lift the check. The
   check is byte-for-byte unchanged and its 34 tests pass. Lifting it would widen the accepted domain
   of a type whose recover() feeds the HLLC solver, whose G6 positivity defect is still OPEN; that
   needs its own frozen gate. single_species_compatibility.md section 4.
2. CASE-FILE REACHABILITY. The mixture EOS is library-level: no case key selects it and no solver
   consumes it. The P17 single-species path is unchanged and remains what production uses.
   This is the same limit P18.2 carries for its conservative transport.
3. THE MARGINAL DEBUG+GUI TIMEOUT is now measured three times. P18.9 cannot predict a single
   Debug+GUI failure count.
4. Inherited: turbulence keeps the lagged gradient boundary; test_transport_characterization.cpp has
   no FixedGradient coverage; 28 clang-format violations and 259 git diff --check warnings.
```
