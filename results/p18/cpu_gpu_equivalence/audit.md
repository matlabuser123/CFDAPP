# P18.8 — audit (§2), and the scope decision

```text
MEASURED AT P18.8 OPEN against HEAD 3bd32f918656f471e72b5f6ac7cb6c2cbf75a274.
Raw state at baseline/00_start_state.log.
```

---

## 1. There is no GPU species or chemistry path today

G9's own note says so, and it is re-verified rather than trusted:

```text
files under include/cfd/gpu/ or cuda/ mentioning:
    MassFraction       0
    Mixture            0
    Arrhenius          0
    Reaction           0
    HeatRelease        0
    FormationEnthalpy  0
    speciesSource      0

AND THE TRAP G9 NAMES IS REAL. DeviceScalarTransport exists, but its production consumers are:
    cuda/kernels/DeviceKEpsilonCuda.cpp
    include/cfd/gpu/DeviceKEpsilon.hpp
    include/cfd/gpu/DeviceTurbulence.hpp
plus k/epsilon tests. EVERY ONE IS k-EPSILON. G9: "A claim of 'GPU species' resting on the
existence of DeviceScalarTransport would be vacuous".
```

## 2. The environment, and why it permits bitwise

```text
nvcc    12.9.86                    GPU   RTX 5000 Ada Generation Laptop, compute_cap 8.9
driver  580.97                     VRAM  15352 MiB        WSL2 sees 31 GiB of host RAM

--use_fast_math occurrences in the whole tree : 0
-fmad=false is applied PER SOURCE via COMPILE_OPTIONS in cuda/CMakeLists.txt, and that file
records P17.10's measured cost of the alternative:
    --fmad=false   17 of 17 rows BITWISE, max |flux diff| exactly 0
    --fmad=true     8 of 17 rows bitwise, max |flux diff| 1.52587890625e-04 absolute

SO G9's CONDITION (4) IS ALREADY THE PROJECT CONVENTION and §6's audit finds nothing to change.
```

## 3. Operation-by-operation classification (§2)

| operation | owner | status |
|---|---|---|
| `MassFractionState` (bounds, ΣY, validation) | P18.1 | **HOST CONTROL ONLY** — throws, holds `std::vector`/`std::string`; validation stays host-side (§20 permits, documented) |
| `rho*Y_i` transport assembly | P18.2 | **MISSING GPU IMPLEMENTATION** — see §5 |
| species diffusion, `ΣJ_i` correction | P18.2 | **MISSING GPU IMPLEMENTATION** — see §5 |
| `M_mix` (harmonic) | P18.3 | **MISSING → implemented by P18.8** |
| `R_mix = Σ Y_i R_i` | P18.3 | **MISSING → implemented by P18.8** |
| `cp_mix`, `cv_mix`, `gamma_mix` | P18.3 | **MISSING → implemented by P18.8** |
| composition-dependent EOS `rho(p,T,Y)`, `p`, `T` | P18.3 → P17 type | **MISSING → implemented by P18.8** |
| reaction stoichiometry `nu_i` | P18.4 | **SHARED HOST/DEVICE** as a POD — the coefficients upload; the *validation* stays host |
| `k(T) = A T^n exp(-Ea/(R_u T))` | P18.5 | **MISSING → implemented by P18.8** |
| concentrations, progress rate `q_r` | P18.5 | **MISSING → implemented by P18.8** |
| `omega_i = M_i nu_i q_r` | P18.4 | **MISSING → implemented by P18.8** |
| reaction enthalpy `Delta H_r` | P18.6 | **MISSING → implemented by P18.8** |
| heat-release source `S_E` | P18.6 | **MISSING → implemented by P18.8** |
| energy update, temperature recovery | P18.6 | **MISSING → implemented by P18.8** |
| conservation reductions (`Σω_i`, `ΣY−1`, energy) | P18.4/P18.6/P18.7 | **implemented per cell on device**; the cross-cell reduction is **HOST CONTROL ONLY** (§39) |
| `DeviceThermodynamics` (single-species closure) | P17.10 | **GPU IMPLEMENTED, but SINGLE-SPECIES** — 9 scalars, no composition. P18.6's audit called widening it "P18.8's largest item" |

## 4. What P18.8 implements, and why that set

```text
THE WHOLE THERMOCHEMISTRY LAYER, CELL-LOCAL, BITWISE:

    mixture M/R/cp/cv/gamma   ->  EOS rho,p,T  ->  k(T)  ->  C_i  ->  q_r
      ->  omega_i  ->  Delta H_r  ->  S_E  ->  energy update  ->  T recovery

EVERY ONE OF THESE IS CELL-LOCAL ALGEBRA. No face ordering, no reduction, no atomics -- so G9's
conditions (2), (3) and (5) are satisfied VACUOUSLY rather than by effort, and bitwise reduces to
(1) one shared source and (4) --fmad=false. That is why this is the right set to do first and to
do completely: it is where the new P18 physics lives AND where bitwise is genuinely attainable.

IT COVERS, directly: sections 9, 10, 11, 12, 13, 14, 17, 18, 19, 20, 21, 22, 23, 33, 34, 35, 36,
37, 38, 39, 40, 41, 44, 45, 46, 48, 49.
```

## 5. What P18.8 does NOT implement, stated up front

```text
SPECIES TRANSPORT ON DEVICE -- the rho*Y_i face-loop assembly, the diffusive closure and the
species boundary treatment (sections 15, 16, 24, 30, 31, 32).

WHY NOT, and this is a scope judgement the user should see rather than discover at the end:

  (a) IT IS A DIFFERENT KIND OF PROBLEM. The thermochemistry is cell-local; the transport is a
      FACE LOOP with a per-cell gather, and G9's bitwise derivation spends three of its five
      conditions on exactly that shape -- accumulation order, uploaded geometry, order-independent
      reductions. Porting it means reproducing P14's device-assembly machinery for the species
      closure, including the P18.2 boundary-lag semantics and the correction velocity.

  (b) THE CANONICAL REACTING FLOW (section 24) DEPENDS ON IT. P18.7's plug flow assembles a sparse
      matrix and calls a Krylov solve; an equivalent device path needs the device assembly AND a
      device solve with matching iteration control (section 25). That is a phase-sized piece of
      work on its own.

  (c) DOING IT BADLY WOULD BE WORSE THAN NOT DOING IT. A species transport kernel that reused
      DeviceScalarTransport without the species closure is precisely the vacuous claim G9 names.

CONSEQUENCE: P18.8 closes G9 for the THERMOCHEMISTRY, including an end-to-end paired batch-reactor
solve compared on the state (section 23's named case), and reports the reacting-FLOW half as NOT
IMPLEMENTED. P18.8 is therefore PARTIAL, and the reason is scope, not a failed measurement.
```

## 6. Host-side by design, not by omission (§20, §39)

```text
STATE VALIDATION STAYS ON THE HOST. MassFractionState enforces 0 <= Y <= 1 and |sum Y - 1| <= 4 eps N
by THROWING, and P18.5/P18.6 refuse invalid temperatures, densities and parameters by throwing.
A device cannot throw.

THE DEVICE THEREFORE RETURNS A STATUS VALUE, exactly as P17.10's DeviceRecoveryStatus does, and
the equivalence suite compares THE REASON, not merely the fact of refusal -- because two backends
rejecting the same state for different reasons is a divergence in the admissible set. Section 20
permits host-side validation "if GPU validation remains host-side, document that clearly"; this is
that documentation.

CROSS-CELL REDUCTIONS STAY ON THE HOST. max_cell |sum omega_i|, max |sum Y - 1| and the energy
totals are reductions over cells. Per section 39 a small scalar reduction is legitimate host
control; the PER-CELL quantities are computed on device and compared per cell, so the comparison
is on the state and not on a summary norm -- which is what G9's `required` clause demands.
```
