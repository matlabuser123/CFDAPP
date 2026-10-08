# TD-002 — NEGATIVE CONTROLS

```text
gate   GATE.md section 4, controls N1-N6
log    logs/04_negative_controls.log
```

Every source variant was applied, built, measured and **reverted with sha256 proved** against the
pre-variant hash. The harness fails closed: a build failure or a hash mismatch stops it rather than
leaving a variant in the tree. `src/species/ChemistryIntegrator.cpp`, `src/app/ProjectRunner.cpp` and
`src/species/HeatRelease.cpp` are all byte-identical to their pre-control state.

| id | control | required | measured | verdict |
| --- | --- | --- | --- | --- |
| N1 | `compensateRoundOff = false` | C1, C3 FAIL | the 85 472-step march is **REFUSED** -- `sum to 0.99999999999999811`, outside `4*eps*2`. A **settings flip**, so the control and production are the same binary | **fails** |
| N2 | a **normalising** raw-fields entry point | C9 FAILS | input deviation `5.7182e-08` -> output `2.220446e-16`, ratio `3.88e-09` against a required `>= 0.5` | **fails** |
| N3 | `pre_exponential = 0` on a 20-step march | C6a FAILS | `max S_E = 0.000000e+00` | **fails** |
| N4 | rates frozen at the first sub-step | C5 FAILS | error **stuck at `0.0473867` / `0.0520555`** across controls `0.2 / 0.05 / 0.0125` | **fails** |
| N5 | the march removed from `ProjectRunner` | C10 FAILS | no `mass_fraction_*` columns; `metadata species: []` | **fails** |
| N6 | `steps: 1` on the march case | equals TD-001 bitwise | every exported reacting value **bitwise identical** | **PASS, as required** |

## Why N2 and N4 are the two that matter

**N2 is the forbidden fix.** TD-002's authorization forbids clipping, silent normalisation and
loosening the `sum Y` tolerance. Normalisation is the one of those three that would *look* like a
success -- it drives the reported deviation to `~eps`, which reads as perfect conservation. C9 exists
precisely so that taking it is caught rather than rewarded, and N2 is what proves C9 can catch it:
`2.22e-16` is nine orders below the bound, unmistakable.

**N4 is a defect the code warned about in prose and nothing measured.**
`ChemistryIntegrator.hpp` says rates are recomputed every sub-step because reusing them "would make
the march first-order-wrong rather than merely inaccurate". Freezing them makes refinement stop
working entirely -- the error does not move across a 16x change in the control -- so C5 detects it by
measurement now, not by comment.

## N3's second reading

N3 also shows why TD-001's amendment A2 mattered. With no chemistry at all, a 20-step march still
moves the composition by `3.32e-07` and the temperature by `3.52e-04 K` -- pure transport noise,
accumulated over 20 steps. An existence threshold like "`dT > 1e-9` proves the energy coupling ran"
would pass on this control. A2 replaced those with case-file-predicted values and bands for exactly
this reason, and TD-002 inherits that discipline.
