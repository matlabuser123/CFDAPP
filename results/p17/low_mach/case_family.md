# P17.6 — the Mach-number case family and its reference mapping

Consolidates what the authorization suggested as `case_family.md` and `reference_mapping.md`,
following the convention P17.1–P17.5 established.

---

## 1. The case

The channel of the existing low-Mach test, reused rather than replaced:

```text
geometry        8.0 x 1.0, so L/H = 8
mesh            32 x 6, the existing test's own default
viscosity       mu = 0.1
gas             R = 287.05, cp = 1005, gamma = 1.399818929
state           T = 300 K, ISOTHERMAL (temperature is an input to CompressibleSIMPLE)
                p_ref = 101325 Pa
sound speed     a = sqrt(gamma R T) = 347.196 m/s, from P17.2's ThermodynamicProperties
boundaries      left Inlet(U0, 0)      right Outlet + FixedValue(p = 0)
                bottom Wall            top Wall
                pressure: FixedGradient(0) on left/bottom/top, FixedValue(0) on right
```

## 2. The family, and how the levels were chosen

```text
level   U0       achieved inlet Ma   machMax      Re = rho U H / mu
1       1.0      0.0028802           0.0041552     11.8
2       0.5      0.0014401           0.0020790      5.9
3       0.25     0.00072005          0.0010397      2.9
4       0.125    0.00036003          0.00051994     1.5
                 span 8x
```

The **achieved** Mach is reported, not the requested one — §3 of the authorization asks for exactly
that, and `machMax` is measured from the solved field rather than from the inlet value.

Only `U0` varies. Geometry, mesh, viscosity, gas, temperature, reference pressure, boundary-condition
*meaning*, numerical schemes and the convergence policy are identical across levels.

### Why the family descends from U0 = 1, and why it stops at four levels

Both ends are set by the **inherited `maxIterations = 3000`**, measured on the unchanged tree in
`logs/01_dryrun.log`:

```text
U0 = 16, 8, 4, 2   the INCOMPRESSIBLE reference hits maxIterations and never converges, so the
                   compressible solve is never even reached. The family cannot go UP.
U0 = 1 -> 0.125    iterations climb 1959 -> 2261 -> 2403 -> 2439 (compressible)
                                     2031 -> 2450 -> 2626 -> 2680 (incompressible)
                   i.e. toward the same 3000 limit. The family cannot go much further DOWN.
```

§22 forbids raising an iteration limit to obtain a pass, so the family is what the inherited policy
permits: four levels, 8× in Mach, anchored at the speed the existing test already uses. The iteration
growth is recorded as a **conditioning characteristic** under §11/§21, not worked around.

## 3. The incompressible reference mapping

For **every** compressible level, a matched incompressible SIMPLE solve on the same mesh, viscosity
and boundary conditions:

```text
quantity              compressible                      incompressible reference
-------------------------------------------------------------------------------------------
solver                CompressibleSIMPLE                SIMPLE
density               solved, rho = EOS(p_abs, T)        FIXED at rho_ref
rho_ref               --                                 DERIVED: p_ref/(R T) = 1.17662428
viscosity             0.1                                0.1
geometry / mesh       identical                          identical
boundary conditions   identical objects                  identical objects
pressure gauge        Outlet + FixedValue(0) at "right"  the same
initial velocity      uniform (U0, 0)                    uniform (U0, 0)
initial pressure      0                                  0
```

### `rho_ref` is DERIVED, and that is a deliberate change from the existing test

The existing test gives the incompressible solver `kDensity = 1.0` while the EOS at the reference
state gives `1.176624` — a **15.01 % mismatch**. That test **compensates** when comparing fluxes (it
scales the incompressible flux by `rhoAvg`), so its own numbers are sound; I checked that
specifically rather than assuming the threshold was absorbing the mismatch, and it is not.

But a **solution** comparison is different: if the two formulations describe gases of different
density, the velocity fields differ for that reason alone, and a 15 % offset would swamp a trend whose
entire range is ~1e-4. So here `rho_ref = p_ref/(R T)`, derived. Control **N2** plants `rho_ref = 1.0`
to show the comparison genuinely depends on this.

### The convergence policy is held at constant RELATIVE precision

The inherited tolerances are **absolute** and were locked for `U_mean = 1`:

```text
velocityTolerance 2e-5   pressureTolerance 5e-4   continuityTolerance 1e-6
maxIterations 3000   velocityRelaxation 0.7   pressureRelaxation 0.3
momentumSolver 500 / 1e-10 / 1e-8      pressureSolver 2000 / 1e-8 / 1e-6
```

Across a family spanning 8× in velocity an absolute tolerance is **not the same convergence policy** —
it silently demands 8× more relative precision at the top. Every gated quantity scales like the flow
(velocity ~ U, pressure ~ 12 μUL/H² ~ U, continuity ~ ṁ ~ U), so the three field tolerances are
multiplied by `U0`.

```text
At U0 = 1 this reproduces the inherited values BIT FOR BIT. It is a policy correction so that the
levels are comparable, not a relaxation -- and the dry-run measured the alternative: with absolute
tolerances, four of five levels could not converge at all.
```

The relaxation factors, iteration limits and linear-solver settings are **untouched**.

## 4. What the expected limiting behaviour is, per quantity

Stated in advance, because §8 requires the expected behaviour to be declared rather than discovered:

```text
density      rho -> rho_ref. Delta rho/rho = Delta p/p, and for THIS channel the pressure drop is
             VISCOUS-dominated, dp ~ 12 mu U L / H^2, not dynamic (rho U^2). So the expected trend is
             Delta rho/rho ~ Ma^1, NOT Ma^2. A quadratic expectation would be wrong here.
velocity     -> the incompressible solution, difference ~ O(Delta rho/rho) ~ Ma
pressure     -> the incompressible field once referenced to a common cell. The RELATIVE measure has
             a convergence floor at pressureTolerance/dp = 5.2083e-5, constant across the family
             because both scale with U0, so it CANNOT keep falling; the absolute measure can.
temperature  EXACTLY constant at 300 K, by construction -- CompressibleSIMPLE takes T as an input
             and this case is isothermal. There is no temperature error to trend, and the meaningful
             check is that temperature RECOVERY from rhoE/rho - ke returns the imposed value.
mass flux    -> rho_ref (U.n) A as the density variation vanishes
residuals    stable convergence, no oscillation or stagnation; iteration counts are NOT required to
             match between formulations, and their growth as Ma falls is a conditioning result
energy       ke/e ~ Ma^2, six to eight orders down. The KINETIC contribution must be bounded
             separately, relative to its own magnitude -- the frozen G5 clause's own requirement.
```

## 5. The second topology that was considered and not used

§15 suggests a second case with a different flow topology if one already exists. The cavity
(`test_cavity_ghia.cpp`) is the obvious candidate and is **not** used as a Mach family, for a stated
reason rather than by omission:

```text
The lid-driven cavity is a CLOSED domain -- no inlet, no outlet, no through-flow. Its pressure level
is set only by the reference cell, so there is no pressure DROP to drive a density variation, and
Delta rho/rho is then a round-off-scale quantity with no Mach trend to measure. It is an excellent
incompressible reference (and is run, unchanged, as a preservation case) but it cannot carry a
low-Mach LIMIT study.
```

The channel is used because the low-Mach limit needs a through-flow with a pressure drop. That is
also why §15's own list names a pressure-driven channel and Poiseuille flow first.
