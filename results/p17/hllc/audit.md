# P17.7 — audit of the existing Riemann / Euler-flux surface

`logs/00_audit.log`, run at HEAD `3bd32f918656f471e72b5f6ac7cb6c2cbf75a274` before any code was
written. Searched roots: `include src cuda tests apps`.

---

## 1. THE FINDING: there is no Riemann layer, and the two apparent hits are comments

P17.0 recorded *"0 hits for HLLC/HLL/Rusanov/conserved-state; the 14 Roe and 107 Euler hits are false
positives"*. That is a claim from an earlier phase. It is **re-verified here**, and it holds — but the
verification is stronger than a hit count, because a hit count cannot distinguish an implementation
from a comment.

```text
term                 files  lines   what they actually are
------------------------------------------------------------------------------------------
HLLC                     0      0   absent
HLL (whole word)         0      0   absent -- searched with -w so "HLLC" cannot inflate it
Rusanov                  0      0   absent
Lax-Friedrichs           0      0   absent
Godunov                  0      0   absent
shock                    0      0   absent from the source tree
ConservedState           0      0   absent (struct and class forms both)
PrimitiveState           0      0   absent (struct and class forms both)
wave speed / waveSpeed   0      0   absent
contact speed            0      0   absent
WENO                     0      0   absent
slope limiter / minmod   0      0   absent
numericalFlux            0      0   there is NO numerical-flux abstraction to extend
interfaceFlux            0      0   absent

Riemann                  1      1   A COMMENT. include/cfd/compressible/EquationOfState.hpp:51
                                    "...the energy equation and the Riemann flux to come..."
                                    i.e. P17.2 anticipating this phase. Not an implementation.
MUSCL                    1      1   A COMMENT. include/cfd/discretization/Convection.hpp:120
                                    notes an existing INCOMPRESSIBLE scheme is "equivalent to van
                                    Leer's MUSCL". A scalar-convection scheme, not a compressible
                                    reconstruction, and not reachable as one.
```

### The "Roe" and "Euler" hits, classified rather than dismissed

```text
Roe    16 lines, but as a WHOLE WORD: 0.
       Every hit is inside another word. A Roe average or Roe flux would necessarily appear as an
       identifier -- `roeAverage`, `RoeFlux`, `roe_` -- i.e. as a word. Zero whole-word hits is
       therefore not a weak signal, it is decisive.

Euler  101 lines, classified by what they are:
         95   backward / forward / implicit / explicit Euler TIME INTEGRATION
          0   "Euler equation", "Euler flux", "Euler system", "Euler state"
          0   Euler as an identifier (eulerStep and similar)
       So not one hit concerns the Euler EQUATIONS. The word is present only as the name of a time
       integrator, which is a different Euler entirely.
```

```text
CONCLUSION: P17.7 is a GREENFIELD implementation. There is nothing to extend and therefore no risk
of creating a second Riemann abstraction -- section 1's warning does not bind, because there is no
first one. This is also what the frozen G6 criterion already says under `prerequisite`.
```

## 2. What DOES exist, and must be reused rather than duplicated

This is the other half of the audit, and the more important half: the thermodynamics P17.7 needs are
already built and verified.

```text
P17.2  ThermodynamicProperties          include/cfd/compressible/ThermodynamicProperties.hpp
         gasConstant() R, specificHeatPressure() cp, specificHeatVolume() cv = cp - R,
         specificHeatRatio() gamma = cp/cv
         speedOfSound(T) = sqrt(gamma R T)          <- THE authoritative sound speed
         density(p, T), machNumber(speed, a)
       IdealGasEOS                       pressure(rho, T), temperature(p, rho), density(p, T)

P17.5  TotalEnergyState                 include/cfd/compressible/TotalEnergy.hpp
         conservedTotalEnergy(rho, T, U) -> rhoE           PRIMITIVE -> CONSERVED
         recover(rho, rhoU, rhoE) -> RecoveredState        CONSERVED -> PRIMITIVE
           { velocity, specificKineticEnergy, specificInternalEnergy, temperature, pressure }
           pressure is ABSOLUTE and comes from P17.2's EOS, NEVER from the gamma form
         temperatureFromInternalEnergy(e)  closed form, exact
         conservedTotalEnergyField / recoverPrimitiveFields   the field-level pair
```

```text
SO: HLLC NEEDS NO EOS OF ITS OWN, and section 3's prohibition is satisfiable exactly. Pressure,
temperature and sound speed are all produced by the P17.2/P17.5 path and only CONSUMED by the flux.
```

`recover()` is also already the validating gate section 8 asks for: it throws
`InvalidArgumentError` if density is not finite and positive, if `rhoE` or momentum is not finite, if
kinetic energy exceeds the available total energy, or if the recovered temperature leaves the property
model's range — **and it clamps nothing and applies no energy floor**, by explicit P17.5 policy.

## 3. Requirement classification (§1)

```text
item                                          classification   note
-----------------------------------------------------------------------------------------------
Define conserved state vector                 MISSING          no ConservedState of any kind
Define inviscid physical flux                 MISSING          0 "Euler flux" hits
Implement left/right reconstructed states     MISSING          no face-state abstraction
Implement wave-speed estimates                MISSING          0 hits
Implement HLLC flux                           MISSING          0 hits
Handle contact wave                           MISSING          0 hits
Preserve positivity of density and pressure   NEEDS EXTENSION  P17.5 recover() already validates
                                                               and refuses to clamp; the STAR-state
                                                               admissibility is what is missing
Add first-order robust mode                   MISSING          no reconstruction layer exists
Define reconstruction interface               MISSING
Add exact / analytic flux tests               MISSING
Add discontinuity tests                       MISSING
Verify rotational / directional consistency   MISSING
Verify CPU/GPU equivalence                    MISSING          see section 5
-----------------------------------------------------------------------------------------------
ALREADY SATISFIED: none of the thirteen. 1 NEEDS EXTENSION, 12 MISSING.
The THERMODYNAMICS they rest on, however, is ALREADY SATISFIED in full -- section 2.
```

## 4. The dimensional question, settled before it becomes a trap

The scope asks for `rho w` and `n_z`. The mesh is 2D, so at first sight the z components are
unavailable. They are not:

```text
include/cfd/core/Vector2.hpp:12   using Vector2 = Vector3;
```

`Vector2` is an **alias** of `Vector3`, and a 2D mesh stores `z = 0` in every vector. So a state
carries three velocity components as a matter of type, and the flux can be written and tested in full
3D form with an arbitrary unit normal — including `n = (0,0,1)` — **at the flux level**, which is
exactly the level P17.7 operates at. What is *not* available is a 3D mesh to drive it from; that is a
mesh-layer limitation and belongs to nobody in P17.

```text
So sections 4, 16E and 19 are answerable in full, and the honest boundary is: the FLUX is verified in
3D; there is no 3D MESH to run it on. That distinction is recorded here so the summary cannot blur it.
```

## 5. CPU/GPU — the scope conflict, named rather than silently resolved

```text
P17_GATES.md G8 says, as of P17.0:  "GPU compressible work is outside the authorized P17 scope"
                                    and marks G8 VACUOUS/DEFERRED.
P17.7's authorization says:          sections 27, 28, 29, 42 require a GPU implementation and
                                    CPU/GPU equivalence.
```

The authorization is the newer and more specific instruction, so GPU HLLC is **in scope for this
item**, and the gates document's blanket line is superseded for it. Flagging this rather than quietly
overriding a frozen gate document.

What the audit found about the GPU tree:

```text
no __host__ __device__ macro exists anywhere in include/ or cuda/
the project convention is SEPARATE .cu kernels that re-express the maths
0 GPU files mention HLLC, Riemann, ConservedState or a compressible solver
   (the single "compressible" hit is a comment saying a kernel ports the INCOMPRESSIBLE assembly)
```

```text
CONSEQUENCE, and it drives the architecture: section 28 says to avoid copy-pasted mathematical logic
"if a common host/device-compatible implementation is practical". For a POINTWISE flux with no mesh
and no solver state, it is not merely practical, it is the obvious design -- so P17.7 will introduce
ONE shared CFD_HOST_DEVICE implementation rather than the project's usual duplicated-kernel pattern.

That also changes what G8 demands: G8 says "where the operation order is identical, BITWISE is the
correct requirement and anything looser is a concession that must be argued for". With one source the
order IS identical, so BITWISE is the target, not a tolerance.

The one obstacle is FMA CONTRACTION: nvcc contracts a*b+c into an FMA by default while the host x86-64
build does not, which would break bitwise equality for a reason that has nothing to do with the
physics. Handled at the equivalence step and measured BOTH ways, so the cost of contraction is
reported rather than hidden behind a tolerance.
```

### Vector3 cannot be reused inside device code, and it is not being touched

```text
include/cfd/core/Vector3.hpp defines its operators, dot, cross, magnitude and isFinite as plain
`inline` functions -- NOT __host__ __device__ -- so device code cannot call them. It is a P12 core
type used by the entire codebase and P17.7 will not modify it.
RESOLUTION: the shared flux core holds Vector3 (a POD aggregate, which device code may construct,
copy and read members of) but does its arithmetic on the .x/.y/.z components, with explicit
host/device shims for sqrt, abs and isfinite. No core file changes.
```

## 6. One probe of my own that misreported, recorded because it is the pattern I keep hitting

```text
logs/00_audit.log section 1g prints
    soundSpeed    declared in: <blank>
which reads as "the project has no sound speed". It has one: the symbol is `speedOfSound`, and it is
in ThermodynamicProperties.hpp. My probe named it wrongly and printed a blank, and a blank looks
exactly like absence.
No consequence here -- section 2 identifies the real API from the header directly -- but it is the
same fail-open shape as the four instrument defects P17.6 found, so it is logged rather than fixed
silently.
```

## 7. What P17.7 therefore has to build

```text
include/cfd/compressible/HllcFlux.hpp      ConservedState, PrimitiveState, FluxVector,
                                           eulerFlux(), hllcFlux(), wave speeds, star states,
                                           branch enum -- ALL CFD_HOST_DEVICE, no EOS of its own
src/compressible/HllcFlux.cpp              the host-only validating entry points that throw, plus
                                           the P17.2/P17.5-backed PrimitiveState factory
cuda/kernels/DeviceHllcFluxKernel.cu       the device side, calling the SAME header functions
tests/unit/compressible/test_hllc_flux.cpp the analytic, branch, contact, rotation, positivity,
                                           conservation, determinism and negative tests
tests/.../test_hllc_gpu_equivalence.cpp    the section 29 state matrix, CPU vs GPU
results/p17/hllc/tools/exact_riemann.py    the INDEPENDENT exact Riemann solver G6(f) requires --
                                           arbitrary precision, no CFDApp code, written outside the
                                           build entirely
```

G6(f) is the criterion that cannot be satisfied by any amount of internal consistency: the reference
must be computed **outside CFDApp**, and adopting CFDApp output as the reference is explicitly
forbidden. That script is therefore part of the deliverable, not a convenience.
