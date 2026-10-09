# P17.10 — audit of GPU coverage for the P17 capability set

`logs/00_audit.log`, run at HEAD `3bd32f918656f471e72b5f6ac7cb6c2cbf75a274` **before any CUDA
source was written**. Authorization §1.

---

## 1. The headline: there is no GPU density-based solver

```text
symbol                 files under cuda/ or include/cfd/gpu/
DensityBasedSolver                 0
ConservedField                     0
assembleResidual                   0
localTimeSteps                     0
applyUpdate                        0

SANITY, so the zeros mean absence rather than a blind grep:
  DensityBasedSolver under src/ + include/cfd/compressible/ : 3 files
```

P17.8 recorded this as a deliberate scope decision — `architecture.md` §8, "no GPU density-based
solver… reported NOT IMPLEMENTED, never PASS" — and P17.9 inherited it, which is why its gate lists
G8 as NOT APPLICABLE rather than deferred. **P17.10 is therefore an implementation phase, not only a
comparison phase.**

## 2. Classification of every P17 capability

```text
capability                       status                        note
---------------------------------------------------------------------------------------------------
Variable density (P17.1)         NOT REQUIRED ON DEVICE        a field property, not an operation
Ideal-gas EOS (P17.2)            MISSING GPU IMPLEMENTATION    closed form; needs scalars only, §5
  p = rho R T                    MISSING                       one multiply on device
  a = sqrt(gamma R T)            SHARED HOST/DEVICE            already inside HllcFlux.hpp
Compressible continuity (P17.3)  NOT REQUIRED ON DEVICE        pressure-based path; not this solver
Compressible momentum (P17.4)    NOT REQUIRED ON DEVICE        likewise
Total energy (P17.5)             MISSING GPU IMPLEMENTATION    the recovery is a CLOSED FORM, §5
  primitive -> conserved         MISSING                       rhoE = p/(g-1) + rho|U|^2/2
  conserved -> primitive         MISSING                       the one that matters -- §9's warning
physical Euler flux (P17.7)      SHARED HOST/DEVICE            eulerFlux(), CFD_HLLC_HOST_DEVICE
HLLC (P17.7)                     GPU IMPLEMENTED               hllcFlux(), one shared source
  wave speeds, star states       SHARED HOST/DEVICE            20 CFD_HLLC_HOST_DEVICE markers
  HLL control                    GPU IMPLEMENTED               evaluateHllOnDeviceForControlOnly
reconstruction (P17.7)           SHARED HOST/DEVICE            reconstructFirstOrder(), header-only
finite-volume residual (P17.8)   MISSING GPU IMPLEMENTATION    the core of this phase, §3
CFL calculation (P17.8)          MISSING GPU IMPLEMENTATION    per-cell spectral radius + min
conserved update (P17.8)         MISSING GPU IMPLEMENTATION    Q - (dt/V) R, per cell
positivity checks (P17.8)        MISSING GPU IMPLEMENTATION    must make the SAME decisions, §29
non-finite detection (P17.8)     MISSING GPU IMPLEMENTATION    likewise
boundary state generation        MISSING GPU IMPLEMENTATION    exteriorState(); 3 kinds, §35
residual norms (P17.8)           HOST CONTROL ONLY (decided)   see §6 below
conservation diagnostics         HOST CONTROL ONLY (decided)   see §6 below
restart (P17.8)                  NOT REQUIRED ON DEVICE        UNSUPPORTED on CPU and it refuses;
                                                               §34 then requires the refusal be
                                                               preserved, not a GPU restart built
---------------------------------------------------------------------------------------------------
GPU IMPLEMENTED 2 · SHARED HOST/DEVICE 4 · MISSING 8 · HOST CONTROL ONLY 2 · NOT REQUIRED 5
```

## 3. What the GPU layer already provides, and what it does not

```text
ALREADY THERE, and reusable unchanged:
  include/cfd/gpu/DeviceHllcFlux.hpp + cuda/kernels/DeviceHllcFluxKernel.cu
    -- evaluates hllcFlux() on device, 0 formulas restated in the .cu, one shared source
    -- but it is a STANDALONE ARRAY EVALUATOR: it memcpys an array of Riemann problems in and the
       fluxes out. It has no mesh, no residual, no state, no loop.
  include/cfd/gpu/DeviceMesh.hpp -- the topology is ALREADY device-resident:
    cellVolumes, cellCentroidX/Y/Z, faceOwner, faceNeighbor, faceCentroidX/Y/Z,
    faceAreaX/Y/Z, faceArea, cellFaceOffsets, cellFaceIds, boundaryFaceIds
  include/cfd/gpu/DeviceBuffer.hpp, DeviceField.hpp -- allocation and transfer primitives
  tests/unit/gpu/ -- 20 existing GPU test files, including test_hllc_gpu_equivalence.cpp

NOT THERE:
  any device-resident conserved state
  any mesh-integrated flux/residual kernel
  any device time-stepping loop
```

## 4. The finding that decides whether BITWISE equivalence is even possible

```text
MeshGeometry assigns face ids from a monotonically increasing counter, and calls cell.addFace() as
each face is created:
    src/mesh/MeshGeometry.cpp:456,480,827,858   const Index faceId = nextFaceId++;
    src/mesh/Cell.cpp:34                        void Cell::addFace(Index id) { faceIds_.push_back(id); }

addFace appends without sorting -- but because the ids only ever increase, EVERY CELL'S FACE LIST IS
ASCENDING BY CONSTRUCTION.
```

```text
WHY THAT IS THE WHOLE BALLGAME. The CPU assembles the residual by looping over faces in ascending
id order and doing r[owner] += F, r[neighbour] -= F. So cell P's residual is a sum of its own faces'
contributions IN ASCENDING FACE-ID ORDER.

A GPU kernel that GATHERS per cell over cellFaceIds visits exactly the same faces in exactly the
same order. Floating-point addition is deterministic for a fixed order, so the residual can be
BITWISE identical rather than merely close -- and no atomics are needed, which is separately what
makes the GPU residual reproducible run to run (authorization §21).

The alternative -- a scatter with atomicAdd -- would have been both non-bitwise AND
non-deterministic. The gather is chosen for correctness, not for speed.
```

## 5. Thermodynamics on device: exactly what blocks it, and why it is not a blocker

```text
std::vector occurrences in the thermodynamic chain:
  TotalEnergy.hpp                   0
  Enthalpy.hpp                      0
  ThermodynamicProperties.hpp       0
  IdealGasEOS.hpp                   0
  TemperatureDependentProperty.hpp  3   <-- the only host-only link
```

`TotalEnergyState` holds a `SensibleEnthalpy`, which holds a `TemperatureDependentProperty`, which
is `std::vector`-backed and cannot cross to the device. But the recovery this solver performs is a
**closed form** and never touches the polynomial:

```text
src/compressible/TotalEnergy.cpp:120   T = (e - h_ref + cp*T_ref) / cv
src/compressible/TotalEnergy.cpp:171   p = eos.pressure(rho, T)  ->  p = rho * R * T
```

So the device needs five scalars — `R`, `cp`, `cv`, `T_ref`, `h_ref` — and nothing else.

```text
DECISION: a small POD `DeviceThermodynamics` carries those five, and the device recovery is written
from the same two closed forms. P17.5's TotalEnergy.cpp is NOT refactored to share them, because
§2 forbids changing CPU numerics for GPU convenience and that file is P17.5 production code with its
own frozen evidence. §4 permits separate implementations "if a direct cross-comparison test is
added", so one is: a test drives TotalEnergyState::recover() and the device form over a matrix of
states and requires BITWISE agreement.
```

## 6. Two capabilities deliberately kept HOST CONTROL ONLY

```text
residual norms        a volume-weighted L2 = a SUM over cells. Summation is order-dependent, so a
conservation sums     parallel tree reduction cannot be bitwise-equal to the CPU's sequential sum.
                      Making the device reduction sequential to force a match would be engineering
                      the answer rather than implementing the operation.

DECISION: the device loop needs only the global dt, which is a MIN -- associative, commutative and
order-independent in IEEE, so it CAN be bitwise. Norms and conservation are computed on the host
from the conserved state, exactly as the CPU does, and are therefore identical by construction when
the state is. This keeps the loop resident (only a dt scalar crosses per step) and keeps every
equivalence claim honest. It is recorded here as a decision, not discovered later as a limitation.
```

## 7. The numerical policy inherited, not invented

```text
cuda/CMakeLists.txt:93    -fmad=false on the twenty-odd existing kernels
cuda/CMakeLists.txt:134   -fmad=false on DeviceHllcFluxKernel.cu specifically
measured by P17.7:  --fmad=false  17/17 bitwise, max |flux diff| exactly 0
                    --fmad=true    8/17 bitwise, max |flux diff| 1.526e-04 (1.9e-15 relative)
```

Authorization §30: *"If P17 requires `-fmad=false` for established equivalence semantics, preserve
the existing policy."* The new kernels are compiled with the same flag, for the same reason, and the
flag is **not** introduced to force agreement — it predates this phase by two phases.

## 8. Minimal kernel set to be implemented

```text
K1  recoverPrimitives      conserved -> primitive, per cell, closed form + validity flag
K2  assembleResidual       per-cell GATHER over cellFaceIds: reconstruct, hllcFlux, integrate,
                           accumulate with +F owner / -F neighbour. No atomics.
K3  localTimeStep          per-cell sum over faces of (|u.n| + a) A, then dt = CFL V / sum
K4  globalTimeStep         a MIN reduction -- order-independent, so bitwise-safe
K5  applyUpdate            Q - (dt/V) R, per cell
K6  validateState          rho > 0, finite Q, recoverable -- the SAME decisions as the CPU

NOT implemented on device, and why:
  residual norms / conservation sums   §6 above
  the retry / rejection control loop   host control logic; the DECISIONS are compared instead
  restart                              UNSUPPORTED on CPU; §34 requires preserving the refusal
```

## 9. One probe of mine that misreported

```text
Section 5 of the audit script printed "std::vector occurrences in TotalEnergy.hpp: 0" under a
heading saying that is "why it cannot cross to device as-is" -- which reads as though the zero were
the blocker, when a zero there means the opposite. The blocker is three directories down, in
TemperatureDependentProperty.hpp. The chain is walked explicitly in §5 above instead. Recorded
rather than silently reworded, because a heading that contradicts its own number is exactly the kind
of thing that gets quoted later as evidence for the wrong conclusion.
```
