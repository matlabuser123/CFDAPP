# P17.7 — implementation: states, the physical flux, and the interfaces

Consolidates what §36 suggested as `state_definition.md`, `physical_flux.md`,
`reconstruction_interface.md` and the boundary-state interface, following the convention P17.1–P17.6
established (one focused document per concern rather than one per section heading).

---

## 1. The delta

```text
include/cfd/compressible/HllcFlux.hpp        NEW  ~430 lines  -- ALL of the numerics, host + device
src/compressible/HllcFlux.cpp                NEW  ~120 lines  -- the factory and throwing wrappers
include/cfd/gpu/DeviceHllcFlux.hpp           NEW             -- the device launcher's declarations
cuda/kernels/DeviceHllcFluxKernel.cu         NEW             -- marshalling only, NO physics
tests/unit/compressible/test_hllc_flux.cpp   NEW  33 TESTs
tests/unit/compressible/hllc_reference_data.hpp  GENERATED   -- the independent 60-digit reference
tests/unit/gpu/test_hllc_gpu_equivalence.cpp NEW   4 TESTs
4 CMakeLists.txt files edited                src/, cuda/, tests/unit/compressible/, tests/unit/gpu/
PRODUCTION FILES MODIFIED                    0 -- every P17.1-P17.6 source is UNTOUCHED by SHA-256
```

```text
THE CMAKE LINE COUNTS NEED CARE, and a first draft of this document got them wrong. `git diff HEAD`
on those four files reports +42 / +40 / +8 / +51 -- but nothing since P12 is committed, so those totals
are the UNION of every uncommitted phase, not P17.7's contribution. Measured by content instead
(lines mentioning hllc or fmad):
      src/CMakeLists.txt                          +42 uncommitted, of which P17.7:  3
      cuda/CMakeLists.txt                         +40 uncommitted, of which P17.7: 12
      tests/unit/compressible/CMakeLists.txt       +8 uncommitted, of which P17.7:  4
      tests/unit/gpu/CMakeLists.txt               +51 uncommitted, of which P17.7:  5
This is the same trap that makes `git diff --numstat HEAD` unusable as an "untouched by this phase"
predicate anywhere in P17, which is why every such claim in this phase rests on SHA-256 instead.
```

## 2. §2 — the conserved state

```cpp
struct ConservedState {
  Real density{};              // rho                            [kg/m^3]
  Vector3 momentumDensity{};   // rho U = (rho u, rho v, rho w)  [kg/(m^2 s)]
  Real totalEnergyDensity{};   // rhoE = rho e_t                 [J/m^3]
};
```

§2 warns against a name where `E` could mean either the specific total energy or the total-energy
density. **P17.5 had already settled this** and P17.7 inherits it verbatim: `rhoE` /
`totalEnergyDensity` is always the density in J/m³, and a bare `E` appears nowhere in the file. Units
are on every field, as §2 requires.

### The conversions are P17.5's, not new ones

§2: *"Reuse the P17.5 primitive↔conserved conversion. Do not implement independent conversion logic
inside HLLC."*

```text
primitive -> conserved   TotalEnergyState::conservedTotalEnergy(rho, T, U)   via makePrimitiveState()
conserved -> primitive   TotalEnergyState::recover(rho, rhoU, rhoE)          via
                                                       makePrimitiveStateFromConserved()
```

`toConserved()` in the header is the one piece of arithmetic P17.7 owns, and it is not a conversion in
the thermodynamic sense: it multiplies velocity by density and copies `rhoE` through. It introduces no
equation of state and no energy relation.

```text
MEASURED, criterion D4_ConservedRoundTripIsExact:
  density and rhoE round-trip BITWISE
  temperature and pressure within 8 eps (they pass through the closed-form inverse and the EOS)
```

`recover()` also supplies the validation §8 asks for, and its policy is inherited deliberately: it
throws on a non-positive or non-finite density, a non-finite `rhoE` or momentum, kinetic energy
exceeding the available total energy, or a temperature outside the property model's range — **and it
clamps nothing and applies no energy floor.** A clamped state corresponds to an energy the solver does
not have.

## 3. §3 — the primitive state, and why it carries `rhoE`

```cpp
struct PrimitiveState {
  Real density{};             // rho   [kg/m^3]
  Vector3 velocity{};         // U     [m/s]
  Real pressure{};            // p     [Pa]    ABSOLUTE, from P17.2's IdealGasEOS
  Real temperature{};         // T     [K]
  Real speedOfSound{};        // a     [m/s]   from ThermodynamicProperties::speedOfSound
  Real totalEnergyDensity{};  // rhoE  [J/m^3] from TotalEnergyState::conservedTotalEnergy
};
```

Carrying `rhoE` alongside the primitives is a deliberate design choice, and it is what makes §3's
prohibition *structurally* satisfiable rather than a matter of discipline:

```text
the physical flux needs rhoE (the energy component is u_n (rhoE + p))
if the state did not carry it, the flux would have to RECONSTRUCT it -- which needs an EOS
so the state carries it, and the flux layer contains no thermodynamics at all
```

```text
D2, verified structurally: 0 occurrences in HllcFlux.hpp/.cpp of a pressure formula, a sound-speed
formula, or (gamma-1)*. gamma enters only as an argument, used solely for Toro's shock factor q_K.
```

### One authoritative path, enforced by construction

`makePrimitiveState()` is the only place a state acquires `p`, `a` and `rhoE`, and it does so by
calling the authoritative models. Criterion `D4_FactoryAgreesWithDirectConstruction` asserts this
**bitwise**:

```text
viaFactory.speedOfSound        == thermo.speedOfSound(300.0)                     BITWISE
viaFactory.pressure            == thermo.equationOfState().pressure(1.2, 300.0)  BITWISE
viaFactory.totalEnergyDensity  == air.conservedTotalEnergy(1.2, 300.0, U)        BITWISE
```

so the factory demonstrably adds nothing of its own.

```text
ONE HONEST QUALIFICATION. The DIMENSIONLESS literature cases (Sod's rho=1, p=1) imply T = 0.0035 K,
far outside any physical property range, and the factory rightly REFUSES them. Those states are
therefore built directly in the test, and D4_FactoryAgreesWithDirectConstruction exists precisely to
license that: it proves direct construction is identical to the factory for a realistic state. The
atmospheric Sod variant exercises the factory path on the same physical configuration.
```

### D3 — the zero-reference cross-check

The independent reference uses `rhoE = p/(gamma-1) + 0.5 rho |U|^2`, which is exact only under the
zero reference (`h_ref = T_ref = 0`) — a point P17.5 measured at length. Rather than assume the two
agree, criterion D3 checks it over four states:

```text
|conservedTotalEnergy(rho,T,U) - (p/(gamma-1) + ke)|  <=  8 eps |rhoE|,  4 of 4 states
```

## 4. §4 — the directional physical flux

```text
  F_n(Q) = [ rho u_n,
             rho u u_n + p n_x,
             rho v u_n + p n_y,
             rho w u_n + p n_z,
             u_n (rhoE + p) ]^T          with u_n = U . n
```

One implementation for an arbitrary unit normal; there is no x-only version and no duplicated y/z
variant. §4's requirement that the 1D x-formula emerge as the `n = (1,0,0)` special case is asserted
**bitwise**:

```text
F1_XNormalIsTheTextbookOneDimensionalFlux   all 5 components EXPECT_EQ against
                                            rho u, rho u^2 + p, rho u v, rho u w, u(rhoE + p)
F2_AxisNormalsPermuteTheFluxBitwise         x <-> y <-> z permutation, BITWISE
F3_MatchesTheIndependentReference           worst |diff|/scale = 1.114e-17  (bound 8 eps = 1.776e-15)
```

`dotHD()` reproduces `cfd::dot` bit for bit, **including** its deliberate handling of an exactly-zero
`z` product (see `Vector3.hpp`), so a 2D dot product stays bitwise equal to the two-term formula down
to the sign of a zero. It is reproduced rather than called because `cfd::dot` is host-only — and
`Vector3`, a P12 core type, is not modified.

## 5. §20 — the normal convention, chosen and enforced

```text
THE INTERFACE TAKES A UNIT NORMAL AND A SEPARATE SCALAR AREA.
  hllcFlux(left, right, unitNormal, gamma, estimator)   -> flux PER UNIT AREA
  integrateOverArea(flux, area)                         -> the integrated flux
```

The two are separate functions so that an area provably cannot reach the wave speeds. §20 warns
against mixing the conventions, so the unit property is **checked**, not assumed:

```text
validateUnitNormal():  | |n|^2 - 1 | <= 16 eps  ->  else StateValidity::NonUnitNormal
  16 eps derived: |n|^2 is three products and two sums (~5 eps), doubled for the squaring, with
  headroom for a normal built by dividing by an inexact magnitude.
MEASURED, P2: an AREA VECTOR (length 2) is rejected; so are lengths 0, 0.5, (1,1,0) and a NaN
component. 5 invalid normals, 5 refusals.
MEASURED, R3: area 0.25/1/4/1024 scales the flux EXACTLY (powers of two) and leaves S_L, S*, S_R
BITWISE unchanged.
```

Control V6 removes the unit check, which is how that criterion is known to have teeth.

## 6. §6, §7, §22, §23 — the reconstruction interface

```cpp
struct ReconstructedFaceState { PrimitiveState left; PrimitiveState right; };
enum class ReconstructionMode { FirstOrder };

reconstructFirstOrder(owner, neighbour)          -> {owner, neighbour}
reconstructFirstOrderBoundary(interior, exterior) -> {interior, exterior}
```

The separation §22 asks for is real and checkable: **`HllcFlux.hpp` contains no mesh access at all**,
so the flux layer *cannot* reconstruct cells itself even by accident. Responsibilities stay
`reconstruction -> Riemann solver -> numerical flux`.

```text
MEASURED, FirstOrderModeIsOwnerAndNeighbour:
  the face states are BITWISE the cell states -- nothing is interpolated (piecewise constant)
  and hllcFluxChecked(face.left, face.right, n) == hllcFluxChecked(owner, neighbour, n), bitwise,
  i.e. the core genuinely consumes the reconstructed states rather than reaching around them
```

§7's first-order robust mode is the only mode implemented, as §6 directs (*"Do not implement a full
MUSCL/WENO scheme unless already present and trivially exposable"* — the audit found 0 MUSCL/WENO
implementations, only a comment). Adding MUSCL later means adding a `ReconstructionMode` and a
function that fills `ReconstructedFaceState`; **`hllcFlux()` does not change**, because it never
learns where its states came from.

## 7. §24 — the boundary-state interface

```text
boundary condition  ->  exterior primitive/conserved state  ->  HLLC
```

`reconstructFirstOrderBoundary(interior, exterior)` takes the exterior state **from the caller**. No
boundary condition is hard-wired into the flux layer, and none is implemented here — supplying it is
P17.8's job. The test drives it with a synthetic exterior state (a velocity-reversed mirror, i.e. a
wall-like condition) and confirms the resulting mass flux vanishes by symmetry:

```text
BoundaryFaceTakesAnExteriorStateFromTheCaller: mass flux 0.000e+00 for an opposed symmetric pair
```

That is the *"small synthetic boundary-state harness"* §23 permits, and it is not a boundary-condition
implementation.

## 8. §8 — failure semantics, defined explicitly

Because the shared core must compile as device code, it **cannot throw**. So:

```text
the shared core     validateState() / validateUnitNormal() return a typed StateValidity;
                    hllcFlux() returns HllcResult{valid=false, validity=<reason>} and computes nothing
the host wrapper    hllcFluxChecked() translates that into cfd::InvalidArgumentError, with a message
                    naming the offending quantity via describeValidity()
the device          returns the same integer reason code, compared against the host's in
                    G2_InadmissibleInputsAreRefusedIdenticallyOnDevice (8 cases, same code)
```

```text
MEASURED: 11 inadmissible states and 5 invalid normals plus 2 invalid gammas are each refused with
the correct typed reason, and the thrown message contains that reason's text. Nothing is clamped,
no floor is applied, and there is no fallback -- see wave_speeds.md section 4.
```

## 9. §34 — how the five components map onto P17.3/P17.4/P17.5

```text
F[0]  mass flux        rho u_n                -> the quantity P17.3's continuity conserves
F[1..3] momentum flux  rho U u_n + p n        -> the quantity P17.4's momentum equation transports
F[4]  energy flux      u_n (rhoE + p)         -> P17.5's conservative total-energy flux, and the
                                                 identity rho hTotal = rhoE + p is P17.5's own
```

The energy component is literally P17.5's convective total-energy flux: P17.5 established
`rho hTotal = rhoE + p`, so `u_n (rhoE + p)` is `m_dot hTotal` per unit area. P17.7 does not restate
that relation, it consumes it.

```text
§34 AND §35, EXPLICITLY: CompressibleSIMPLE is NOT replaced and is NOT routed through HLLC. The two
architectures coexist. For the density-based solver, the inviscid HLLC flux will replace the
segregated convective treatment IN ITS OWN PATH -- which does not exist yet and is P17.8's.
VERIFIED: 0 references to HLLC from any SIMPLE path; every SIMPLE and P17.1-P17.6 source UNTOUCHED by
SHA-256; the P13-001 SIMPLE fingerprint BITWISE IDENTICAL.
```
