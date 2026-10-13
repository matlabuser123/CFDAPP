# P17.0 — P14 generic transport reuse map

What P14 actually provides, and which P17 equations may use it. Read from the
implementation, not from the P14 summary.

Verdicts use the authorization's three classes:

```text
SAFE DIRECT REUSE          use as-is, no change to P14
REUSE WITH ADAPTATION      the shape fits, something must be added alongside
NOT SEMANTICALLY COMPATIBLE forcing it through would be wrong
```

---

## 1. What P14 is

One header and one source: `include/cfd/transport/ScalarTransport.hpp`,
`src/transport/ScalarTransport.cpp`. It assembles **one scalar** equation into one CSR
system.

```text
assembleScalarTransportEquation(mesh, phi, massFlux, diffusivity, boundaries,
                                Su, Sp, nonOrthogonal = {}, timeState = {},
                                interpolation = Arithmetic)
   -> ScalarTransportAssembly { system, diagonal }
```

Assembly order is **diffusion, convection, source, transient** and its own header
records why that is load-bearing: `SparseMatrixBuilder` accumulates, floating-point
addition is not associative, so the order is part of the numerical result.

### The pieces

```text
assembleScalarDiffusionContribution   field diffusivity; faceDiffusivity() with
                                      Arithmetic or Harmonic interpolation
                                      (Harmonic is flux-preserving across a
                                      discontinuity in the coefficient)
assembleScalarConvectionContribution  consumes an arbitrary massFlux SurfaceField and
                                      never assumes how it was computed
applyImplicitScalarSource             Patankar splitting:
                                        b_P += Su_P V_P     (explicit)
                                        a_P -= Sp_P V_P     (implicit, linearised)
applyTransientContribution            conservative implicit Euler with TWO densities:
                                        a_P^time = rho_P^{n+1} V_P / dt
                                        b_P^time = rho_P^n phi_P^n V_P / dt
```

`TransportTimeState` carries `rhoOld` **and** `rhoNew` separately, and the header
states why: using `rho^{n+1} phi^n` on the right-hand side is **not** conservative
when density changes, and is "the single easiest mistake to make here".

Supporting infrastructure, all already generic:

```text
applyImplicitUnderRelaxation   pressure_velocity/UnderRelaxation.hpp -- already used
                               by CompressibleRelaxedMomentum, so already proven on a
                               compressible path
SparseMatrix::fixRow           imposed-value rows; used by ScalarTransport, KEpsilon,
                               KOmega, SST and DeviceScalarTransport
boundaries                     polymorphic cfd::boundary::ScalarBoundaryCondition;
                               the assembler only ever calls
                               boundaryValue(ownerValue, normalDistance)
nonOrthogonal                  NonOrthogonalCorrectionOptions, shared with momentum
GPU                            DeviceScalarTransport -- assembles all four
                               contributions into ONE CSR system on device; the
                               system never leaves the device
```

## 2. Verdict per P17 equation

```text
P17 equation                          verdict                      why
---------------------------------------------------------------------------------------
P17.5 total energy, SEGREGATED         REUSE WITH ADAPTATION        see §3
  as a scalar transport of e_t or h_t
P17.5 conduction  -div(q)              SAFE DIRECT REUSE            k(T) as the
                                                                    diffusivity field
P17.5 transient  d(rho e_t)/dt         SAFE DIRECT REUSE            rhoOld/rhoNew is
                                                                    exactly this form
P17.5 pressure work, viscous work,     SAFE DIRECT REUSE            Su/Sp sources
  body-force work
P17.1 variable density                 NOT APPLICABLE               density is an
                                                                    input to P14, not
                                                                    a transported phi
P17.3 compressible continuity          NOT APPLICABLE               it is a constraint
                                                                    on pressure, not a
                                                                    scalar transport
P17.4 compressible momentum            ALREADY REUSED ELSEWHERE     momentum uses
                                                                    physics::Momentum-
                                                                    Equation, not P14
P17.7 HLLC / Riemann flux              NOT SEMANTICALLY COMPATIBLE  see §4
P17.8 density-based solver             NOT SEMANTICALLY COMPATIBLE  see §4
```

## 3. P17.5 segregated energy — reuse with adaptation

The conservative total-energy equation splits cleanly onto P14's four slots:

```text
d(rho e_t)/dt              -> applyTransientContribution, phi = e_t,
                              rhoOld / rhoNew   SAFE DIRECT REUSE
div(rho e_t U)             -> assembleScalarConvectionContribution with the existing
                              compressible mass flux   SAFE DIRECT REUSE
-div(q) = div(k grad T)    -> assembleScalarDiffusionContribution, diffusivity = k
                              field from P15's TemperatureDependentProperty
                              SAFE DIRECT REUSE
div(p U), div(tau.U),      -> Su / Sp   SAFE DIRECT REUSE of the mechanism
  rho f.U, S_E
```

Note the algebraic identity that makes this work:

```text
div[(rho e_t + p) U]  =  div(rho e_t U)  +  div(p U)
                         ^ scalar convection   ^ a source term
```

**What must be added alongside, and is NOT in P14:**

```text
1. temperature recovery  T from e -- P14 transports phi and never inverts it.
                         P15's SensibleEnthalpy::temperature(h) is the right
                         machinery; see p15_reuse_map.md.
2. the diffusion driver  P14 diffuses phi itself. Conduction is driven by grad(T),
                         not grad(e_t). With phi = e_t these are different operators
                         unless cp is constant. This is the exact trap P15's
                         Enthalpy.hpp documents for cp*T versus h(T), and it recurs
                         here one level up. It must be resolved explicitly, not by
                         assuming cv is constant.
3. energy BCs            adiabatic wall, fixed total enthalpy, and an inflow that
                         sets e_t consistently with p, T and U. The polymorphic BC
                         interface accepts new types, so this is additive.
4. kinetic energy        0.5|U|^2 is not a scalar P14 knows about; it couples the
                         energy equation to the momentum solution each iteration.
```

Item 2 is the one most likely to be got wrong quietly, and it is the reason this row
is ADAPTATION rather than DIRECT.

## 4. Why the Riemann / density-based route is NOT compatible

The authorization warns specifically against forcing `div(rho U phi)` through a scalar
abstraction when a coupled conserved-state Euler flux is required. Measured against
P14, the warning is correct on four independent counts:

```text
1. COUPLING AT A FACE
   assembleScalarTransportEquation assembles ONE scalar system and CONSUMES a mass
   flux that was computed elsewhere. An HLLC flux computes all five conserved
   components' fluxes TOGETHER from the left and right full states, and the mass flux
   is an OUTPUT of that computation, not an input to it. The data flow is reversed.

2. DENSITY'S ROLE IS REVERSED
   In P14 rho is an input field (rhoOld/rhoNew) used to weight the transient term. In
   a density-based solver rho is the FIRST conserved unknown, evolved by the same flux
   as the others. P14 has no slot for that.

3. WAVE-SPEED ESTIMATION NEEDS THE FULL STATE
   S_L, S_R and S* are functions of (rho, u, p, a) on both sides of the face. A
   per-scalar assembler never has the other components in scope.

4. POSITIVITY IS A JOINT PROPERTY
   "Preserve positivity of density and pressure" constrains a combination of
   conserved components. Per-scalar clipping cannot express it, and would silently
   destroy conservation if it tried.
```

Conclusion: **P17.7 and P17.8 need a new coupled conserved-state path.** P14 is not a
foundation for it and should not be bent into one.

## 5. A sequencing risk this map exposes

P17's working order puts P17.5 (total energy) **before** P17.7 (HLLC) and P17.8
(density-based solver). The natural way to build P17.5 is segregated on P14 — which
§3 shows is a good fit.

But a density-based solver evolves `rho e_t` as a **conserved component of a coupled
state**, not as a segregated scalar. So a segregated P17.5 energy equation may not be
reusable by P17.8, and the energy physics could end up implemented twice.

```text
option A   build P17.5 segregated on P14 now; accept that P17.8 will need its own
           conserved-state energy update. Two implementations of the same physics,
           which CLAUDE.md warns against, but each simple and separately validated.
option B   build P17.5 directly as a conserved-state equation, so P17.8 inherits it.
           Larger step, no P14 reuse, and it puts the coupled-state design decision
           before the evidence from P17.6 is in.
option C   build P17.5 segregated, but factor the thermodynamic closure
           (e_t <-> T <-> p, temperature recovery, kinetic-energy split) into its own
           unit that BOTH routes consume. The closure is the part that must not be
           duplicated; the assembly can differ.
```

This is a design decision for the P17.5 authorization, not something P17.0 may settle.
**Recorded rather than resolved.** Option C is the one that avoids duplicating the
physics without forcing the coupled-state decision early, and is what this audit would
recommend if asked.

## 6. GPU

`DeviceScalarTransport` already ports all four contributions and composes them into a
single device-resident CSR system, so a segregated energy equation has a genuine GPU
route available. **GPU compressible work is outside the authorized P17 scope**, so
this is recorded as available, not planned.
