# TD-003 C3 — ATTRIBUTED. The TRANSIENT term produces 98.8 % of the residual, and it carries the deviation rather than creating it.

```text
STATUS:      **ATTRIBUTED, not hypothesised.** Six hypotheses failed in this item; this
             instrument charges the residual to a NAMED assembler by isolation, with a
             reconstruction check that makes either outcome informative.
               transient            2.376432e-11    98.79 %
               closure correction   9.252641e-13     3.85 %
               diffusion            6.776264e-21     0.00 %   machine precision on 1e-4 terms
               convection           0.000000e+00     EXACTLY zero
               source (Su=Sp=0)     0.000000e+00     EXACTLY zero
               sum of parts / r_total = 1.0263       <- the decomposition CLOSES
             With the term named, the algebra is forced: `r^trans = rho V/dt * d_old`, so the
             transient CARRIES the previous step's deviation.
             **AND IT CORRECTS MY OWN CONTROL.** NV1 disabled the CLOSURE correction -- 3.85 %
             of r -- and I read its null result as refuting feedback in general. It refuted
             feedback through the closure ONLY. The transient was never tested.
             **The origin of the deviation is STILL UNKNOWN.** The transient explains
             PERSISTENCE, not seeding, and every source site measured sums to 1 exactly.
SCOPE:       one instrumented run on a copy outside the repository. No production change, no
             test change; the 1e-8 threshold stands untouched and the test still fails.
INSTRUMENT:  logs/08_attribution.log. Eight extra assemblies per worst-step update, bands and
             rhs only -- no solve, so the isolated configurations need not be well posed.
COMMIT:      carried as evidence.
```

---

## 1. WHY THIS INSTRUMENT IS DIFFERENT FROM THE SIX THAT FAILED

Every earlier attempt reasoned from a correlation or a scale argument, and each time the
observable was derived from the quantity being diagnosed. This one does not test a story.

`src/species/SpeciesTransport.cpp:300`-`:345` runs the assemblers as a SEQUENCE that accumulates
into one builder and one rhs. So `r = sum_i b_i - (A*1)` is ADDITIVE over them, and each share is
obtained by assembling with only that one enabled:

```text
k=1  diffusion alone
k=2  (diffusion + closure) - diffusion     the closure correction's own share
k=3  convection alone
k=4  transient alone
k=5  source -- Su = Sp = 0, so adding 0.0 is exact; the share is exactly 0, asserted
```

**The reconstruction check is what makes it decisive**, because production accumulates in one
builder and its rounding differs from the isolated runs:

```text
sum_k r_k ~ r_total   ->  ONE assembler is responsible, and it is named
sum_k r_k !~ r_total  ->  no single assembler is; r comes from the ACCUMULATION ORDER
```

Measured `sum/total = 1.0263`. **The first branch holds, to 2.6 %.**

## 2. THE ALGEBRA, NOW THAT THE TERM IS NAMED

The transient contributes `a_P += rho_new V/dt` and `b_i += rho_old Y_i^old V/dt`.
`PremixedFlame.cpp:1157` passes the SAME density at both levels -- deliberately, and its comment
explains why -- so both are `rho V/dt`:

```text
sum_i b_i^trans = rho V/dt * (sum_i Y_i^old) = rho V/dt * (1 + d_old)
(A*1)^trans     = rho V/dt
r^trans         = rho V/dt * d_old
```

So the transient cannot cancel unless `sum_i Y_i^old` is exactly 1. The implied coefficient,
`r^trans / d = 1.032546e-05`, is consistent with a transient coefficient at these scales
(`||A||_inf = 2.73e-04` includes diffusion as well).

### 2.1 THIS IS PROBABLY NOT A DEFECT, and that matters for what to do next

`r^trans = rho V/dt * d_old` is the CORRECT discretisation of `rho dY/dt` in non-conservative
form. If `sum_i Y_i^old = 1 + d`, the equation faithfully says the deviation persists -- the
transient TRANSPORTS it, it does not manufacture it.

**So "fix the transient" may be exactly the wrong conclusion.** The question this attribution
sharpens is not why `r` is large but **what seeds `d` in the first place**, given that every
source site measured sums to 1 exactly: the inlet composition (bit-exactly 1.0), the seeded
convex blend, `sum_i omega_i` (exactly 0 in IEEE), the recentring fill (bit-exactly 1.0) and the
burnt composition (`sum_i nu_i W_i` exactly 0).

## 3. IT CORRECTS MY OWN CONTROL

`verification_amendment_withdrawn.md` §2 recorded the feedback hypothesis as REFUTED on the
strength of NV1. That conclusion was too broad.

```text
what NV1 disabled    the CLOSURE correction -- measured here at 9.252641e-13, 3.85 % of r
why it changed little  sum_j J^0_{j,f} = -gamma d(sum Y)/dx A, and at Le = 1 with sum Y uniform
                       to ~1e-6, rawSum ~ 0. The term does almost nothing in this flame.
what was never tested  the TRANSIENT -- 98.79 % of r
```

**So the feedback MECHANISM is real and runs through the transient; NV1 refuted only the closure
route.** The correction is recorded forward rather than by rewriting that file.

## 4. WHAT IS NOT ESTABLISHED — including a number I am deliberately not claiming

```text
NOT established  THE LOOP GAIN. `||A^-1||_inf * rho V/dt = 1.2391` multiplies a GLOBAL max-row-sum
                 by a LOCAL coefficient at one cell. That is the same cross-scope category error
                 that wrecked the "round-off floor" claim (which compared a residual at one state
                 against ||b||_2 from the whole march). A rigorous gain needs the SPECTRAL RADIUS
                 of (rho V/dt) A^-1, not a norm times a local value. 1.2391 is indicative only and
                 is NOT evidence of growth.
NOT established  what SEEDS d. Section 2.1. The transient explains persistence only.
NOT established  that the transient should change. Section 2.1 argues the opposite; a conservative
                 rho^{n+1}/rho^n form was already tried and rejected at :1145-:1157 for measured
                 reasons (a temperature of 149 K against a 300 K inlet, |sum Y - 1| of 0.89).
NOT established  that the 3.85 % closure share is itself benign. It is small HERE because
                 rawSum ~ 0 at Le = 1; at Le != 1 it would not be, and no run here varies Le.
NOT measured     the attribution at any state other than the worst one, or on the production
                 reacting path, which shares the assembler but not the settings.
APPROXIMATION    the lambda rebuilds the zero source locally (`Su = Sp = 0`) because `source` is
                 scoped to the species loop. It is the same value production passes
                 (PremixedFlame.cpp:1172) but it is a RECONSTRUCTION, not the same object.
UNCHANGED        the 1e-8 threshold, AMENDMENT A1's withdrawal, and TD-003's §12 stop.
```
