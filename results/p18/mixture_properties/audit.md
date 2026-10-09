# P18.3 — audit of the existing thermodynamic infrastructure (§1)

```text
WRITTEN BEFORE ANY CODE CHANGE.
GATE            results/p18/gates/P18_GATES.md, sha256 re-verified at P18.3 open:
                921616544e3631d40de3ea629a1617e8ec014f89fcb1d7b8a728db7132d618f3  UNCHANGED
P18.3 START     P18_START_SOURCE_FINGERPRINT
                f2e27faa51b3092adb40180848a91da644848419994a3e86a7a1482e267b60a4
PRINCIPAL GATE  G5 -- mixture thermodynamic closure
```

---

## 1. Requirement classification (§1)

| P18.3 requirement | Class | Basis |
|---|---|---|
| `cv_mix = cp_mix − R_mix` | **DIRECT REUSE** | `ThermodynamicProperties` already derives it at construction |
| `gamma_mix = cp_mix / cv_mix` | **DIRECT REUSE** | same — derived at construction, never stored independently |
| `rho = p / (R_mix T)`, and `p(rho,T)`, `T(p,rho)` | **DIRECT REUSE** | `IdealGasEOS`, reached through `ThermodynamicProperties` |
| `cp_i(T)` | **DIRECT REUSE** | P15's `TemperatureDependentProperty` — polynomial in T with a validity range and a `constant()` factory |
| `h_i(T)` integrated | **DIRECT REUSE** | P15's `SensibleEnthalpy`: `h = ∫cp dT + h_ref`, with `temperature(h)` by inversion |
| valid composition `Y_i` | **DIRECT REUSE** | P18.1's `MassFractionState` — already enforces finiteness, `[0,1]`, `ΣY=1` |
| `cp_mix = Σ Y_i cp_i(T)` | **MISSING** | no mixture weighting exists anywhere |
| `R_mix` | **MISSING** | `IdealGasEOS` holds ONE `gasConstant_` |
| molecular weight `M_i`, `M_mix` | **MISSING** | **0 occurrences of `molecularWeight` or `molarMass` in the entire repository** |
| universal gas constant `R_u` | **MISSING** | not in `Constants.hpp`, which deliberately holds no tunable physical constants |

**No second EOS and no second enthalpy stack is created** (§1). The mixture layer computes `R_mix`
and `cp_mix` from composition and then **constructs the existing `ThermodynamicProperties`** from
them. Every one of `cv_mix`, `gamma_mix` and the EOS therefore comes from already-qualified P17 code,
which makes §6's "do not allow `cp_mix`, `cv_mix`, `R_mix` to drift" structurally impossible rather
than merely tested.

## 2. What the existing types actually provide

```text
cfd::compressible::ThermodynamicProperties(Real gasConstant, Real specificHeatPressure)
    gasConstant()           R
    specificHeatPressure()  cp
    specificHeatVolume()    cv = cp - R          <-- derived at construction
    specificHeatRatio()     gamma = cp / cv      <-- derived at construction
    density(p, T)           through IdealGasEOS
    speedOfSound(T)
  -> THE CLOSURE G5 ASKS FOR IS ALREADY IMPLEMENTED HERE, for one species. P18.3 supplies the two
     inputs from composition and inherits the rest.

cfd::compressible::IdealGasEOS(Real gasConstant)
    density · pressure · temperature · dDensityDPressure · dDensityDTemperature
  -> p = rho R T in both directions plus derivatives. Nothing about it is single-species except the
     VALUE of R, which is exactly what P18.3 makes composition-dependent.

cfd::thermal::TemperatureDependentProperty(coefficients, Tmin, Tmax, name)
    evaluate(T) · isConstant() · the validity range
  -> cp_i(T) needs no new polynomial logic (§5).

cfd::thermal::SensibleEnthalpy(specificHeat, T_ref, h_ref = 0)
    enthalpy(T)   the INTEGRATED sensible enthalpy, not cp(T)*T
    temperature(h) by inversion
  -> h_i(T) reuses this, preserving P15's reference conventions (§15).

cfd::species::MassFractionState  (P18.1)
    finiteness, 0 <= Y_i <= 1, |sum Y_i - 1| <= 4 eps N, all enforced at construction
  -> §20 is satisfied by TAKING this type. No validation is duplicated (§20's second clause).
```

## 3. THE MEASUREMENT THAT DECIDES THE FORMULATION

G5 requires the single-species limit to be **BITWISE**. Three candidate routes to `R_mix` exist, and
they are not equivalent in floating point. Measured **before** writing any code:

```text
(a)  1 / (1 / x) == x  ?           NOT BITWISE for 34397 / 200000 = 17.198% of random doubles.
(b)  R_u * (1/M) == R_u / M ?      NOT BITWISE for M = 0.032 kg/mol -- which is OXYGEN.
                                   diff 5.684e-14. Also fails nothing for air, CO2, H2, CH4 --
                                   so a test using only those would have passed vacuously.
(c)  sum_i Y_i R_i  at Y_1 = 1     = 1.0 * R_1 = R_1  EXACTLY.
                                   Multiplication by exactly 1.0 is exact, which is the very
                                   property G5's note relies on.
```

```text
CONSEQUENCE, and it is the single most important line in this audit:

    R_mix MUST be computed as  sum_i Y_i R_i,  with R_i = R_u / M_i evaluated ONCE per species.

Computing R_mix as R_u * sum_i(Y_i / M_i) -- the algebraically identical textbook form -- would
NOT reproduce P17's single-species R bitwise for oxygen, and G5's bitwise criterion would fail for
a reason that has nothing to do with the mixture model.

The two forms are still cross-checked against each other (§4), because they must agree to round-off
and a disagreement beyond that would mean one of them is wrong.
```

## 4. Molecular weight: new, and with no P17 counterpart

```text
There is NO molecular weight anywhere in the repository -- 0 occurrences. So:
  * M_i is a new authoritative species datum (§2);
  * R_i = R_u / M_i is DERIVED, not stored redundantly (§2's stated preference);
  * M_mix has NO P17 value to reduce to bitwise, because P17 has no molecular weight at all.
    G5's bitwise clause says "every mixture quantity must reduce to P17's EXISTING single-species
    value" -- so it binds R, cp, cv, gamma and density, which P17 has, and cannot bind M.
    The single-species M recovery is therefore MEASURED AND REPORTED rather than claimed bitwise,
    and the reason is (a) above: the harmonic form carries a reciprocal pair.
```

## 5. The `TotalEnergyState` refusal named in G5's note

```text
WHAT IT SAYS (src/compressible/TotalEnergy.cpp:53)
  "the enthalpy model's specific heat is temperature-dependent, but ThermodynamicProperties carries
   a single cp from which it derives cv and gamma. A variable-cp energy state would be
   thermodynamically inconsistent with the EOS block; supporting it requires a variable-gamma EOS."

THE REFUSAL IS SOUND, not an obstacle: with one stored cp, a cp(T) enthalpy model genuinely cannot be
made consistent with the EOS block, and the header records that the alternative -- silently using
h = cp(T) T -- is measured by P15 as 26-103% wrong.

G5's note: "That refusal must be lifted deliberately as part of this gate, not deleted as an
obstacle."  READ AS WRITTEN: do not delete the check to make something pass; if cp(T) is wanted in
the energy path, build the prerequisite the message names.

P18.3'S POSITION, decided here and revisited with measurements in hand:
  * P18.3 DELIVERS THE PREREQUISITE. gamma_mix = cp_mix(T,Y) / (cp_mix(T,Y) - R_mix(Y)) varies with
    BOTH temperature and composition -- that is a variable-gamma closure.
  * The refusal lives on P17's density-based total-energy path. §17 says "Implement only what P18.3
    needs for mixture EOS/property correctness" and "Do not reopen the P17 shock solver"; §32 says
    protect TotalEnergyState.
  * So P18.3 does NOT delete the check. It records the exact extension point (§17's explicit
    request) and reports the decision, rather than widening a qualified P17 path inside a
    thermodynamics phase.
  * The extension point is named in single_species_compatibility.md.
```

## 6. Findings recorded, not acted on

```text
F1  Constants.hpp deliberately holds no tunable physical constants, so R_u needs a home. It is
    placed with the species thermodynamic data, as a named constant with its CODATA value and
    units, not in Constants.hpp -- which would change the meaning of that header.
F2  ThermodynamicProperties takes (R, cp) and derives the rest. That is the right shape for a
    mixture too, and is why P18.3 adds no parallel closure. Recorded because it is the reason the
    phase is small.
F3  TotalEnergyState's cp-consistency check uses kSpecificHeatRelativeTolerance = 1e-12 between its
    two cp sources. A mixture cp would have to satisfy the same check at whatever T it is evaluated
    at; that is part of the extension point in §5 and is not exercised here.
```
