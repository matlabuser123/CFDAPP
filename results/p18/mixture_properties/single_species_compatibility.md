# P18.3 — single-species compatibility (§10, §11) and the `TotalEnergyState` extension point (§17)

```text
G5: "THE SINGLE-SPECIES LIMIT IS BITWISE. With N_species = 1 and Y = 1, sum_i Y_i cp_i is
     1.0 * cp_1, and multiplication by exactly 1.0 is exact -- so there is no round-off to allow
     for, and anything other than bitwise means the mixture path is not a generalisation of the
     existing one."

MEASURED: 0 bitwise mismatches over 5 temperatures x 3 pressures x 5 quantities.
```

---

## 1. Result

| Quantity | Single-species vs P17 | Basis |
|---|---|---|
| `R_mix` | **BITWISE** | `Σ Y_i R_i` at `Y=1` is `1.0 · R_1` |
| `cp_mix` | **BITWISE** | `Σ Y_i cp_i` at `Y=1` is `1.0 · cp_1` |
| `cv_mix` | **BITWISE** | derived by `ThermodynamicProperties` from two bitwise inputs |
| `gamma_mix` | **BITWISE** | same |
| `density` | **BITWISE** | the same `IdealGasEOS`, same `R` |
| `speedOfSound` | **BITWISE** | same |
| `h_mix` | **BITWISE** | `1.0 · h_1(T)`, P15's integral unchanged |
| `M_mix` | **1.238495e-16 relative**, not bitwise | see §3 |

The comparison is against a `ThermodynamicProperties` constructed **directly the way P17 constructs
it**, from `(R_1, cp_1)` — not against a recorded number that could have been transcribed from the
implementation. Checked at every pure endpoint of the **ternary** mixture as well, so the zero terms
are genuinely present in the sums and must contribute exactly nothing (§11).

## 2. Why it is bitwise by construction, not by luck

```text
R_mix = sum_i Y_i R_i   IS THE ONLY FORMULATION THAT ACHIEVES THIS, and the alternatives were
                        measured to fail BEFORE any code was written (audit.md section 3):

    R_u * (1/M) == R_u / M   is FALSE for M = 0.032 kg/mol -- OXYGEN, a species anyone would use
    1 / (1 / x)  == x        is FALSE for 34397 / 200000 = 17.198% of random doubles

Once R_mix and cp_mix are bitwise, EVERYTHING DERIVED FROM THEM IS TOO, because the mixture path
constructs the SAME ThermodynamicProperties object P17 constructs. That is why this architecture was
chosen, not merely why it happens to pass -- a design that computed cv, gamma and density itself
would have had to reproduce three more expressions bit for bit.
```

## 3. `M_mix` is measured, not claimed bitwise — and the reason is stated

```text
M_mix = 1 / sum_i(Y_i / M_i), so the single-species case is 1/(1/M_1) -- a RECIPROCAL PAIR, which is
not bitwise for 17.2% of doubles.

IT IS ALSO NOT REQUIRED TO BE. G5's clause binds "P17's EXISTING single-species value", and P17 has
NO MOLECULAR WEIGHT AT ALL -- 0 occurrences of molecularWeight or molarMass in the repository before
P18.3. There is nothing for M to reduce to bitwise.

So the recovery is measured against the 8-eps closure allowance and reported: 1.238495e-16, i.e.
0.56 eps, worst over the three pure endpoints. Claiming bitwise here would be claiming a property of
IEEE arithmetic that does not hold.
```

## 4. The `TotalEnergyState` refusal — the extension point

G5's note: *"TotalEnergyState currently REFUSES a temperature-dependent cp ... That refusal must be
lifted deliberately as part of this gate, not deleted as an obstacle."*

```text
WHAT THE REFUSAL IS (src/compressible/TotalEnergy.cpp:53, UNCHANGED by P18.3)
  "the enthalpy model's specific heat is temperature-dependent, but ThermodynamicProperties carries
   a single cp from which it derives cv and gamma ... supporting it requires a variable-gamma EOS."

IT IS SOUND. With one stored cp, a cp(T) enthalpy model genuinely cannot be consistent with the EOS
block, and the header records that the alternative -- silently using h = cp(T) T -- is measured by
P15 as 26-103% wrong.

P18.3 DID NOT DELETE IT. The check is byte-for-byte unchanged and all 34 TotalEnergy tests pass
(logs/01_preservation.log).
```

**P18.3 supplies the prerequisite the message names.** `gamma_mix = cp_mix(T,Y) / (cp_mix(T,Y) −
R_mix(Y))` varies with **both** temperature and composition — measured in
`TemperatureDependentCpFlowsThroughTheMixture`, where `gamma(300 K) != gamma(2400 K)` under a
two-term `cp(T)` model. That is a variable-gamma closure.

```text
THE EXACT EXTENSION POINT, which section 17 asks to be DOCUMENTED rather than built:

  TotalEnergyState(ThermodynamicProperties thermodynamics, SensibleEnthalpy enthalpy)
    stores ONE ThermodynamicProperties for the object's lifetime, and its cp-consistency check
    compares enthalpy_.specificHeat(T_ref) against thermo_.specificHeatPressure() ONCE, at
    construction, within kSpecificHeatRelativeTolerance = 1e-12.

  TO ACCEPT cp(T) IT WOULD HAVE TO HOLD A PROPERTY SOURCE RATHER THAN A PROPERTY -- something it can
  ask for R, cp, cv and gamma AT A TEMPERATURE, and for a mixture, at a composition too. That is
  exactly the shape of

      MixtureThermodynamics::properties(composition, T) -> ThermodynamicProperties

  which returns a freshly constructed object per (Y, T). So the extension is: replace the stored
  ThermodynamicProperties with a callable that yields one, and move the consistency check from
  construction to each evaluation.

WHY P18.3 DID NOT DO IT
  * section 17: "Implement only what P18.3 needs for mixture EOS/property correctness" and "Do not
    reopen the P17 shock solver". TotalEnergyState is on P17's density-based path.
  * section 32: protect TotalEnergyState; and P17 G6/G10 remain deferred and must not be touched.
  * The change would widen the ACCEPTED DOMAIN of a type whose recover() feeds the HLLC solver --
    and P17's G6 (HLLC star-density positivity) is still OPEN. Widening the inputs to a path with an
    open positivity defect needs its own frozen gate, not a thermodynamics phase.
  * G5's acceptance clause does not require it. The note is guidance on how to treat the refusal,
    and the guidance -- do not delete it to clear an obstacle -- was followed.

REPORTED for whoever authorizes the energy-side work. TODO.md carries it as debt.
```

## 5. What "preserved" was measured on, not assumed

```text
P18.3 MODIFIES NO EXISTING PRODUCTION SOURCE. It adds two files and one target_sources block.
So preservation is structural -- but that is a claim about code, so it was measured:

  P15 thermal unit suite                      163 / 163
  P15 Enthalpy tests                            8 / 8
  P15 TemperatureProperty tests                 5 / 5
  P17 compressible unit suite                 215 / 215
  P17 ThermodynamicProperties tests            10 / 10
  P17 IdealGas tests                           30 / 30
  P17 TotalEnergy tests                        34 / 34
  P18.1 + P18.2 + P18.3 species suite         165 / 165
  P14 transport · P16 turbulence           22 / 22 · 246 / 246

ONE FILTER MISS RECORDED HONESTLY: the first version of the preservation script filtered
'*TemperatureDependent*' and matched 0 tests -- the suite is actually named TemperatureProperty.
A 0-test filter reported as a pass would have been a vacuous claim, so the filter was corrected and
the real count (5) recorded. logs/01_preservation.log.
```
