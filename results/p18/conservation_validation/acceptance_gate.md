# P18.7 — acceptance gate (§2)

```text
FROZEN BEFORE ANY MEASUREMENT. Hashed at logs/01_freeze.log.

EVERY BOUND IS INHERITED FROM AN ALREADY-FROZEN GATE OR DERIVED FROM A DIMENSIONAL FLOOR.
Not one is fitted to an observation. Where a quantity has no derivable absolute bound, the
criterion is CONVERGENCE UNDER REFINEMENT -- the error must fall -- which is the same reasoning
G8 itself uses for profiles and P17.9's A2 used for the rarefaction head.

G8 IS NOT PART OF THIS GATE. audit.md section 1 records why it cannot be closed in P18.7's scope
and it is reported NOT CLOSED, unweakened.
```

---

## 1. Criteria

### C1 — species mass conservation, closed domain (G3)

```text
case          closed batch reactor, and the closed-domain reacting-flow variant
measured      per species i:  M_i(t2) - M_i(t1) - INT omega_i V dt        [kg]
              with M_i = sum_cells rho Y_i V, and EVERY TERM REPORTED SEPARATELY (section 3)
reference     the integral balance itself; no external reference needed for a closed domain
normalization |imbalance| / |total species mass|
bound         <= 8 * eps * N_cells          INHERITED VERBATIM from G3
control       N1 (break one species source sign), N2 (omit one species source),
              N13 (wrong dt in the accumulation)
```

### C2 — species mass conservation, open domain (G3)

```text
case          steady 1D reacting plug flow with inflow and outflow
measured      per species:  in - out + INT omega_i V        [kg/s]
              convective and diffusive boundary contributions REPORTED SEPARATELY (section 32)
normalization |in - out + source| / |in|
bound         <= 8 * eps * N_cells          INHERITED VERBATIM from G3
control       N11 (omit the boundary species flux from the balance)
```

### C3 — the mixture sum (G3)

```text
measured      sum over species of the C1/C2 balances
bound         the mixture imbalance <= the WORST single-species imbalance
              INHERITED VERBATIM from G3: "it cannot be better than its parts, and must not be
              worse"
control       N5 (break reaction mass conservation) -- must fail the sum while per-species pass
```

### C4 — sum-to-one closure (G2)

```text
measured      max_cell |sum_i Y_i - 1|, at: initial, after the first reacting update,
              intermediate, final -- for every case (section 7)
bound         <= massFractionSumTolerance(N_species) = 4 * eps * N_species
              INHERITED VERBATIM from P18.1, which enforces it
disclosure    NO RENORMALIZATION EXISTS ANYWHERE IN THE PRODUCTION PATH. normalizeMassFractions is
              a separate named free function that nothing calls implicitly; P18.6's reactor refuses
              rather than renormalises. Asserted by an integrity grep with a positive control.
control       N4 (break the sum assertion)
```

### C5 — mass-fraction bounds (G2)

```text
measured      min and max of Y_i over every species, cell and case
bound         0 <= Y_i <= 1, BOOLEAN, no tolerance   INHERITED from P18.1
              A violating state is REFUSED by P18.1, never clipped (section 8).
control       N1, N2 -- a broken source drives Y negative and must be refused
```

### C6 — local reaction mass conservation (G3, G6)

```text
measured      max_cell |sum_i omega_i|, and the scale max_i |omega_i| it is judged against
bound         <= 32 * eps * max_i |omega_i|      INHERITED from P18.4/P18.5's measured criterion
              LOCAL, not global: section 5 forbids letting global cancellation hide a local error.
control       N9 (perturb a stoichiometric coefficient), N5
```

### C7 — energy conservation (G7)

```text
case          closed adiabatic batch reactor; and the open plug flow
measured      E = sum_cells V [ rho cp(T,Y) T + rho sum_i Y_i h_f,i ]   P18.6's frozen definition
              reported as E_initial, E_final, absolute drift, relative drift, AND per-step
              (section 12): max step imbalance, RMS step imbalance
normalization |drift| / |E_initial|
bound         <= 8 * eps * N_cells          INHERITED VERBATIM from G7
control       N6 (energy-source sign), N7 (omit the reaction heat source), N12 (omit the boundary
              energy flux)
```

### C8 — zero-reaction limit (G1, §14)

```text
case          every P18.7 case, with q_r = 0
measured      Y_i, T, rho, energy, and the species/energy sources
bound         BITWISE EQUALITY to the non-reacting run. Not "small" -- section 14 says to use
              bitwise where existing compatibility gates expect it, and P18.6's section 34 already
              established the bitwise requirement for this limit.
control       N14 (disable chemistry while claiming a reacting benchmark) must be DETECTED by the
              non-vacuity guard, i.e. the reacting case must be shown to differ from this one
```

### C9 — zero-heat-release limit (§15)

```text
case          thermoneutral reaction, Delta H_r^0 = 0, q_r != 0
measured      S_E, the species change, and T
bound         S_E EXACTLY 0.  Species MUST change.  For T the expectation is DERIVED BEFORE
              MEASUREMENT, not assumed constant:
                  equal cp_i   -> cp_mix is composition-independent and rho cp T is the conserved
                                  sensible quantity, so T is BITWISE unchanged
                  unequal cp_i -> cp_mix changes, so T MOVES, to T_0 * cp_0/cp_1
              Requiring constant T in the second case would be requiring the wrong thing.
control       N7
```

### C10 — batch reactor vs an INDEPENDENT reference (§16-§21)

```text
case          closed adiabatic batch reactor, single irreversible first-order reaction
reference     tools/batch_reference.py -- mpmath QUADRATURE at 50 digits. NOT a time integration,
              and it calls NO CFDApp code (section 19, section 46).
measured      Y_i(t), T(t), q(t), total species mass, sum Y_i, total energy, at checkpoints
              (section 60) -- not only the final state
normalization relative error against the reference
bound         THE ERROR MUST FALL UNDER dt REFINEMENT across dt, dt/2, dt/4 -- and the observed
              order is REPORTED, not gated. The production update is EXPLICIT EULER, so first-order
              behaviour is what the design predicts; section 20 forbids forcing second order.
              NO INVENTED ABSOLUTE BOUND, for the reason G8 itself gives for profiles.
control       N8 (stale temperature in kinetics), N10 (Arrhenius exponent sign), N13
```

### C11 — canonical reacting flow vs an independent reference (§22-§26)

```text
case          STEADY 1D ADIABATIC REACTING PLUG FLOW -- section 24's choice 2.
              Advection + chemistry + thermal coupling simultaneously, subsonic/low-Mach, well
              inside P17's qualified envelope, and NOT a flame, shock or detonation (section 46 of
              P18.6 and section 22 here).
reference     THE SAME mpmath quadrature. For steady adiabatic plug flow the governing system is
              the batch reactor with t -> x/U, which is a derivation recorded in
              reacting_flow.md, not a coincidence -- so one independent reference serves both and
              neither is validated against CFDApp.
measured      Y_i(x), T(x), outlet composition, integrated conversion, peak reaction location
bound         ERROR MUST FALL UNDER GRID REFINEMENT across N, 2N, 4N; observed order REPORTED.
              Convection is first-order upwind here, so first order is the design prediction.
control       N14, N3 (remove the diffusion closure), N11
```

### C12 — determinism (G10, §41-§43)

```text
measured      Y_i, T, energy, reaction-rate and conservation histories over repeats
bound         BITWISE, EXACT EQUALITY. Inherited from G10 and from P18.5/P18.6's own measurements.
control       any non-determinism is itself the failure
```

## 2. Refinement protocol (§29, §30)

```text
SPATIAL AND TEMPORAL STUDIES ARE RUN SEPARATELY, and the held-fixed quantity is stated:
    spatial refinement  -- steady plug flow; there IS no time error, so none has to be bounded
    temporal refinement -- batch reactor on a single cell; there IS no spatial error
THIS IS WHY THESE TWO CASES WERE CHOSEN: each isolates one error source EXACTLY rather than
approximately, so section 29's requirement is met by construction rather than by argument.
A combined refined case (section 30) is then run to confirm no unexpected interaction.
```

## 3. The fourteen negative controls (§44)

| # | defect | criterion it must break |
|---|---|---|
| N1 | break one species source sign | C1, C5 |
| N2 | omit one species reaction source | C1, C3 |
| N3 | remove the `ΣJ_i` diffusion closure | C11, C4 |
| N4 | break the `ΣY_i` assertion | C4 |
| N5 | break reaction mass conservation | C3, C6 |
| N6 | break the energy-source sign | C7 |
| N7 | omit the reaction heat source | C7, C9 |
| N8 | stale temperature in kinetics | C10 |
| N9 | perturb a stoichiometric coefficient | C6 |
| N10 | flip the Arrhenius exponent sign | C10 |
| N11 | omit the boundary species flux from the balance | C2 |
| N12 | omit the boundary energy flux from the balance | C7 |
| N13 | wrong `dt` in the transient accumulation | C1, C10 |
| N14 | disable chemistry while claiming a reacting benchmark | C8's non-vacuity, C11 |

```text
EVERY CONTROL IS AN ASSERTION, not a printed value (section 44). Each is a permanent test.
```

## 4. What this gate does NOT cover

```text
G8   canonical reacting validation -- a 1D laminar premixed flame with flame speed against a
     published spread. NOT CLOSABLE in P18.7's scope; audit.md section 1. NOT weakened, NOT
     restated as something easier, and NOT claimed.
ELEMENTAL CONSERVATION -- NOT MODELED; audit.md section 2. Sections 10 and 39 are N/A.
G9, G11 -- section 47 forbids claiming them.
STIFF CHEMISTRY -- P18.6 has no stiff integrator. The batch reactor is kept inside the explicit
     method's legitimate stability envelope and the limit is REPORTED (section 21), not hidden
     behind an undocumented tiny time step.
```
