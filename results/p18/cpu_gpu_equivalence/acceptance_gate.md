# P18.8 — acceptance gate (§7)

```text
FROZEN BEFORE ANY CPU/GPU COMPARISON. Hashed at logs/01_freeze.log.

THE POLICY IS BITWISE, AND IT IS G9's OWN, INHERITED VERBATIM:
    "acceptance   BITWISE. DERIVED FROM P17.10, which established that bitwise is achievable for a
     face-loop assembly when (1) one shared source compiles for both, (2) the per-cell gather
     visits faces in the CPU's accumulation order, (3) geometry is uploaded not recomputed,
     (4) --fmad=false, (5) reductions are order-independent. Anything looser is a concession that
     must be argued from a specific mechanism, not assumed."

NO TOLERANCE IS CHOSEN AFTER SEEING A DIFFERENCE. Section 7 forbids it and none is.
```

---

## 1. Why bitwise is required of this layer and not merely hoped for

```text
P18.8 implements CELL-LOCAL algebra only (audit.md section 4). Of G9's five conditions:

    (2) accumulation order        VACUOUS -- there is no face gather
    (3) uploaded geometry         VACUOUS -- no geometry enters the thermochemistry
    (5) order-independent reduce  VACUOUS -- no cross-cell reduction on device

so bitwise rests entirely on:

    (1) ONE SHARED SOURCE. include/cfd/gpu/DeviceSpeciesThermochemistry.hpp uses
        CFD_HLLC_HOST_DEVICE, which expands to __host__ __device__ under nvcc and to NOTHING under
        gcc. The SAME TEXT is the CPU restatement and the device code.
    (4) --fmad=false, already the per-source convention in cuda/CMakeLists.txt.

AND ONE MORE, WHICH P17.10 LEARNED THE HARD WAY AND WHICH IS WRITTEN INTO DeviceThermodynamics.hpp:
    EVERY EXPRESSION IS PARENTHESISED THE WAY THE CPU WRITES IT. `rho*R*T` is (rho*R)*T left to
    right; `rho*(R*T)` is the same number in exact arithmetic and a different one in floating
    point. The restatements mirror the production parenthesisation term by term, and the
    cross-comparison test is what makes that checkable rather than asserted.
```

## 2. Criteria

### E1 — mixture properties (§9)

```text
compared   M_mix, R_mix, cp_mix, cv_mix, gamma_mix
cases      single-species, binary, ternary, near-pure (Y -> 1-eps), spatially varying, x 5 temperatures
criterion  BITWISE, every row
report     bitwise rows / total rows, max absolute and max relative difference
control    N1 (species index permutation), N2 (one diffusivity/weight for all species)
```

### E2 — EOS (§10)

```text
compared   rho(p,T,Y), p(rho,T,Y), T(p,rho,Y), and the INVALID-STATE DECISION for each
criterion  BITWISE on the value; EXACTLY EQUAL on the decision and its reason
control    N11 (GPU silently accepts an invalid state)
```

### E3 — Arrhenius (§11)

```text
compared   k(T), the exponential argument, and the BRANCH taken (A=0 exact zero; direct vs
           log-space fallback under amendment A1)
cases      A = 0; Ea = 0; n = 0; n < 0; high Ea with low T (underflow); high T; the overflow
           fallback regime
criterion  BITWISE on the value; EXACTLY EQUAL on the branch
control    N4 (exponent sign flipped)
```

### E4 — reaction progress (§12)

```text
compared   C_i, k, forward progress, reverse progress, NET progress -- not only omega_i
criterion  BITWISE
control    N4, N8 (one reaction omitted)
```

### E5 — stoichiometric sources (§13, §14)

```text
compared   omega_i per species, per reaction, per cell; the sign; inert species exactly 0;
           multi-reaction accumulation in ASCENDING REACTION INDEX then ascending species
criterion  BITWISE
and        sum_i omega_i must vanish INDEPENDENTLY on each backend to 32*eps*max|omega| --
           section 14: "A GPU result that matches CPU but violates mass conservation is not
           acceptable. Equivalence does not replace correctness."
control    N5 (molecular-weight factor omitted), N1
```

### E6 — heat release (§17)

```text
compared   h_i(T), Delta H_r(T), Delta H_r^0, S_E per reaction and total
cases      exothermic, endothermic, thermoneutral, multi-reaction, reversible
criterion  BITWISE, and SIGN EQUIVALENCE is a hard pass/fail
control    N6 (heat-release sign flipped)
```

### E7 — energy update and temperature recovery (§18, §19)

```text
compared   the energy source, the updated sensible energy, the recovered T, and the recovery
           STATUS; then the full coupled chain
               T_n -> k_n -> q_n -> omega_n / S_E,n -> energy update -> T_(n+1) -> k_(n+1)
           with EVERY INTERMEDIATE compared, not only the final T
criterion  BITWISE
control    N7 (stale temperature), N9 (stale composition in recovery)
```

### E8 — one-step full thermochemistry (§21) — the major gate

```text
One deterministic step exercising mixture properties, kinetics, sources, species update, heat
release, energy update and temperature recovery, with EVERY intermediate compared.
criterion  BITWISE on every intermediate
```

### E9 — multi-step trajectory and the batch reactor (§22, §23)

```text
P18.7's VALIDATED batch reactor, run as paired CPU/GPU solves with IDENTICAL initial state, dt
sequence and step count.
compared   Y_i(t), T(t), q, k, omega_i, S_E, energy, species mass, sum Y_i at every step
criterion  BITWISE at every step, and the divergence must not grow -- measured, not assumed
control    N10 (different timestep/control), N7
```

### E10 — determinism (§44, §45, §46)

```text
CPU repeats  : bitwise, and UNCHANGED from the P18.7 baseline
GPU repeats  : bitwise
BLOCK SIZES  : at least two legal sizes (64 and 256); results BITWISE EQUAL between them
criterion  BITWISE
```

### E11 — invalid-state parity (§20, §48)

```text
compared   accepted/rejected DECISION, the reason code, and the offending cell/species where
           applicable, for: negative Y_i, invalid sum Y_i, NaN Y_i, T <= 0, NaN T, invalid
           molecular weight, invalid kinetic parameter, non-finite reaction rate
criterion  EXACTLY EQUAL decision AND reason. A GPU that accepts what the CPU rejects FAILS.
control    N11
```

### E12 — transfer and residency (§36, §37, §38)

```text
measured   H2D and D2H call counts and bytes, for one step, a short trajectory and the full solve,
           separated into initialization / per-step / final output / diagnostics
criterion  NO FULL-FIELD per-step transfer in the resident loop. Small scalar control transfers
           are permitted and must be NAMED (section 39).
           INSTRUMENTED, never claimed from code inspection (section 36).
control    N12 (a full-field transfer inserted into the resident loop)
```

### E13 — CUDA diagnostics (§41, §42, §43)

```text
memcheck, racecheck, initcheck, synccheck over the new P18 kernels, with the MEASURED kernel
scope reported -- not "all CUDA clean".
LIVE CONTROLS retained where the established methodology supports them: a clean sanitizer log and
a log from a run that never instrumented the kernel are INDISTINGUISHABLE, so the control is what
makes the clean result mean anything.
```

## 3. The twelve negative controls (§49)

| # | defect | criterion it must break |
|---|---|---|
| N1 | GPU species index permutation | E1, E5 |
| N2 | one diffusivity/weight for all species | E1 |
| N3 | `ΣJ_i` correction omitted | **N/A — transport not implemented** (audit.md §5) |
| N4 | Arrhenius exponent sign flipped | E3, E4 |
| N5 | molecular-weight factor omitted in `ω_i` | E5 |
| N6 | heat-release sign flipped | E6 |
| N7 | stale temperature | E7, E9 |
| N8 | one reaction omitted | E4, E5 |
| N9 | stale composition in temperature recovery | E7 |
| N10 | different timestep/control | E9 |
| N11 | invalid state silently accepted | E2, E11 |
| N12 | full-field transfer in the resident loop | E12 |

```text
EVERY CONTROL IS A FLAG ON THE DEVICE PATH, not a source edit -- the pattern
DeviceThermodynamics.hpp established, so each control runs in every regression and cannot rot.
```

## 4. What this gate does NOT cover

```text
SPECIES TRANSPORT ON DEVICE (sections 15, 16, 30, 31, 32) and the CANONICAL REACTING FLOW
(section 24). NOT IMPLEMENTED; audit.md section 5 states why. Reported as NOT IMPLEMENTED, not
weakened into something easier, and N3 is marked N/A rather than quietly dropped.

G11 -- section 53 forbids claiming it. It stays NOT YET QUALIFIED until P18.9.

PERFORMANCE. Section 51: no speedup claim without equivalent solves, and no performance gate is
required. P18.8 will report NOT MEASURED or MEASURED SEPARATELY -- NO PERFORMANCE GATE, and
nothing else.
```
