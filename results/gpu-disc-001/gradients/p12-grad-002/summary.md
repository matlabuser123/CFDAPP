# P12-GRAD-002 / P12-MESH-001 regression against the CUDA path

Two separate statements, deliberately kept separate — one is a test run, the other is an argument
from an exact result, and conflating them would overstate what was checked.

## 1. The CPU gates still pass

`cpu_gate_tests.log` — `ctest -R "Gradient|Interpolation|BoundaryConsistency"`

```text
100% tests passed, 0 tests failed out of 82
```

This phase modified **no** production CPU file. The only production header touched outside
`cuda/` is `include/cfd/gpu/DeviceMesh.hpp`, which gained a const accessor and is CUDA-only.

## 2. The CUDA path is bitwise identical on the GRAD-002 mesh families

`gpu_on_grad002_meshes.log` — the subset of the differential covering the meshes those phases were
qualified on: `quad translated`, `distorted q16`, `q16 translated`, `sheared 0.35`,
`planar skew 3d 6`, `warped 3d 6`.

```text
90 lines, 0 FAIL
```

Every case: `diff=0  maxAbs=0  maxRel=0  L2=0`, under Dirichlet, Neumann and mixed conditions, for
constant, linear, quadratic and manufactured fields.

## 3. Why that is a regression result and not just a coincidence

P12-GRAD-002's defect was that an **exact geometric predicate** guarding the boundary treatment made
a *translated* Cartesian mesh get a different discretization. The property it restored is that the
gradient does not depend on where the mesh sits in space.

The differential covers both halves of that directly:

```text
quad 16 at origin          vs  quad 16 at (1000, -250)
distorted q16 at origin    vs  distorted q16 at (1000, -250)
```

On each of the four, the GPU reproduces the CPU **exactly, bit for bit**. So whatever the CPU's
translation behaviour is — and §1 says the CPU tests assert it is correct — the CUDA path has the
identical behaviour. There is no residual for a tolerance to hide, and no mesh family where the two
paths could diverge under translation while both still passing.

The same argument carries P12-MESH-001: `planar skew 3d 6` and `warped 3d 6` under Neumann
conditions have 216 oblique faces each, re-evaluated inside every sweep with the normal/tangential
split, and the results are bitwise identical.

## 4. The limit of the argument

Bitwise identity transfers a property from the CPU to the GPU **only on the cases actually run**.
It says nothing about a mesh family neither path has been tested on. The mesh families here are the
ones P12-GRAD-002 and P12-MESH-001 were themselves qualified on, which is the intended scope, but
it is not a proof for arbitrary geometry.
