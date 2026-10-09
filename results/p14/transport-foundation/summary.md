# P14 — Generic Transport Foundation: summary

**The audit overturned the premise twice.** CFDApp did not need a transport framework built — it
needed one **unlocked**. A fully generic scalar transport assembler already existed on the CPU, and
the two hard halves already existed on the GPU. P14 is a promotion, not a construction, and that is
what makes its central gate provable rather than merely testable.

```text
audit       results/p14/transport-foundation/audit.md
environment WSL2 Ubuntu 22.04, i9-14900HX, gcc 11.4, CUDA 12.9, RTX 5000 Ada (sm_89)
```

---

## 1. What the audit found

```text
scalar CONVECTION assemblers already in the tree       4
scalar DIFFUSION  assemblers already in the tree       5
scalar SOURCE     assemblers already in the tree       2
a generic transport layer (src/transport)              0  -- absent
GPU scalar-transport kernels, counted by filename      0 of 18
```

**First overturn (CPU).** `cfd::turbulence::assembleScalarTransportEquation` already did variable
diffusivity, mass-flux convection, Patankar Su/Sp and polymorphic scalar BCs. Its own header calls
it *"model-agnostic assembly, model-specific coefficients"*. It was unreachable: living in
`cfd::turbulence`, thermal/species/multiphase could not use it without depending on the turbulence
model.

**Second overturn (GPU).** My audit said *"Phase I is genuinely new work, not a port"*. **That was
half wrong.** Counting kernels by filename missed that scalar transport is already ported inside
the diffusion and convection kernels:

```text
assembleScalarDiffusionDevice   ports turbulence::assembleScalarDiffusionContribution
assembleScalarConvectionDevice  ports thermal::assembleThermalConvectionContribution
```

Those are exactly the two functions the host path composes. So the device work was also
composition, not porting.

## 2. What was built

```text
include/cfd/transport/ScalarTransport.hpp   the promoted generic equation
src/transport/ScalarTransport.cpp           same loops, same order, same primitives
include/cfd/gpu/DeviceScalarTransport.hpp   device assembly
cuda/kernels/DeviceScalarTransportKernel.cu source, transient, merge, pattern guard
include/cfd/discretization/TimeDerivative.*  + ScalarField-density overload (additive)
docs/developer_guide/transport.md           the equation set and how to consume it
```

Genuinely new, because it did not exist anywhere: the **conservative variable-density transient**
term, the **generic source API**, the **harmonic face-diffusivity option**, and the **composition**
on both backends.

**Nothing was migrated.** `thermal/`, `species/`, `turbulence/`, `multiphase/` and
`pressure_velocity/` are untouched. They move onto this layer in P15–P19.

## 3. The golden gate — bitwise, by construction

The risk P14 exists to avoid is a **second finite-volume convention** in a codebase with nine
scalar assemblers and a bitwise standard. Because the layer was promoted rather than rewritten,
the requirement is bitwise equality, not a tolerance:

```text
TransportFoundation.LegacyMatrixEquivalence                             PASS  bitwise
TransportFoundation.LegacyMatrixEquivalenceWithNonOrthogonalCorrection  PASS  bitwise
```

CSR row offsets, column indices, values and RHS all compared with `EXPECT_EQ` on doubles.

## 4. Numerical formulation, as implemented

```text
transient    a_P += rho^{n+1} V/dt ;  b_P += rho^n phi^n V/dt     (conservative)
convection   upwind on owner-oriented F_f (already carries rho), coefficient 1
diffusion    Gamma_f |S_f| / d
Gamma_f      arithmetic (dNf*gP + dPf*gN)/(dPf+dNf)      DEFAULT, the existing convention
             harmonic   d_PN / (d_Pf/gP + d_fN/gN)       opt-in, for discontinuous Gamma
source       a_P -= Sp V ; b_P += Su V                    Patankar, Sp<=0 validated never clamped
Dirichlet    D_b = Gamma_f A_f/d_Pb ; a_P += D_b ; b_P += D_b phi_b
Neumann      prescribed flux enters b_P
```

## 5. Verification — CPU

```text
UNIT (21/21)
  transient one-cell hand calculation            a_P = 120, b_P = 336
  old-time RHS uses rho^n not rho^{n+1}          rho^n=2 vs rho^{n+1}=5, factor 2.5 apart
  timestep sensitivity                            dt/10 -> 10x diagonal
  field-density overload == scalar overload      BITWISE
  Patankar sign convention                        a_P gains -Sp*V, b_P gains Su*V
  positive Sp rejected on request, never clamped
  harmonic == 2 gP gN/(gP+gN) at a midpoint face; harmonic < arithmetic
  harmonic is NOT the default                     default is bitwise the arithmetic path
  BC type changes the system; uniform field exact under all-Neumann
  repeated assembly                               BITWISE

ANALYTICAL + CONSERVATION (10/10)
  Case 1  steady diffusion, linear                max error < 1e-9, nx = 10/20/40
  Case 2  diffusion + source, parabolic           vs S x(L-x)/2Gamma; sign control passes
  Case 3  convection-diffusion                    bounded, monotone, direction correct
  Case 4  transient diffusion                     monotone approach; dt 0.05 vs 0.0125 agree 1e-6
  Case 5  variable Gamma                          face flux constant to 5e-3
  Phase L global balance                          4 configurations, normalised < 1e-8
```

## 6. Verification — CPU/GPU equivalence (Phase J)

```text
                                 structure   values bitwise   max diff        rhs
steady (diff+conv+source)        OK          5408/5860        1.421e-14 (2 ulp)   0 ulp
transient (all four terms)       OK          5411/5860        1.421e-14 (2 ulp)   0 ulp
diffusion + convection only      OK          5408/5860        1.421e-14 (2 ulp)   0 ulp
diffusion + source only          OK          5860/5860        0.000e+00 (0 ulp)   0 ulp
determinism (device x2)          BITWISE IDENTICAL
                                              (40x30 grid; 6x5 and 16x12 also run)

solution equivalence     Linf        relL2       conservation host / device
  6x5                    5.684e-14   1.296e-16   4.922e-11 /  4.976e-11
  16x12                  4.626e-10   7.243e-13   1.350e-10 / -1.402e-10
  40x30                  1.722e-09   2.087e-12  -4.663e-09 /  2.871e-09
```

**Why it is 2 ULP and not 0, stated precisely.** Not FMA — the kernel is in
`cuda/CMakeLists.txt`'s `-fmad=false` list like every qualified kernel. Not a discretisation
difference. It is **association order**: the host accumulates `((d1+d2+..)+c1)+c2+..` into one
builder, while the device adds each term's completed total. Off-diagonals receive one contribution
per term and are therefore exact — which is why the source-only system is **100% bitwise**, and
that asymmetry is the evidence for the explanation. Forcing bitwise would require fusing convection
into the diffusion kernel face-by-face; 2 ULP does not justify it.

## 7. Residency (Phase B12) — a defect found and fixed

The first implementation checked the two sparsity patterns by downloading both index arrays and
comparing on the host. The audit measured what that cost:

```text
BEFORE   per assembly:  H2D 0 calls    D2H 4 calls, 609 KB
AFTER    per assembly:  H2D 1 call (4 B)   D2H 1 call (4 B)
```

B12's rule is that per-iteration transfers need a documented blocker. There was no blocker, only a
lazy implementation: the check now runs as a kernel and returns a single flag. **609 KB -> 4 bytes,
guard fully intact.** No field data (`phi`, `Gamma`, `Su`, `Sp`) or matrix values cross the bus
during assembly at all.

```text
80x80, 20 assemblies    CPU 7.84 ms each    GPU 2.20 ms each    17 kernels per assembly
```

## 8. Preservation

```text
P13-001 SIMPLE fingerprint, 10 cases    IDENTICAL -- P14 moved no bits in the solver
```

Run against the frozen P13-001 baseline. A passing CTest suite does not prove the numbers are the
same bits; this does.

## 9. Mistakes made and corrected, recorded because they shaped the result

1. **Solving once was wrong.** The Dirichlet treatment is a *deferred* reconstruction evaluated
   from the current field, so the system depends on the iterate it was built around. Assembling
   about `phi = 0` and solving gave **max error 231 against an analytical range of 400**. The
   harness now iterates, as production does.
2. **Case 5's metric was bad, not its result.** Comparing rise across half-intervals excluded the
   steep wall-to-first-centre region where Gamma is smallest. Replaced with face-flux constancy,
   which is what conservation actually means here.
3. **Absolute convergence tolerance** of 1e-12 on a field of magnitude 500 is ~9 ULP — unreachable.
   It passed on order-one cases and failed only on the 100..500 one. Now relative.
4. **A probe script re-ran a stale binary** after a failed compile, because it tested `[ -x ]` on a
   path that still held the previous build. Same shape as P13-007's silently-unregistered test. The
   script now deletes the binary first and checks the compiler's exit status.
5. **A regression was invalidated by my own rebuilds** mid-run and re-run from a settled tree; the
   compromised log is kept as `final_INVALID_rebuilt_midrun.log`.
6. **Two header comments went stale** against measurement (a wrong function name, and a claim that
   bitwise was not expected when it largely is). Corrected against the numbers.

## 10. Known limitations

* **Robin / mixed BC not implemented.** Needed for `-k dT/dn = h(T - T_inf)`; belongs with P15.
* **BDF2 not implemented.** `TransportTimeState` carries one old level, not two.
* **Layering debt:** the convection contribution delegates to `thermal::` with `cp = 1`, inherited
  from the turbulence path. `transport -> thermal` should reverse when P15 migrates thermal.
* **Harmonic interpolation is not used by any production caller yet** — it is tested and available.
* **No case-file representation** for a generic transport equation; deliberately not invented
  before a physics module needs one.
* **2 ULP diagonal difference** CPU vs GPU, explained above, not eliminated.
