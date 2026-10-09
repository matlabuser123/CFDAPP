# P17.0 — the exact remaining P17 implementation gap

The authorization's 24 questions, answered from traced code. This file is meant to
determine the real shape of P17 after audit.

Evidence: `compressible_inventory.md`, `test_inventory.md`, `capability_matrix.md`,
`p14_reuse_map.md`, `p15_reuse_map.md`.

---

## The 24 questions

**1. Is variable density already production integrated?**
YES, both modes. `coupled: false` (the default) evaluates it post-hoc from the
converged incompressible result; `coupled: true` iterates it inside
`CompressibleSIMPLE`. Reachable from `physics.json`, exercised by
`cases/compressible_validation` and `cases/compressible_channel_coupled`.

**2. Is density iterated, derived, or both depending on solver?**
**Always derived**, never authoritative: `rho = EOS(p_abs, T)`. Whether it is
*iterated* depends on the mode:

```text
coupled: false   derived ONCE, after the incompressible solve converged. It does not
                 influence velocity or pressure at all.
coupled: true    derived EVERY outer iteration from the just-corrected pressure
                 (CompressibleSIMPLE.cpp:455), and fed back through the
                 pressure-correction compressibility term -- genuinely iterated state.
```

It is never an independently evolved conserved variable. That is the single most
important structural fact for P17.8.

**3. Is ideal-gas EOS production integrated?**
YES for `density`, `dDensityDPressure`, `dDensityDTemperature`, `R`, `cp`, `cv`,
`gamma`, `speedOfSound`, `machNumber`. **NO for `pressure(rho, T)`** — P17.2 added it
and nothing calls it yet, which is correct at this point and is recorded so no later
item mistakes it for integration.

**4. Does EOS support all state recovery needed for higher Mach?**
**PARTIAL.** It inverts `p <-> rho` at a **given T**, which is what a pressure-based
solver needs. A density-based solver recovers state from the conserved vector instead:

```text
have    rho = f(p,T),  p = f(rho,T),  a = sqrt(gamma R T),  constant cv and gamma
NEED    p from (rho, e)      i.e. p = (gamma - 1) rho e     -- no path exists
        T from e             -- no path exists (see Q13)
        h_t, e_t             -- do not exist as quantities
        cv(T), gamma(T)      -- only as CONSTANTS derived from a constant cp
```

The gap is not the p-rho relation; it is that **temperature is an input everywhere**,
and a conserved-state solver has to produce it as an output.

**5. Is compressible continuity conservative?**
**Where it is in production, yes.** The steady constraint `div(rho U) = 0` is enforced
through the pressure-correction equation using **one authoritative face mass flux**
(`calculateCompressibleMassFlux` / `evaluateCompressibleFaceDensity`), so the discrete
face fluxes telescope and global conservation holds. `|globalNetFlux|` is reported in
metadata and gated in the low-Mach suite.
**The transient form is not in production at all** — see Q7.

**6. Is compressible momentum conservative?**
**NO, not as the compressible momentum equation.** Three of four terms are:

```text
d(rho U)/dt        YES  rho_new u_new and rho_old u_old kept separate; reduces exactly
                        to the incompressible formula at constant density (tested)
div(rho U x U)     YES  flux-based on the compressible mass flux, telescoping.
                        UPWIND ONLY, first order.
-grad(p)           YES
div(tau)           NO   the INCOMPRESSIBLE Laplacian mu lap U is used. The
                        dilatational term -(2/3) mu grad(div U) is ABSENT:
                        grad(div 0 hits, dilatation 0, bulkViscosity 0, twoThirds 0,
                        Stokes 0; and test_mms_compressible.cpp names
                        "mu/3 grad(div U) stress term" in its own not_covered field.
```

Harmless through P16, because `div U = 0` there. Compressible flow is exactly where
`div U != 0`.

**7. Is transient compressible continuity already implemented?**
**As a non-production DIAGNOSTIC only.** `evaluateCompressibleContinuity` computes the
residual of `(rho^{n+1}-rho^n)V/dt + sum(mDot_f)` from an already-computed flux. Its
own header calls it a diagnostic. Callers: its own `.cpp`, 6 unit tests, one line of
`test_low_mach_regression.cpp`. **No production caller.** `CompressibleSIMPLE` calls
the steady `physics::evaluateContinuity` instead (`CompressibleSIMPLE.cpp:478`). It is
never *solved* for density.

**8. Is transient compressible momentum already implemented?**
**The term exists and is used; physical unsteadiness does not.**
`compressibleMomentumTimeDerivative` is called via
`assembleRelaxedCompressibleMomentumComponent`, but in a **pseudo-transient
(dual-time)** framing: `pseudoTimeStep` is "a numerical stabilization parameter, not a
physical time step" (`CompressibleSIMPLE.hpp:27`), and `densityOld == densityNew` at
the call site. There is no time-accurate compressible solve.

**9. Does a genuine total-energy equation exist?**
**NO.** `totalEnergy`, `TotalEnergy`, `rhoE`, `rho_e`, `internalEnergy`: 0 hits in the
whole tree. P15's `thermal/EnergyEquation.hpp` is a **temperature-transport** equation,
steady or transient, and is not classified as a total-energy equation.

**10. Is rhoE or an equivalent conserved total-energy variable evolved?**
**NO.** 0 hits. Nothing evolves it.

**11. Does pressure work already exist?**
**NO.** `pressureWork`: 0 hits. `div(pU)` appears nowhere.

**12. Does viscous work / dissipation already exist?**
**NO**, not in an energy balance. `viscousDissipation`, `viscousWork`: 0 hits; and 0
hits for `dissipat` anywhere under `include/cfd/thermal`, `include/cfd/physics`,
`src/thermal`, `src/physics`.
*False positive ruled out:* `dissipat` does appear under `include/cfd/turbulence` and
`src/turbulence` — that is the turbulent dissipation rate epsilon of the k-epsilon
model, a turbulence closure quantity, **not** `div(tau . U)` in an energy equation.

**13. Is temperature recoverable from conserved energy?**
**NO** — but the machinery to do it correctly exists next door. P15's
`SensibleEnthalpy` provides exact closed-form `h(T)` and `T(h)` by bounded bisection,
with an explicit tolerance and an **error-not-clamp** policy on out-of-range input.
`T(e)` needs `e(T) = h(T) - R T` (one subtraction, closed form inherited) plus a new
admissibility check: monotonicity of `e(T)` requires `cv = cp - R > 0`, which the
property model does **not** currently guarantee (it guarantees only `cp > 0`).

**14. Does a conserved-variable state representation already exist?**
**NO.** `conservedVariable`, `primitiveVariable`: 0 hits. No state-vector type, no
primitive-conserved conversion.

**15. Does any Euler numerical flux already exist?**
**NO.** All 107 `Euler` hits are `implicitEulerTimeDerivative`,
`aleImplicitEulerTimeDerivative` and `ImplicitEuler*` test names — **time
integration**, never the Euler equations.

**16. Does any HLL / HLLC / Roe / Rusanov solver already exist?**
**NO.** `HLLC` 0, `HLL` 0, `Rusanov` 0, `LaxFriedrichs` 0.
`Roe` returns 14 hits and **every one is a substring of "zeroes" / "zeroed" / "Zero"**.
`Riemann` returns 1 hit: a forward-looking comment at `EquationOfState.hpp:51` that
P17.2 itself wrote. A bare symbol census would have concluded this capability partly
exists. It does not.

**17. Does any CFL-based compressible time-step control exist?**
**NO control. A reporter exists and is partially reusable.**

```text
exists      cfd::solver::CFL -- CFLResult{maxCFL, meanCFL, maxCFLCell},
            Co_P = (dt/(2 V_P)) * sum_faces(|massFlux_f|/rho), from the authoritative
            face mass flux. Callers: PISO, TransientSolver.
its own     "diagnostic only -- this does not modify dt or suggest a value... No
words       adaptive time stepping here or implied by this API."
limitation  ADVECTIVE only. A compressible explicit update is limited by the ACOUSTIC
            speed, (|u| + a) dt / dx, and there is no acoustic CFL anywhere.
missing     any time-step controller; local time stepping; global time stepping
```

**18. Does any density-based compressible solver already exist?**
**NO.** The only compressible solver is `CompressibleSIMPLE`: pressure-based, steady,
segregated, 2D-only, with density derived from the EOS.

**19. Does Sod shock-tube validation exist?**
**NO.** `Sod` 0, `shockTube` 0, `shock_tube` 0. No case file, no test, no reference.

**20. Does normal-shock validation exist?**
**NO.** `normalShock` 0, `shock` 0, `supersonic` 0, `transonic` 0, `rarefaction` 0,
`choked` 0, `nozzle` 0. `discontinuity` returns 4 hits, all unrelated: harmonic face
interpolation, an alpha-advection comment, and a turbulence test name.

**21. What compressible functionality already exists on CUDA?**
**None.** Exactly one file in the GPU layer mentions "compressible":
`include/cfd/gpu/DevicePressureCorrection.hpp`, and only to record that it ports the
**incompressible** `assemblePressureCorrection`. No compressible EOS, mass-flux,
momentum or pressure-correction kernel exists.

**22. Is there existing CPU/GPU compressible equivalence evidence?**
**NO**, and it is vacuously absent: there is no GPU compressible implementation for a
CPU result to be compared against. Any P17.10 criterion over this would be **vacuous**
until a GPU path exists — which is why `capability_matrix.md` classifies it MISSING
rather than "PASS, nothing to compare".

**23. Which P17 TODO entries are already substantially satisfied?**

```text
P17.1  Variable density        SUBSTANTIALLY SATISFIED for the steady low-Mach 2D
                               regime. Needs verification evidence and a frozen gate,
                               not implementation. Genuine gaps: explicit density
                               bounds, and an invalid-state policy beyond the EOS's
                               own positivity throw.
P17.2  Ideal-gas EOS           DONE and evidenced (results/p17/compressible/eos/).
                               One residual gap: no configured [Tmin,Tmax] validity
                               band, unlike P15's property model.
P17.3  Compressible continuity STEADY form satisfied. Transient form NOT in production.
P17.4  Compressible momentum   Three of four terms satisfied. Viscous stress is
                               INCOMPLETE -- real implementation work, not verification.
P17.6  Low-Mach validation     SUBSTANTIALLY SATISFIED -- 7 cases plus a system MMS
                               with an honest order gate and a determinism check. The
                               one genuine addition is ENERGY conservation, which
                               cannot exist until P17.5 does.
```

**24. Which P17 entries genuinely require new implementation?**

```text
P17.4 (part) the dilatational stress term -(2/3) mu grad(div U)          SMALL, SHARP
P17.5        total-energy equation, in full: e, e_t, E, h_t, conservative
             assembly, EOS coupling, pressure work, conduction, viscous
             work, temperature recovery T(e)                             LARGE
P17.7        HLLC / Riemann flux: conserved state, inviscid flux, L/R
             reconstruction, wave speeds, contact wave, positivity       LARGE
P17.8        density-based solver: conserved update, CFL control (acoustic),
             primitive<->conserved, positivity protection, residuals     LARGE
P17.9        shock validation: Sod, contact, supersonic advection, normal
             shock, expansion; plus an independent analytic Riemann
             reference that contains no CFDApp code                      LARGE
P17.10       CPU/GPU equivalence: vacuous until a GPU compressible path
             exists; outside the authorized scope list                   DEFERRED
```

---

## The real shape of P17 after audit

```text
                        authorization's framing        what the audit found
--------------------------------------------------------------------------------------
P17.1 P17.3 P17.6       "implement"                    VERIFY + gate existing code
P17.2                   "implement"                    DONE (P17.2 closed)
P17.4                   "verify"                        VERIFY + one real physics fix
P17.5 P17.7 P17.8 P17.9 "implement"                    IMPLEMENT -- genuinely absent
P17.10                  "implement"                     VACUOUS today; deferred
```

**Five of twelve sub-items are verification-and-gate work against code that already
exists.** One (P17.4) is mostly verification with a small, sharply defined physics
addition. Four are the real build. One is vacuous.

## Cross-cutting blockers the checklist does not mention

These are not in any P17.x item, and at least the first two must be decided before
P17.7.

```text
B1  2D ONLY. CompressibleSIMPLE.cpp:147 requireTwoDimensional refuses a 3D mesh.
    Sod and a normal shock are naturally 1D and can be run on a 2D mesh, so this does
    not block P17.9 -- but it does mean no 3D compressible capability is reachable,
    and any P17 claim must say "2D".

B2  UPWIND ONLY, first order. P17.7 asks for a "reconstruction interface for higher
    order". No higher-order machinery exists on the compressible path at all, so that
    interface is new work, not an extension.

B3  DUAL cp DEFINITIONS. compressible.specific_heat_pressure (a constant) and
    thermal.specific_heat (optionally a polynomial cp(T)) can both be configured in
    one case, and nothing reconciles them. Not active in any current case; reachable
    by construction. HIGH severity for P17.5 -- an energy-conservation gate would
    measure the wrong thing while appearing to pass. See p15_reuse_map.md section 4.

B4  pseudoTimeStep HAS NO CASE-FORMAT KEY. The compressible solver's only
    stabilization parameter is unreachable from a case file; ProjectRunner pins it at
    1.0. A capability that no case file can reach is not shipped capability.

B5  SEGREGATED vs CONSERVED-STATE ENERGY. A P17.5 energy equation built segregated on
    P14 may not be reusable by P17.8's conserved-state update, risking two
    implementations of the same physics. See p14_reuse_map.md section 5 -- factoring
    the thermodynamic closure out is the option that avoids duplication without
    forcing the coupled-state decision early.

B6  NO ANALYTIC RIEMANN REFERENCE. CLAUDE.md section 6 requires independent reference
    paths, "for example exact-rational Python with no CFDApp code". P17.9 needs an
    exact Sod solution produced outside CFDApp; adopting solver output as the
    reference is explicitly forbidden.
```
