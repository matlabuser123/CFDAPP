# P17.5 — the discrete total-energy formulation

Consolidates what the authorization suggested as `state_definition.md`, `formulation.md`,
`pressure_work.md`, `conduction.md`, `viscous_work.md` and `boundary_energy.md`, following the
convention P17.1–P17.4 already established in this repository: fewer, denser documents whose claims
each carry their measurement.

---

## 1. The state, and the naming that avoids the `E` collision

TODO.md writes `E = rho e_t`, while much of the literature uses `E` for the *specific* total energy.
A bare `E` is never used:

```text
e        specific internal energy           [J/kg]
ke       specific kinetic energy            [J/kg]
eTotal   specific total energy              [J/kg]
rhoE     conserved total-energy density     [J/m^3]
h        specific static enthalpy           [J/kg]
hTotal   specific total enthalpy            [J/kg]
```

```text
h(T)      = h_ref + integral_{T_ref}^{T} cp dtheta      P15 SensibleEnthalpy, CLOSED FORM
e(T)      = h(T) - R T                                  <-- the defining choice
ke        = 0.5 |U|^2
eTotal    = e + ke
rhoE      = rho eTotal
hTotal    = h + ke
```

`e := h - R T` is load-bearing rather than one option among several. It makes `h - e = R T` **exact
by construction for any reference state** (measured exactly 0 at `T_ref = 0` *and* at
`T_ref = 298.15`), gives `de/dT = cp - R = cv` automatically, preserves P15's zero point instead of
redefining it, and makes `h = e + p/rho` the same statement as the EOS rather than a second one to
reconcile. The full derivation and the measurements are in `thermodynamic_contract.md`.

And then, with no further assumption:

```text
rho hTotal = rho (h + ke) = rho (e + R T + ke) = rhoE + p
```

which is why the convective flux may be written as either `(rhoE + p) U` or `m_dot_f hTotal_f`.

### Ownership and synchronisation, stated because §5 asks

```text
storage            rhoE is a ScalarField the CALLER owns. TotalEnergyState is a stateless
                   converter -- it holds only the EOS and the enthalpy model, never a field. There
                   is therefore no second, unsynchronised copy of the energy anywhere.
initialization     conservedTotalEnergyField(mesh, rho, T, U, state)
old-time storage   the caller keeps rhoE_old; totalEnergyAccumulation takes both explicitly
update path        advanceTotalEnergyExplicit -> recoverPrimitiveFields -> (T, p) for the next step
boundary values    hTotal on boundary faces is a SurfaceField argument, not derived internally, so
                   an inlet total enthalpy is prescribed by the caller and is visible in evidence
CPU ownership      authoritative. There is no GPU path -- see section 8.
serialization      NOT wired. rhoE has no case-format key and no export field; see section 8.
```

**One state, one direction of travel.** The synchronisation rule is that `rhoE` is the conserved
variable and `(T, p)` are *derived* from it by `recoverPrimitiveFields`, never the other way round
within a step. The failure mode §5 warns about — temperature saying one thing and `rhoE` another —
is structurally impossible here because nothing else stores an energy.

## 2. The discrete equation, for cell P

```text
(rhoE_new - rhoE_old) V / dt
  + sum_f  m_dot_f hTotal_f                 convective + PRESSURE work
  + sum_f  q_f . n_f A_f                    conduction
=  sum_f (tau . U)_f . n_f A_f               viscous work
  + rho f . U V                             body-force power
  + S_E V                                   volumetric source
```

**Sign convention, stated once.** Every `...Divergence` function returns the **net outflow out of
the cell, integrated over the cell volume, in watts**. A positive value means the cell is losing
energy by that mechanism. The balance is therefore

```text
accumulation + convectiveOutflow + conductiveOutflow
  - viscousWorkInflow - bodyForcePower - volumetricSource  =  imbalance
```

## 3. Transient — `d(rho eTotal)/dt`, not `rho d(eTotal)/dt`

```text
(rhoE_new - rhoE_old) V / dt
```

The two conserved states are subtracted directly. For variable density the two forms differ, and a
test in which **both** `rho` and `eTotal` change is what distinguishes them. Measured, with
`rho: 1 -> 2`, `T: 300 -> 500 K`, `U = (10,0)`, `dt = 0.25`, `V = 1`:

```text
implemented form   (rhoE_new - rhoE_old) V/dt
the WRONG form     rho_new (eTotal_new - eTotal_old) V/dt
the two differ by  > 1, asserted by TransientTermUsesSeparateOldAndNewDensity
```

## 4. Convective flux — and why pressure work cannot be forgotten

```text
F_E,f = m_dot_f hTotal_f          upwinded on the mass flux
```

The `m_dot_f` is **the P17.3 authoritative mass flux, passed in as an argument**, not recomputed.
Because `rho hTotal = rhoE + p`, this single term *is* `(rhoE + p) U`: the pressure work is inside
the flux rather than a separate term that could be omitted. That is the structural reason to carry
`hTotal`.

Measured, on a 5×4 mesh with `T = 300 + 20x`:

```text
m_dot_f hTotal_upwind  vs  (rhoE + p)_upwind (U.n) A       agree to <= 8 eps, every internal face
dropping p, i.e. transporting eTotal instead              changes the answer by > 1e-6 relative
```

The second line is the pressure-work test: it is the assertion that would fail if `p U` were missing.
Control **N3** plants exactly that defect in production code.

```text
UPWIND ONLY, first order. The same limitation P17.4 recorded for momentum convection; no
higher-order energy scheme exists on this path.
```

**No double counting.** The existing P15 thermal path is a *separate* equation for temperature; it
is not summed with this one. Nothing in P17.5 adds a pressure-work term to P15, and P15's own
`cp*T` transport is untouched.

## 5. Conduction — and the operator choice that a failure forced

```text
q = -k grad T,   so the energy equation carries -div(q) = +div(k grad T)
and this function returns the OUTFLOW:  sum_f q_f . n_f A_f
```

```text
internal face   Df = discretization::internalFaceDiffusionTerms(...).coefficient
                outflow_owner += Df (T_P - T_N);  outflow_neighbour -= the same double
boundary face   {diagonal, source} = thermal::boundaryDiffusionContribution(..., Df)
                outflow = diagonal * T_P - source
```

**This is deliberately NOT `discretization::diffusion`, and the reason is measured.** That operator's
boundary branch fits a quadratic through (boundary, owner, opposite-neighbour), so a prescribed
**zero** gradient yields a **non-zero** flux on a curved profile. Built on it, a closed "adiabatic"
box leaked 5.64 W and drifted 1.15e-9 relative in 20 steps — 2.4e15 eps, not round-off. P15's
assembler instead assembles the prescribed flux *exactly* (its own header: "zero for Adiabatic"),
which is what a conservation statement requires. Full diagnosis:
`AMENDMENT_conduction_boundary.md`.

Measured after the correction:

```text
sum(conductiveOutflow) over a closed zero-gradient domain    2.66e-15  = 1.9 eps
                                             before the fix  5.6361    = 2.4e15 eps
a UNIFORM temperature conducts                               EXACTLY 0 through every face
T = c x^2, constant k, x-interior cells                      outflow = -2 k c V, exact
```

The quadratic field is used rather than a linear one on purpose: a linear `T` gives exactly zero
conduction and so cannot test the **sign**, which is the one thing easy to get backwards here.

`k` comes from P15. No new conductivity correlation is introduced; `k(T)` support is noted in §8.

## 6. Viscous work — carried forward as limited, not papered over

```text
the conservative term is   sum_f (tau . U)_f . n_f A_f
```

**P17.4 does not build the stress tensor.** It assembles `mu lap(U)` plus an optional
`-(2/3) mu grad(div U)`; there is no `tau_ij`, so `(tau . U) . n` cannot be formed exactly from it.
Two things follow and both are stated rather than finessed:

```text
NOT DONE   substituting tau : grad(U). That is the INTERNAL-energy form of the dissipation term,
           not the total-energy form, and swapping it in would silently change which equation is
           being solved while every internal check still passed.
DONE       viscousWorkLimitation() reports the shortfall in machine-readable form, the balance
           takes viscous work as an explicit caller-supplied field, and a null term is reported as
           EXACTLY 0 rather than folded into the imbalance.
```

So P17.5 does not claim a more complete total-energy model than the momentum model supports — which
is what §13 of the authorization asks for in its last paragraph.

## 7. Body-force power and the volumetric source

```text
rho f . U V   per cell [W]
```

Measured: `U = 0` gives **exactly 0** (a zero `dot` is exact); doubling `rho` doubles the power
exactly; falling with gravity adds energy (positive power), which fixes the sign.

```text
S_E V   per cell [W], units W/m^3 before the volume factor
```

Measured in a closed motionless adiabatic domain: `d(rhoE)/dt = S_E` exactly, and over 10 steps
`dE_domain = S_E V_domain dt n_steps` to within the derived bound.

## 8. Boundary energy, and what is NOT reachable

```text
BC type                 how each quantity is obtained
--------------------------------------------------------------------------------------------
adiabatic / zero grad   conduction EXACTLY 0 (section 5). Convection 0 if the mass flux is 0.
fixed temperature       conduction from Df (T_P - T_b) with T_b prescribed; the two-point form
                        is used, so the wall flux is first order -- the same honesty P17.4
                        recorded for its Dirichlet wall
prescribed heat flux    assembled exactly, diagonal = 0 and source = Df g d
inlet                   hTotal on the face is a SurfaceField the CALLER prescribes, so an inlet
                        total enthalpy is explicit and visible rather than inferred
outlet                  upwind takes the owner cell's hTotal, the convention the P17.3 mass flux
                        already uses for density
symmetry / wall         zero normal mass flux, so zero convective energy; conduction as above
```

An **adiabatic, impermeable, stationary** wall therefore carries zero energy by both mechanisms:
convection because `m_dot_f = 0`, conduction because the prescribed flux is assembled exactly.

```text
NOT REACHABLE FROM A CASE FILE, and so NOT shipped capability:
  rhoE has no case-format key, no initial-condition entry and no export field. The whole
  total-energy path is library-only, the same status pseudoTimeStep and
  includeDilatationalStress already have. P15's SensibleEnthalpy -- which this path is built on --
  is ITSELF unreachable from a case (audit.md 3b), so wiring P17.5 to the case format requires
  wiring P15's enthalpy model first. Recorded as follow-on work, not claimed here.
NOT PRESENT: periodic and mass-flow-inlet boundaries do not exist in this codebase at all.
NOT SUPPORTED: k(T). Conduction takes a constant k. P15's k(T) model exists and is reused-ready,
  but a variable-k conductive FLUX needs a face-interpolated k, which is new discretisation and
  was left out rather than guessed at.
NOT SUPPORTED: cp(T). Rejected at construction with a named error, because the EOS block stores a
  single cp from which it derives cv and gamma -- see thermodynamic_contract.md 1.
2D ONLY: the compressible path is 2D. |U|^2 = u^2+v^2+w^2 is computed from Vector3 with z = 0, so
  the inactive dimension is handled by the type rather than by a branch, but no 3D claim is made.
```
