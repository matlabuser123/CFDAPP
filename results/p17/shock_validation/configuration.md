# P17.9 — the frozen numerical configuration

Authorization §2. Recorded **before** any benchmark was run, and frozen with the acceptance gate.

The rule this file exists to enforce: **one configuration for every benchmark.** No case is tuned
independently to make it pass. Where a case must differ, the difference is listed in §4 below with
its physical or numerical reason, and there are only three such differences in the whole phase.

---

## 1. The solver configuration — identical for every benchmark

```text
solver              cfd::compressible::DensityBasedSolver            (P17.8, UNCHANGED)
reconstruction      first order, piecewise constant                  (P17.7's only mode)
Riemann solver      hllcFluxChecked, Toro PVRS wave speeds           (P17.7, UNCHANGED)
time integrator     single-stage explicit forward Euler              (P17.8, UNCHANGED)
stepping mode       TimeSteppingMode::GlobalPhysical
CFL                 0.4                                              (P17.8's default)
positivity policy   deterministic retry, dt x 0.5, up to 5 times. NOTHING IS CLAMPED.
fallback policy     NONE. There is no fallback flux -- P17.7 proved the HLLC contact denominator
                    is provably non-degenerate, so there is nothing to fall back from.
gas                 R = 287.05 J/(kg K), cp = 1005 J/(kg K)
                    => cv = 717.95, gamma = 1.3998189288947698308
enthalpy reference  SensibleEnthalpy(cp, T_ref = 0, h_ref = 0)       -- the zero reference, so
                    e = cv T exactly and rhoE = p/(gamma-1) + rho|U|^2/2
convergence         not applicable: every shock case is a TRANSIENT run to a fixed end time and
                    terminates with status TimeComplete. Steady criteria are not evaluated in
                    GlobalPhysical mode at all (P17.8 criterion M6).
```

```text
GAMMA IS 1.3998189..., NOT 1.4, AND THAT IS NOT A DETAIL.
The textbook Sod tables are computed at exactly 1.4. Quoting them here would introduce a ~1e-4
discrepancy that has nothing to do with the solver and would sit right on top of the errors being
measured. Every reference number in P17.9 is recomputed at the PROJECT's gamma by
tools/riemann_reference.py. The gas is the same one P17.7's exact_riemann.py and every P17.8 test
suite use, so the numbers are directly comparable across phases.
```

## 2. The Sod shock tube — the primary case

```text
initial state, exactly as the frozen G7 criterion names it:
    x < x0 :   rho = 1.0     u = 0     p = 1.0
    x > x0 :   rho = 0.125   u = 0     p = 0.1

domain              [0, 1] x [0, Ly],  Ly = 1/nx  (square cells)
x0                  0.5
mesh                createCartesian2D(nx, 1, 1.0, 1.0/nx)     -- 2D mesh, 1D problem
cell counts         nx = 200 (nominal), 400, 800               -- the refinement family
dx                  5.000e-03, 2.500e-03, 1.250e-03
final time          t = 0.2
boundaries          InviscidWall on all four patches
```

```text
WHY WALLS, AND WHY THEY ARE EXACT HERE. At t = 0.2 the leftmost wave (the rarefaction head) has
reached x = 0.2634 and the rightmost (the shock) x = 0.8504, so the regions adjacent to both x-
boundaries are still in their UNDISTURBED initial state, where u = 0. An inviscid wall mirrors the
normal velocity; mirroring zero gives back the same state, so the boundary is not merely far away,
it is EXACTLY consistent with the initial condition for the whole run. No boundary treatment error
enters the measurement. Authorization §30 is satisfied by construction rather than by hoping.

margin to the left boundary  : 0.2634
margin to the right boundary : 0.1496
```

```text
WHY THE LITERATURE SCALING (p = 1) RATHER THAN AN ATMOSPHERIC ONE. Those states give
T_left = 3.4837e-03 K and T_right = 2.7870e-03 K, which look alarming until one checks the two
things that could break:
  * the admissibility floor is e(T_min) with T_min = 0, so e = cv T > 0 is admissible;
  * T(e) is a CLOSED FORM in this project, T = (e - h_ref + cp T_ref)/cv, not a bisection with a
    tolerance in kelvin. Its accuracy is therefore scale-free -- exact to double round-off at any
    temperature. (P17.5 replaced the bisection for the calorically perfect gas precisely here.)
So the literal G7 states are runnable, and running the criterion exactly as frozen beats running a
rescaled substitute and arguing it is equivalent.
```

## 3. The other benchmarks

```text
case                    mesh / domain                      end time   boundaries
-----------------------------------------------------------------------------------------------
stationary contact      nx=200, [0,1] x [0,1/200]          0.2        InviscidWall
  p_L = p_R = 1.0, u_L = u_R = 0, rho_L = 1.0, rho_R = 0.125
moving contact          nx=200, [0,1] x [0,1/200]          0.2        SupersonicInflow / Outflow
  p_L = p_R = 1.0, u_L = u_R = 2.0 (supersonic), rho_L = 1.0, rho_R = 0.125
supersonic advection    nx=200, [0,1] x [0,1/200]          3.0e-4     SupersonicInflow / Outflow
  the P17.8 C4 entropy pulse, Ma ~ 2, reused so the two phases' numbers compare directly
normal shock            nx=400, [0,1] x [0,1/400]          see below  SupersonicInflow / Outflow
  M1 = 2.0 upstream, downstream from the analytical relations; the shock is initialised at its
  steady position and must STAY there
expansion wave          nx=200, [0,1] x [0,1/200]          0.2        InviscidWall
  the Sod LEFT rarefaction, measured in its own right against the self-similar fan solution
compression wave        nx=200, [0,1] x [0,1/200]          0.12       InviscidWall
  two symmetric streams converging: u_L = +0.4, u_R = -0.4, rho = 1, p = 1 both sides
```

## 4. The only per-case differences, each with its reason

```text
1. END TIME differs per case. It is not a tuning knob: each is chosen so the waves stay inside the
   domain (§30) and so the feature being measured has travelled far enough to be measurable. The
   choices are recorded above and are not revisited after seeing results.

2. BOUNDARIES differ between the wall cases and the supersonic through-flow cases. A subsonic open
   boundary is NOT IMPLEMENTED in P17.8, so every case is built to need only walls (with the wave
   never reaching them) or genuinely supersonic inflow/outflow. This is a scope limit being
   respected, not a convenience.

3. MESH SIZE is nx = 400 for the normal shock rather than 200, because the measurement there is a
   STATIONARY structure and the question is whether it stays put; a finer mesh makes the
   position resolution tighter. Declared here in advance.

NOTHING ELSE VARIES. Same CFL, same reconstruction, same flux, same integrator, same gas, same
positivity policy, for every case in the phase.
```

## 5. CFL sensitivity (§21)

Sod is run at **CFL 0.4 (nominal)** and **CFL 0.2**. Both are stable for an explicit first-order
scheme; the pair exists to show the result is not an artefact of one narrowly chosen step. The
comparison is made on shock position, contact position, field errors and conservation.

```text
CFL IS NOT TUNED AFTER SEEING THE ANSWER. Both values are declared here, before any run, and both
are reported whatever they show.
```

## 6. What is deliberately NOT varied

```text
higher-order reconstruction   NOT USED. Authorization §34 is explicit: the first-order baseline
                              must qualify first, and a high-order scheme must not be used to mask
                              a first-order failure. P17.7 provides no other mode in any case.
limiters                      none exist to enable
artificial viscosity          none. The only dissipation is the scheme's own.
entropy fix                   none. If one were needed the rarefaction would show it, and that
                              would be a finding rather than something to patch around.
```
