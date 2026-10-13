# P17.10 — kernel inventory

Authorization §3. Written **before any kernel source**, from a line-by-line reading of the CPU
solver, not from its comments. Every expression below was copied out of the production file and its
line number recorded, because §2 says *"do not port code blindly"* and the only way to honour that
is to name what is being ported.

CPU source of record: `src/compressible/DensityBasedSolver.cpp`,
sha256 `53e84ac7b5f53c6d6704e8a7a77281dd678ae18f450d74862abcce60d8421326` (P17.8's frozen value,
re-verified at the head of this step).

---

## 1. What does NOT need porting, because it is already one source

```text
include/cfd/compressible/HllcFlux.hpp carries 20 CFD_HLLC_HOST_DEVICE markers. The ones this
phase needs, all compiled by BOTH gcc and nvcc from that same text:

  line 181  validateState              line 237  integrateOverArea
  line 198  validateUnitNormal         line 252  hllcShockFactor
  line 213  eulerFlux                  line 257  hllcOuterSpeeds
  line 225  toConserved                line 280  hllcFlux
  line  85  dotHD                      line 419  hllFluxForControlOnly
  line  55  absR / 63 sqrtR / 71 finiteR / 79 minR / 80 maxR
  line 470  reconstructFirstOrder      line 481  reconstructFirstOrderBoundary

CONSEQUENCE: K2a below restates ZERO formulas, exactly as P17.7's DeviceHllcFluxKernel.cu does.
The Riemann solver is not ported at all -- it is merely called from a different compiler.
```

## 2. The device-side constant block

```text
K0  DeviceThermodynamics -- a POD of 9 doubles, uploaded once per solve.

    R       thermo.gasConstant()                        p = rho R T, and e = h - R T
    cp      thermo.specificHeatPressure()               the T recovery
    cv      thermo.specificHeatVolume()                 the T recovery
    gamma   thermo.specificHeatRatio()                  a = sqrt(gamma R T), and HLLC's gamma
    tRef    enthalpy.referenceTemperature()             the T recovery
    hRef    enthalpy.referenceEnthalpy()                the T recovery
    tMin    cpModel.minimumTemperature()                the range REJECTION, see trap 7
    tMax    cpModel.maximumTemperature()                the range REJECTION, see trap 7
    eFloor  COMPUTED ON THE HOST by calling the production
            TotalEnergyState::specificInternalEnergy(tMin), then uploaded as a number.
```

```text
WHY eFloor IS UPLOADED RATHER THAN RECOMPUTED. It is enthalpy(tMin) - R*tMin, and enthalpy() runs
the cp polynomial's antiderivative -- std::vector-backed, host-only. Rather than restate that
evaluation on device (where a different grouping of h_ref + cp*tMin - cp*tRef would be a silent
last-bit difference), the host evaluates the production function once and ships the result. A
constant cannot drift. This is the cheapest possible way to make one more quantity bitwise by
construction rather than by argument.

WHY THE CLOSED FORMS ARE EXACT AT ALL. TotalEnergyState's constructor REFUSES a temperature-
dependent cp outright (src/compressible/TotalEnergy.cpp:48, if (!cpModel.isConstant()) throw).
The gas is calorically perfect by enforcement, so h = h_ref + cp (T - T_ref) holds exactly and the
recovery below is the algebraic inverse rather than an approximation to one.
```

## 3. The kernels

### K1 — `recoverPrimitives`, one thread per cell

Reproduces `makePrimitiveStateFromConserved` -> `TotalEnergyState::recover`, whose arithmetic lines
are `src/compressible/TotalEnergy.cpp:141,143,145,146` plus `:120` and `:171`:

```text
  velocity   = { mx/rho, my/rho, mz/rho }                          exact per-component division
  ke         = 0.5 * dot(velocity, velocity)                       TotalEnergy.cpp:89
  eTotal     = rhoE / rho
  e          = eTotal - ke
  REJECT if !finite(e) || e < eFloor                               nothing is clamped
  T          = (e - hRef + cp * tRef) / cv                         TotalEnergy.cpp:120
  REJECT if T outside [tMin, tMax]                                 trap 7
  p          = (rho * R) * T                                       IdealGasEOS.cpp:66
  a          = sqrt((gamma * R) * T)                               ThermodynamicProperties.cpp:40
  then validateState() -- ALREADY host/device, HllcFlux.hpp:181
```

```text
THIS IS THE ONLY KERNEL THAT RESTATES A CPU FORMULA. §4 permits that "if a direct cross-comparison
test is added", and one is -- see §5 below. The parenthesisation above is not cosmetic: the CPU
writes `density * gasConstant_ * temperature`, which is (rho*R)*T left to right, and
`specificHeatRatio_ * gasConstant() * temperature`, which is (gamma*R)*T. Writing rho*(R*T) instead
would differ in the last bit, and the cross-comparison test is what would catch it.
```

### K2a — `computeFaceFluxes`, one thread per **face**

```text
  normal  = { s * Sx/A, s * Sy/A, s * Sz/A } with s = +1        DensityBasedSolver.cpp:35-38
  interior face : reconstructFirstOrder(prim[owner], prim[neighbour])
  boundary face : reconstructFirstOrderBoundary(prim[owner],
                      exteriorState(treatment[f], prim[owner], normal))
  flux    = integrateOverArea(hllcFlux(L, R, normal, gamma).flux, faceArea[f])
  store 5 doubles per face
```

### K2b — `gatherResidual`, one thread per **cell**

```text
  acc = 0
  for k in cellFaceOffsets[c] .. cellFaceOffsets[c+1]:      ASCENDING, by construction
      f    = cellFaceIds[k]
      sign = (faceOwner[f] == c) ? +1 : -1
      acc += sign * faceFlux[f]                              5 components
```

```text
WHY TWO KERNELS RATHER THAN ONE. A single fused per-cell kernel would evaluate every interior
face's Riemann problem TWICE -- once for each side. It would still be bitwise (same inputs, same
code, deterministic), but it would break the property the CPU header states in its own words:
"ONE hllcFlux evaluation per face produces all five components", and authorization §6's prohibition
on recomputing the convective fluxes independently is about exactly that discipline. The split
keeps one evaluation per face AND keeps the accumulation order identical. It also halves the flux
work, but that is a side effect, not the reason.
```

### K3 — `localTimeStep`, one thread per cell

```text
  speedSum = 0
  for each face f of c, ASCENDING:                          DensityBasedSolver.cpp:292-311
      normal   = outwardNormal(f, iAmOwner)                 SIGN FLIPS HERE -- trap 2
      faceValue = interior ? meanForCfl(prim[c], prim[other]) : prim[c]
      speedSum += (|dot(faceValue.velocity, normal)| + faceValue.speedOfSound) * faceArea[f]
  REJECT if !(speedSum > 0) || !finite(speedSum)
  dt[c] = cfl * cellVolume[c] / speedSum
  REJECT if !finite(dt[c]) || !(dt[c] > 0)
```

### K4 — `globalTimeStep`, a **min** reduction

```text
  smallest = +infinity; smallest = min(smallest, dt[c]) over all cells
  REJECT if !finite(smallest) || !(smallest > 0)

min is associative and commutative in IEEE-754 for non-NaN operands, so ANY reduction tree gives
the CPU's sequential answer bitwise. This is the one reduction in the device loop, and it is the
reason the loop can stay resident with only a scalar crossing per step. A parallel SUM would not
have this property, which is why residual norms and conservation sums stay on the host
(audit.md §6) rather than being made "fast".
```

### K5 — `applyUpdate`, one thread per cell

```text
  scale = dt[c] / cellVolume[c]                             DensityBasedSolver.cpp:342
  rho   = rho  - scale * r.rho
  rhoU  = rhoU - scale * r.rhoU        componentwise
  rhoE  = rhoE - scale * r.rhoE
```

### K6 — `validateState`, one thread per cell, producing the SAME decisions

```text
  the K1 rejections, plus finiteness of every conserved component, reduced to
    (a) an admissible/inadmissible flag per cell, and
    (b) the FIRST offending cell in ascending id order -- a min-index reduction, order-independent
        for the same reason K4 is.

The CPU reports the first offender in deterministic id order (the firstUnrecoverableCell helper
P17.8 added after P5 found offendingCell hard-coded to 0). A device reduction that returned ANY
offender would compare "equal" on a one-offender case and diverge on a two-offender case -- which
is precisely the kind of instrument that passes until it matters. The min-index is taken, not the
first-seen.
```

## 4. Seven traps found by reading the CPU source, each of which a naive port gets wrong

```text
TRAP 1  THE NEIGHBOUR DOES NOT GET ITS OWN NORMAL.
        assembleResidual computes outwardNormal(face, fromOwner=true) ONCE per face and gives the
        neighbour the NEGATED flux (DensityBasedSolver.cpp:238, 264-271). Re-evaluating HLLC from
        the neighbour's side with its own outward normal and swapped states is mathematically the
        negation -- and is NOT bitwise the negation. K2a/K2b use the owner normal always.

TRAP 2  ...BUT localTimeSteps DOES FLIP THE NORMAL, per cell (DensityBasedSolver.cpp:294). The two
        kernels must NOT share one normal helper on autopilot. K3 keeps the s = iAmOwner ? 1 : -1
        factor, which is harmless there only because |u.n| discards the sign afterwards.

TRAP 3  THE WALL REBUILDS rhoE RATHER THAN COPYING IT. exteriorState subtracts the interior kinetic
        energy and adds the mirrored one (DensityBasedSolver.cpp:206-212) even though |U| is
        unchanged in exact arithmetic, because it is NOT unchanged in floating point. "Simplifying"
        that to ext.rhoE = int.rhoE is the obvious optimisation and it breaks bitwise equality.

TRAP 4  PRESSURE COMES FROM THE EOS, NEVER FROM p = (gamma-1)(rhoE - ke). P17.5 measured the gamma
        form 83% wrong under a nonzero enthalpy reference (TotalEnergy.cpp:186 comment). The device
        uses rho*R*T like the host, and reaches T first.

TRAP 5  meanForCfl AVERAGES THE SOUND SPEED ITSELF, not the temperature
        (DensityBasedSolver.cpp:50-58). 0.5*(a_L + a_R) is not sqrt(gamma R (0.5(T_L+T_R))).
        Recomputing a from the mean temperature is the physically tidier choice and the wrong one.

TRAP 6  AN UNMAPPED BOUNDARY PATCH IS A HARD ERROR, not a wall. boundaryByFace throws for a patch
        with no treatment and for a boundary face in no patch (DensityBasedSolver.cpp:62-83). The
        device upload builds its per-face treatment array THROUGH that same host function, so the
        refusal is inherited rather than reimplemented -- and a device path that silently defaulted
        to a wall would produce a plausible wrong answer, which is the failure mode §29 is about.

TRAP 7  enthalpy(T) IS CALLED FOR ITS EXCEPTION, NOT ITS VALUE.
        temperatureFromInternalEnergy computes T in closed form and then evaluates
        (void)enthalpy_.enthalpy(temperature) purely to reuse P15's range check
        (TotalEnergy.cpp:126-128, the (void) and all). Reading that line as dead code and dropping
        it would make the GPU ACCEPT states the CPU REFUSES -- a divergence in the admissible set,
        not in a number. K1 reproduces it as a [tMin, tMax] test, which is exactly what
        TemperatureDependentProperty::evaluate enforces.
```

## 5. How each restatement is proved, not asserted

```text
restated on device            proof obligation
------------------------------------------------------------------------------------------------
K1's recovery                 drive TotalEnergyState::recover() and the device form over a matrix
                              of conserved states; require BITWISE equality of v, T, p, a, AND
                              identical accept/reject decisions including the reject reason.
exteriorState                 drive the production free function and the device form over all 3
                              boundary kinds x a set of interior states and normals; BITWISE.
outwardNormal, meanForCfl     covered by K3's per-cell dt comparison, which is BITWISE and would
                              not survive a changed operand order in either.
everything else               NOT RESTATED. One source; nothing to prove beyond the equality the
                              equivalence suite already measures.
```

```text
AND THE SECOND HALF OF THAT CHAIN, which a cross-comparison test alone would miss. The device
functions are __host__ __device__, so a host-compiled call to them proves only that the
RESTATEMENT matches the CPU -- not that nvcc's DEVICE code matches nvcc's HOST code. Both halves
are measured separately:
    host production        vs  device header compiled for HOST    -- catches a mis-transcription
    device header on HOST  vs  device header on DEVICE            -- catches a codegen difference
A single end-to-end comparison conflates the two and, when it fails, names neither.
```

## 6. What is deliberately NOT a kernel

```text
residual norms, conservation sums   order-dependent SUMS; host-side, audit.md §6
the retry / rejection control loop  host control logic; the DECISIONS are compared instead
restart                             UNSUPPORTED on CPU and it refuses; §34 requires preserving the
                                    refusal, not building a GPU restart the CPU has no counterpart
                                    for
LocalPseudoTime mode                K3 produces the per-cell dt already; no separate kernel
```
