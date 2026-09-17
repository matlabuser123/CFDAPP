# P12-GRAD-002-INV-001 — investigation plan and frozen state

Authorized 2026-09-16 as an **investigation only**: no production source change, no test, threshold,
golden output or previous evidence modified, no amendment to GRAD-002 or its Amendment A1, no
MESH-007 resumption, no fix of the deformed-3D warped-face issue, nothing committed or pushed.

## Frozen state

| item | value |
| --- | --- |
| git HEAD | `b66310ca871811c4c7671056beec291a451af55c` |
| working tree | 254 entries (178 modified, 76 untracked) at the start of the investigation |
| GRAD-002 binary under test | `libcfdcore.a` sha256 `51e82ee8e0ed16363169b929934961c343ceff2eab58e656f1ada8ac0696f3cd` |
| pre-GRAD-002 comparison library | `$HOME/m7ref/base/build/src/libcfdcore.a` sha256 `eaadaa635e70adb180703875b4d8916fb0baee6277feac08c98c206a85330d6e` |
| GRAD-001 comparison library | `$HOME/m7ref/grad001/libcfdcore.a` sha256 `4fa871b8175b56481431b071e43a8ddfbd8f5bda310b5abdc34f9831a57dbee0` |
| GRAD-002 original gate | `acceptance_gate.md` sha256 `a46973ed5ba4190a008a1c3f3138f21612a6b1ca0f11ca32deb6d0899602d2eb` |
| GRAD-002 Amendment A1 gate | `acceptance_gate_A1.md` sha256 `353b72ef11345922c849af0a456e2c8082e92188a8a10647b7c9fde2e249f186` |

All GRAD-001, GRAD-002, A1 and MESH-007 evidence is preserved unchanged; the freeze log
[logs/00_inv_freeze.log](logs/00_inv_freeze.log) hashes it.

## Method

Every measurement below is made by a probe that links against **either** library and is run from the
repository root, so old and new are compared through the identical code path. Each probe that
re-implements a production formulation carries a **cross-check** against the library it is linked
with, and no conclusion is drawn from a probe that fails its own cross-check.

| step | question | probe |
| --- | --- | --- |
| 2 | exact reproduction of both regressions, old vs new | `inv_poiseuille.cpp`, `inv_curved.cpp` |
| 3, 4 | where do the differences live, spatially and per face | `inv_operator.cpp` |
| 5 | is the degradation causally connected to the boundary reconstruction | `inv_operator.cpp` (α blend, endpoints validated against both libraries) |
| 6 | which reconstruction is locally more accurate against the exact field | `inv_operator.cpp` |
| 7 | interaction with the non-orthogonal correction | `inv_poiseuille.cpp --nonorth=N` (case-file only) |
| 8 | which operator carries the difference into the observable | `inv_wallflux.cpp`, `inv_poiseuille.cpp --convection= / --gradient=`, residual traces |
| 9 | curved-channel geometry / interface interaction | `inv_curved.cpp` |
| 10 | refinement behaviour | `inv_poiseuille.cpp --refine`, `--tight` |
| 11 | deformed-3D warped-face characterization only | `inv_quadrature.cpp` |

## Classification vocabulary (fixed before the evidence)

```text
A — defect in GRAD-002 boundary reconstruction
B — interaction with another numerical operator
C — previous validation benefited accidentally from the old first-order/discontinuous treatment
D — validation metric/reference/gate issue
E — unrelated pre-existing defect
F — inconclusive
```

No classification is chosen until quantitative evidence supports it.
