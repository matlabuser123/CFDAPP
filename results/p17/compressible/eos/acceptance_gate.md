# P17.2 — Ideal-gas EOS inverse: acceptance gate

**Frozen 2026-09-26, before any code was written.** Every baseline below is measured
on the unchanged tree and recorded in the dry-run, never inferred. Stop at the first
failure. No threshold may be weakened; a mis-derived criterion is preserved and
amended separately.

---

## What this item is, and is not

P17's audit found the EOS abstraction already provides the forward mapping
`(p,T) -> rho` plus both analytical derivatives, and that `ThermodynamicProperties`
already derives `cv = cp - R` and `gamma = cp/cv` by construction. This item adds
**one** thing: the inverse `(rho,T) -> p`, on the abstraction rather than on the
concrete class, so future generic compressible code never has to down-cast.

It is **not** a new thermodynamic-property implementation. `EquationOfState`,
`IdealGasEOS` and `ThermodynamicProperties` remain the only homes.

## Type convention, recorded

The authorization wrote `virtual double pressure(double rho, double T) const = 0;`.
The implementation uses `Real`, which is this codebase's typedef for `double` and
what every neighbouring signature in `EquationOfState` uses. Same type, spelled the
way the surrounding code spells it.

## Group P — the pressure convention is PRESERVED, not extended

| id | criterion | baseline |
| --- | --- | --- |
| P1 | `pressure(rho,T)` returns **absolute thermodynamic** pressure, documented in the interface. | the convention already exists in 3 places |
| P2 | SIMPLE still solves **gauge** pressure. Unchanged. | `CompressibleSIMPLE.hpp:90` |
| P3 | `referencePressure` remains the single gauge <-> absolute bridge. Unchanged. | `ProjectRunner.hpp:144`, `PhysicsConfig.hpp:179` |
| P4 | `density(p,T)` behaviour is bit-for-bit unchanged. | measured in the dry-run |
| P5 | `dDensityDPressure` and `dDensityDTemperature` are bit-for-bit unchanged. | measured in the dry-run |

## Group R — round trips  (DETECTION)

| id | criterion | bound |
| --- | --- | --- |
| R1 | `(p,T) -> rho -> p` recovers p. | relative error <= 1e-12 |
| R2 | `(rho,T) -> p -> rho` recovers rho. | relative error <= 1e-12 |
| R3 | Both hold at **representative non-default states**, not only at the defaults a hard-coded return could satisfy. | >= 4 states, incl. non-unit R |
| R4 | `p = rho * R * T` exactly, against an independently computed expectation. | relative error <= 1e-12 |

**Why 1e-12 and not tighter:** `p/(R*T)` then `rho*R*T` is two roundings of a double,
so exact bitwise recovery is not guaranteed for arbitrary values; 1e-12 is ~4500x
the double epsilon and still four orders tighter than the 1e-9 the existing
`EosConsistencyErrorIsAtFloatingPointTolerance` low-Mach test uses. Derived from the
dimensional floor of the operation, not chosen after seeing a result.

## Group V — validation and determinism

| id | criterion |
| --- | --- |
| V1 | Non-finite or non-positive `rho` is rejected with `InvalidArgumentError`. |
| V2 | Non-finite or non-positive `T` is rejected with `InvalidArgumentError`. |
| V3 | A resulting non-finite pressure is rejected, not returned. |
| V4 | The same inputs produce bitwise-identical output across repeated calls. |
| V5 | Rejection messages name the offending field, as the existing ones do. |

## Group T — the thermodynamic invariants stay true

| id | criterion |
| --- | --- |
| T1 | `cp - cv = R` still holds. Already true *by construction* — `ThermodynamicProperties` derives cv; this item must not add a path that bypasses that. |
| T2 | `gamma = cp/cv` still holds, same reasoning. |
| T3 | `speedOfSound(T) = sqrt(gamma R T)` tests still pass. |

## Group N — non-vacuity controls  (each MUST fail)

| id | control | must break |
| --- | --- | --- |
| N1 | Return `rho * T` (drop R) from `pressure`. | R4, and R2 for any R != 1 |
| N2 | Return a constant. | R1/R2/R3 at every non-default state |
| N3 | Remove the input validation. | V1/V2 |
| N4 | Perturb the gas constant by 1e-6 relative. | R4 at the 1e-12 bound — proves the bound is what detects, not the plumbing |

N4 is the sharpest: a 1e-6 perturbation is far above the 1e-12 bound but far below
anything a casual eye would notice, so it proves the criterion's *tightness* is
doing the work.

## Group G — preservation gate

| id | criterion |
| --- | --- |
| G1 | The existing EOS and thermodynamics tests pass. |
| G2 | The full existing compressible suite passes — 90 tests, 13 files. |
| G3 | P13 preservation subset passes. |
| G4 | P14 transport preservation passes — 104 tests. |
| G5 | P15 thermal preservation passes — 248 tests. |
| G6 | P16 preservation passes — 363 + 126 tests. |
| G7 | The P13-001 SIMPLE fingerprint is **bitwise identical**. An EOS change must not move the incompressible solver by one bit. |

## Dry-run record

To be completed against the unchanged tree **before** this gate is acted on.

```text
pressure(rho,T) on EquationOfState        absent          -> present   NON-VACUOUS
implementations of EquationOfState        1 (IdealGasEOS) -> 1
existing compressible tests              measured below
density(p,T) reference values            measured below
derivative reference values              measured below
SIMPLE fingerprint                       10 cases / 130 lines, bitwise
SOURCE_FINGERPRINT_V2                    721ce161aafbf6958a4620c4d9fe4b4f332dbba3519f6e9b9496548213901955
```

## Gate verdict

P17.2 closes only when P, R, V, T and G pass and every N control fails. The new
qualified source state is recorded with the authorized interface delta attributed
file-by-file.
