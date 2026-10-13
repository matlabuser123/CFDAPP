# G8 — the reference and its spread, CITED BEFORE THE RUN

G8's acceptance clause requires this document to exist **before** anything is measured:

> *"flame speed within the published experimental/numerical SPREAD for the mechanism, with the
> reference and its spread CITED before the run. Where the literature gives a range, the range is
> the criterion — not its midpoint."*

Nothing in this file is from memory. Every number is quoted from a source that was retrieved,
text-extracted and is named below, so the citation can be checked independently before it becomes an
acceptance criterion.

**Status: the flame-speed criterion is ESTABLISHED. The mechanism parameters are NOT yet citable —
see §3. No measurement may be made until §3 is closed.**

---

## 1. The target: stoichiometric methane–air laminar burning velocity at ambient conditions

### Primary source

> Hermanns, R. T. E. (2007). **"Laminar burning velocities of methane–hydrogen–air mixtures."**
> PhD thesis, Technische Universiteit Eindhoven (Mathematics and Computer Science).
> DOI: **10.6100/IR630126**. Publisher's PDF / Version of Record.

Retrieved from `https://pure.tue.nl/ws/files/2993149/200711972.pdf` and text-extracted locally with
`pdftotext -layout` (6 866 lines). The quotes below are verbatim from that extraction.

### The consensus value and its spread

> *"Note that, nowadays, experimental results reveal a value of **36 ± 1 cm/s** as the laminar
> burning velocity of stoichiometric methane-air flames."*
> — extracted line 991–993 (Chapter 2, "Combustion Reaction Mechanisms")

> *"burning velocities, with the applied new insights, lead to more consensus, which is for
> stoichiometric methane-air flames **≈ 36 cm/s within ±1 cm/s**."*
> — extracted line 1865–1866 (Chapter 4, on methane–air at ambient conditions)

### The same thesis's own independent measurement

Table 4.3, *"Laminar burning velocity measurements with the heat flux burner of stoichiometric
methane-hydrogen-air flames at ambient conditions"*, row `RH2 = 0 %` (i.e. **pure methane**):

| `RH2` (%) | `S_L` brass plate (cm/s) | `S_L` gold-coated plate (cm/s) |
| ---: | --- | --- |
| **0** | **36.4 ± 0.1** | **36.4 ± 0.2** |

— extracted lines 2891–2897. Restated in the Concluding Remarks as *"36.4 ± 0.1 cm/s (at RH2 = 0%)"*
with **95 % confidence error intervals** (extracted line 5504–5506).

Two independent burner-plate materials agree to within their stated uncertainties, and the value sits
inside the consensus band.

### A further primary experimental source, cited by the literature above

> Vagelopoulos, C. N. and Egolfopoulos, F. N. (1998). **"Laminar flame speeds of methane-air at
> 1 atm."** *Twenty-seventh Symposium (International) on Combustion*, p. 513.

Named as reference [7] in Mohseni (2012), BTH-AMT-EX--2012/D-14--SE (extracted line 2145–2147).
Recorded here as corroboration of the conditions; the criterion below rests on Hermanns (2007).

### Conditions

Stoichiometric (equivalence ratio φ = 1.0) methane–air, **ambient conditions**: `298 K`, `1 atm`.
Both appear in the source (`298 K` and `1 atm` located in the extraction).

---

## 2. THE FROZEN G8 CRITERION

```text
QUANTITY   : laminar burning velocity S_L of a freely propagating, adiabatic, 1D premixed
             stoichiometric methane-air flame
CONDITIONS : phi = 1.0, p = 1 atm, unburnt T = 298 K
CRITERION  : 35.0 cm/s  <=  S_L  <=  37.0 cm/s
SOURCE     : Hermanns (2007), TU/e, DOI 10.6100/IR630126 -- "36 +/- 1 cm/s"
```

**The range is the criterion, not its midpoint**, exactly as G8 requires. A measured 36.4 cm/s and a
measured 35.1 cm/s both pass; 34.9 cm/s fails.

### What this criterion does NOT permit

* **No tuning.** The mechanism parameters must be **published and fixed before the measurement**
  (§3). If `A` were adjusted until `S_L` landed in 35–37 cm/s, the criterion would be measuring my
  arithmetic rather than the code, and G8 would be worthless. This is the integrity crux of the whole
  gate.
* **No midpoint target.** 36.0 is not the goal; the band is.
* **No post-hoc widening.** If the measurement lands outside 35–37 cm/s, that is a FAIL to preserve
  and report, not a reason to find a wider citation.

### The two controls G8 names, with their expected outcomes

| Control | Required outcome |
| --- | --- |
| reactions disabled | **no flame propagates** — `S_L` must not be measurable |
| 10× error in the pre-exponential `A` | `S_L` must land **outside** 35.0–37.0 cm/s |

The second is a real constraint on the mechanism choice: a 10× change in `A` must move `S_L` enough
to leave a ±1 cm/s band. For a one-step global reaction `S_L ∝ √A` approximately, so 10× in `A` gives
roughly 3.2× in `S_L` — far outside the band. The control is therefore expected to be decisive rather
than marginal.

---

## 3. THE FROZEN MECHANISM — Westbrook & Dryer (1981), Table II, Set 3

### Source, retrieved and verified

> Westbrook, C. K. and Dryer, F. L. (1981). **"Simplified Reaction Mechanisms for the Oxidation of
> Hydrocarbon Fuels in Flames."** *Combustion Science and Technology*, **27**:1–2, 31–43.
> DOI: **10.1080/00102208108946970**

Downloaded with explicit user authorization from
`http://ronney.usc.edu/AME513b/Lecture4/References/WestbrookDryerCST1981.pdf`
(HTTP only; the host refuses HTTPS). **627 042 bytes, 10 pages,
sha256 `7bedfa37cf924922f7ae2e24612c712e65fa28128cfe9cf0459894c40b935269`.** Text-extracted with
`pdftotext -layout` (848 lines). Every figure below is quoted from that extraction, not from a
secondary source.

### Table II, verbatim

> ```
> TABLE II
> Single-step and detailed reaction rate parameters for
> methane-air, corresponding to curves in Figure 2.
>                Same units as Table I
>                  A          E_a       a        b
> Set 1      Detailed reaction mechanism
> Set 2    1.3 x 10^?        48.4     -0.3     1.3
> Set 3    6.7 x 10^12       48.4      0.2     1.3
> Set 4    1.0 x 10^13       48.4      0.7     0.8
> Set 5    2.4 x 10^1?       48.4      1.0     1.0
> ```

Table I's caption fixes the units for all of these: *"Units are cm-sec-mole-kcal-Kelvins."*

### Why Set 3, in the authors' own words

Set 2 is Table I's best-fit set and carries the **negative fuel order** that is singular as the fuel
burns out. Westbrook & Dryer state the problem and the remedy themselves:

> *"The fuel concentration exponent for CH4 from Table I has a negative value, −0.3. Technically, the
> fuel acts as an inhibitor… From a numerical point of view this can create problems since the rate
> of methane consumption increases without limit as the methane concentration approaches zero. There
> are several possible solutions to this problem. A reverse reaction can be used which provides an
> equilibrium fuel concentration at some small level… The rate expression can also be artificially
> truncated at some predetermined value. However, in some cases it may be preferable to sacrifice
> some of the generality provided by the rate parameters in Table I in order to keep the rate
> expression conveniently bounded."*

And of Set 3 specifically:

> *"parameter Set 3, with a = 0.2, b = 1.3 also shows quite good agreement."*

So Set 3 is the **published, bounded** choice, selected on the authors' own stated grounds. It is
**not** a set I fitted, and no parameter in it is mine.

This also resolves the collision recorded in §4: with `a = +0.2` the rate is bounded as `Y_CH4 → 0`,
so **no concentration floor is needed** and P18's "no clipping, no normalisation" invariant is
preserved untouched. No new numerics, no gate amendment.

### Documented limitation of Set 3, recorded before use

> *"Of the simplified mechanisms, only Set 2 predicts the proper variation of flame speed with
> pressure above one atmosphere."*

Set 3 therefore reproduces flame speed against equivalence ratio but **not** the pressure dependence
above 1 atm. G8's case is fixed at **1 atm**, so this does not affect the criterion — but it bounds
the claim: the qualification will cover `φ = 1.0` at `1 atm` and must not be presented as a validated
pressure response.

### The frozen rate expression, in the paper's units

```text
reaction    CH4 + 2 O2 -> CO2 + 2 H2O          (single-step complete oxidation)
rate        w = A * exp(-Ea/(R*T)) * [CH4]^0.2 * [O2]^1.3
A           6.7e12
Ea          48.4 kcal/mol
a (CH4)     0.2
b (O2)      1.3
units       cm, sec, mole, kcal, Kelvin   (Table I caption)
```

### Conversion to the code's SI units — DERIVED, and checkable

CFDApp works in `m, s, mol, J, K`, so `A` must be converted. `A`'s dimensions follow from the
orders, which is exactly why the units had to come from the primary source:

```text
reaction order sum  a + b = 0.2 + 1.3 = 1.5
[A]_cgs = (mole/cm^3)^(1 - 1.5) / s = (mole/cm^3)^(-0.5) / s

w_SI  [mol/(m^3 s)] = 1e6 * w_cgs        and        [X]_cgs = 1e-6 * [X]_SI

w_SI = 1e6 * A_cgs * (1e-6 [CH4]_SI)^0.2 * (1e-6 [O2]_SI)^1.3
     = 1e6 * A_cgs * 1e(-6 * 1.5) * [CH4]_SI^0.2 * [O2]_SI^1.3
     = A_cgs * 1e(6 - 9) * [CH4]^0.2 [O2]^1.3
     = A_cgs * 1e-3  * [CH4]^0.2 [O2]^1.3

=>  A_SI = 6.7e12 * 1e-3 = 6.7e9      units (mol/m^3)^(-0.5) / s
    Ea   = 48.4 kcal/mol * 4184 J/kcal = 202505.6 J/mol
```

**Independent cross-check of the activation energy.** A secondary paper quoted WD's methane
activation energy as `202.506 kJ/mol`. That equals `48.4 kcal/mol` to the digits given
(48.4 × 4184 = 202 505.6 J/mol), from a source that never mentions kcal. Two independent routes to
the same number, so the extraction and the conversion are both confirmed.

**This conversion is itself a hypothesis until tested.** The first implementation step is a unit
round-trip test asserting that the rate computed in SI equals `1e6 ×` the rate computed in cgs for
the same physical state — a check that fails if the `1e-3` factor or either exponent is wrong. A
wrong `A` by `1e±3` would move the flame speed by roughly `10^±1.5`, far outside 35–37 cm/s, so this
is the one conversion that could silently invalidate G8.

### What is now frozen, and what it forbids

```text
G8 CRITERION  : 35.0 <= S_L <= 37.0 cm/s   (Hermanns 2007, "36 +/- 1 cm/s")
G8 MECHANISM  : WD 1981 Table II Set 3, A_SI = 6.7e9, Ea = 202505.6 J/mol, a = 0.2, b = 1.3
```

Neither may be adjusted after a measurement. If the measured `S_L` falls outside the band, that is a
**FAIL to preserve and report** — not a reason to revisit `A`, to switch to another Set, or to find a
wider citation. Changing the mechanism after seeing a flame speed would make G8 a measurement of my
tuning, which is the one thing it exists to prevent.

---

## 4. The design collision, RESOLVED

The negative-fuel-order singularity recorded when the mechanism was still open is resolved by the
choice of Set 3, on Westbrook & Dryer's own stated grounds (quoted in §3). With `a = +0.2` the rate
is bounded as `Y_CH4 -> 0`, so:

* **no concentration floor is introduced**, and P18's qualified "no clipping, no normalisation"
  invariant stands untouched;
* **no new numerics** are added, so **no gate amendment is needed**;
* the remedy is the one the primary source recommends, not one invented here.

Finding this before implementation is what made it cheap. Discovered afterwards it would have meant
either a silent clip inside a phase that forbids clipping, or a gate amendment driven by an
implementation difficulty -- which section 53 forbids.

---

## 5. Status

```text
G8 criterion  : FROZEN   35.0 <= S_L <= 37.0 cm/s      Hermanns (2007) TU/e, DOI 10.6100/IR630126
G8 mechanism  : FROZEN   WD (1981) Table II Set 3      DOI 10.1080/00102208108946970
                         A_SI = 6.7e9 (mol/m^3)^-0.5/s, Ea = 202505.6 J/mol, a = 0.2, b = 1.3
measurement   : NOT YET MADE -- nothing has been computed against these values
```

Both preconditions of G8 are now satisfied: the reference and its spread are cited, and the
mechanism is published and fixed, **before** any measurement exists. Implementation may begin.
