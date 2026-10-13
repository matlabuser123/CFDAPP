# P18.7 — audit (§1), and a scope finding that must be settled first

```text
MEASURED AT P18.7 OPEN, against HEAD 3bd32f918656f471e72b5f6ac7cb6c2cbf75a274.
Raw counts at logs/00_p18_7_start_state.log.
```

---

## 1. THE FINDING: G8 as frozen cannot be closed in P18.7's authorized scope

G8's `measured` clause, verbatim:

```text
measured     a 1D laminar premixed flame: flame speed, peak temperature, species profiles.
acceptance   flame speed within the published experimental/numerical SPREAD for the mechanism,
                 with the reference and its spread CITED before the run.
```

**What that requires, against what exists:**

| G8 needs | status |
|---|---|
| a coupled reacting-flow solver (transport + chemistry + energy, iterated to steady state) | **DOES NOT EXIST** |
| a stiff or sub-stepped chemistry integrator | **DOES NOT EXIST** — P18.6 states this, and its explicit march is bounded at ~72 steps |
| a flame-speed eigenvalue extraction | **DOES NOT EXIST** |
| mixture-averaged transport properties for a real mechanism | **NOT POPULATED** — P18.2 has the closure; no species transport data exists |
| a real mechanism with published flame-speed data | **NOT PRESENT** |

```text
AND THE REACHABILITY MEASUREMENT IS THE DECISIVE ONE:

    files outside species/ referencing the chemistry layer : 0
    case-file keys defining a mechanism / mixture / h_f     : 0

NOTHING drives the chemistry. P18.2-P18.6 are a five-phase library that no solver, app, case file,
CLI or GUI reaches. A premixed flame is not a measurement that can be taken of this tree; it is a
solver that would have to be BUILT first.
```

**Why building it is not the right action inside P18.7**, by the authorization's own words:

```text
section 1   "Prefer test/evidence instrumentation over production changes. Only modify production
             code if a required physical diagnostic cannot be obtained through the authoritative
             APIs."
section 24  "Do not over-expand scope just to implement a famous benchmark. Use the smallest
             benchmark that genuinely exercises the coupled equations."
section 21  "If instability occurs because P18 lacks a stiff chemistry integrator, document that as
             a limitation."
section 62  lists "no flame-speed validation" among the limitations P18.7 may legitimately record.

AND SECTION 24's PREFERENCE ORDER PUTS THE FLAME LAST:
    1. manufactured reacting advection-diffusion-energy case
    2. 1D steady reacting plug-flow/reference case
    3. simple premixed low-Mach flame with quantitative external reference
```

```text
SO THE AUTHORIZATION AND THE FROZEN GATE DISAGREE, and the disagreement is not resolvable by
working harder: the authorization asks for validation-first with the smallest sufficient benchmark,
and G8 names the largest one.

THIS IS NOT A THRESHOLD TO WEAKEN AND IT IS NOT BEING WEAKENED. G8 stays exactly as frozen.
P18.7 will report G8 as NOT CLOSED, with this audit as the reason, and will close as PARTIAL.
CLAUDE.md section 12 names this case: "the architecture ... needs a bigger case or file-format
change than approved".

WHAT P18.7 DOES INSTEAD is every other thing the authorization asks for -- and those are
substantial and independently valuable. See section 3.
```

## 2. Elemental conservation: NOT MODELED (§9)

```text
files carrying elemental composition : 0

Section 9 anticipates this exactly: "If elemental composition is NOT modeled, record ELEMENTAL
CONSERVATION: NOT MODELED. Do not invent an elemental database solely to check this box." and
"lack of elemental metadata is not itself a P18.7 failure if mass conservation is correctly
enforced and the limitation is documented."

P18.4 deferred it for the same reason and recorded it as debt. P18.7 does NOT invent one.
Section 66's honest form is used in TODO.md:
    [~] Elemental conservation -- N/A: elemental composition metadata is not modeled
```

Consequently §10 (per-reaction elemental balance) and §39 (elemental balance through boundaries)
are **N/A**, and `elemental_conservation.md` records that rather than a number.

## 3. Metric classification (§1)

| metric | status | source |
|---|---|---|
| species mass `M_i = Σ ρY_i V` | **AVAILABLE WITH TEST INSTRUMENTATION** | P18.1 fields + cell volumes |
| `Σ_i Y_i − 1` per cell | **ALREADY AVAILABLE** | P18.1 enforces it; `massFractionSumTolerance` is public |
| `Y_i` bounds | **ALREADY AVAILABLE** | P18.1 validates, without tolerance |
| `max_cell |Σ_i ω_i|` | **ALREADY AVAILABLE** | P18.4 `sourceConservation` |
| reaction progress `q_r` | **ALREADY AVAILABLE** | P18.5 `arrheniusProgressRates` |
| species sources `ω_i` | **ALREADY AVAILABLE** | P18.4 `reactionSpeciesSources` |
| reaction energy source `S_E` | **ALREADY AVAILABLE** | P18.6 `reactionHeatRelease` (two routes) |
| total energy, split into pools | **ALREADY AVAILABLE** | P18.6 `totalEnergy` / `energyDensity` |
| temperature | **ALREADY AVAILABLE** | P18.6 `temperatureFromSensibleEnergy` |
| mixture properties | **ALREADY AVAILABLE** | P18.3 |
| **boundary species flux** | **AVAILABLE WITH TEST INSTRUMENTATION** | from the assembled system: P18.2's `speciesTransportAssembly` + the boundary set |
| **boundary energy flux** | **AVAILABLE WITH TEST INSTRUMENTATION** | P15's energy assembly |
| pressure | **ALREADY AVAILABLE** | P18.3 → P17 `ThermodynamicProperties` |
| time-step history | **AVAILABLE WITH TEST INSTRUMENTATION** | the driver loop owns `dt` |
| **elemental inventory** | **MISSING — NOT MODELED** | §2 above |
| **flame speed** | **MISSING DIAGNOSTIC** | §1 above |

```text
EVERY METRIC P18.7 ACTUALLY NEEDS IS REACHABLE WITHOUT A PRODUCTION CHANGE, with two exceptions
that are recorded as such rather than built around. That satisfies section 1's instruction to
prefer instrumentation.
```

## 4. The enabler: chemistry and transport CAN be driven together

```text
speciesTransportAssembly(mesh, i, registry, fields, massFlux, diffusivities, boundaries,
                         const cfd::transport::ScalarSourceFields& source, timeState,
                         closure, options)

takes the SAME ScalarSourceFields type that P18.4's reactionSpeciesSources FILLS. So a driver can
assemble conservative rho*Y_i transport WITH reaction sources, and feed the matching S_E to P15's
per-cell heat-source assembler, using only authoritative APIs.

THAT IS WHAT MAKES A MANUFACTURED REACTING ADVECTION-DIFFUSION-ENERGY CASE (section 24's FIRST
choice) BUILDABLE AS TEST INSTRUMENTATION. No second reacting-flow implementation is created, and
no production file is modified.
```

## 5. What P18.7 will therefore deliver

```text
CLOSES / STRENGTHENS
  G3  species conservation      per-species integral balance with every term reported separately,
                                closed AND open domains
  G7  reacting energy           integrated evidence beyond P18.6's closed reactor
  G2  species-state validity    sum-to-one and bounds across every P18.7 case, no renormalisation
  G10 determinism               batch reactor and reacting-flow, bitwise
  G5, G6                        re-exercised on the integrated cases

DELIVERS, per the authorization
  batch reactor vs an INDEPENDENT high-accuracy ODE reference (sections 16-21), including a
      dt / dt/2 / dt/4 family and the observed temporal behaviour
  a manufactured reacting advection-diffusion-energy case (sections 24.1, 25) with spatial and
      temporal refinement studies run SEPARATELY (section 29)
  zero-reaction and zero-heat-release limits (sections 14, 15, 34, 35, 36)
  the fourteen negative controls (section 44)

DOES NOT CLOSE
  G8  canonical reacting validation -- section 1 above. REPORTED NOT CLOSED, not weakened.
  elemental conservation -- NOT MODELED, section 2 above.

So P18.7's honest verdict is PARTIAL, and the reason is architectural rather than numerical.
```
