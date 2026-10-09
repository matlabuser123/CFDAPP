# P18.4 — audit of the existing source infrastructure (§1)

```text
WRITTEN BEFORE ANY CODE CHANGE.
GATE            results/p18/gates/P18_GATES.md, sha256 re-verified at P18.4 open:
                921616544e3631d40de3ea629a1617e8ec014f89fcb1d7b8a728db7132d618f3  UNCHANGED
P18.4 START     P18_START_SOURCE_FINGERPRINT
                2e0ac729f60a3045d947fdf4211c5d1f809e5ea3057d8d5a540c165328c606ab
EVIDENCE PATH   G6 names results/p18/reactions/; the authorization §51 names
                results/p18/reaction_source/. The authorization is later and more specific, so
                evidence lives there. The frozen gate is NOT edited to match.
```

---

## 1. WHICH GATE P18.4 CAN ACTUALLY CLAIM — read before anything else

The authorization §44 says *"The principal P18.4 gate is G6 — reaction source correctness."* The
frozen gate does not quite say that, and the difference matters:

```text
G6's own `phase` field says:  P18.5
G6's `measured` clause is:    "omega_i for a known mechanism against an independent
                               arbitrary-precision implementation ... over a matrix of T, p and
                               composition"
G6's acceptance is TWO criteria:
   (a) relative error <= 32 * eps on each RATE          <- needs Arrhenius. P18.5's.
   (b) stoichiometric closure: sum_i W_i nu_i = 0 for each reaction,
       to <= 4 eps * max|W_i nu_i|                      <- PURE STOICHIOMETRY. P18.4's.
G6's negative controls: two are about the Arrhenius exponent and activation energy (P18.5's);
   the third -- "a stoichiometric coefficient perturbed must FAIL the closure criterion" -- is
   P18.4's.
```

```text
SO P18.4's HONEST POSITION, fixed here so it cannot drift later:

  CLOSES        G6 criterion (b), the stoichiometric closure, with its OWN derived threshold
                4 * eps * max|W_i nu_i| and its OWN negative control.
  EVIDENCE TOWARD  G6 overall, G3 (species conservation: sum_i omega_i = 0), G1, G2, G10.
  DOES NOT CLOSE   G6 as a whole. Criterion (a) is a rate comparison against an independent
                Arrhenius implementation, and P18.4 implements no kinetics at all.

Claiming "G6 PASS" from a phase with no rate model would be exactly the over-claim the gate's own
phase field guards against.
```

## 2. Requirement classification (§1)

| P18.4 requirement | Class | Basis |
|---|---|---|
| a per-cell explicit species source | **DIRECT REUSE** | P14's `ScalarSourceFields{Su, Sp}`; `b_P += Su_P · V_P` is exactly an explicit volumetric source |
| the `omega_i` hook into transport | **DIRECT REUSE** | P18.2's `SpeciesSources{ vector<ScalarSourceFields> }`, already a parameter of `speciesTransportAssembly` |
| the explicit zero | **DIRECT REUSE** | `zeroSpeciesSources(N, cells)` — P18.2 built it precisely so `omega_i = 0` was verifiably exact |
| species identity | **DIRECT REUSE** | P18.1's `SpeciesRegistry`; reactions refer to it and keep no list of their own (§3) |
| molecular weights `M_i` | **DIRECT REUSE** | P18.3's `SpeciesThermodynamics::molecularWeight()` — **one source of truth** (§35) |
| valid composition | **DIRECT REUSE** | P18.1's `MassFractionState`, unchanged |
| reaction definition, stoichiometry | **MISSING** | **0** `class`/`struct` named Reaction, Stoichiometry, Mechanism or ProgressRate in the repository |
| net `nu_i`, canonicalisation | **MISSING** | nothing |
| molar → mass conversion | **MISSING** | nothing |
| mass-balance validation | **MISSING** | nothing |

```text
THE SEARCH WAS DONE CASE-SENSITIVELY AND THE RESULT CHECKED, because a case-insensitive grep for
"reaction" and "mechanism" returns hits -- all of them the ENGLISH WORDS in comments about
unrelated things (CompressibleMassFlux's "velocity-interpolation mechanism", GPU timing
"mechanism", and P18.2/P18.3's own prose). Case-sensitive class/struct declarations: 0.
```

## 3. What P14 and P18.2 already provide, exactly

```text
cfd::transport::ScalarSourceFields
    ScalarField Su;   // b_P += Su_P * V_P      EXPLICIT   <- omega_i goes here
    ScalarField Sp;   // a_P -= Sp_P * V_P      IMPLICIT   <- stays 0 in P18.4

cfd::species::SpeciesSources { std::vector<ScalarSourceFields> perSpecies; }
cfd::species::zeroSpeciesSources(speciesCount, cellCount)
cfd::species::speciesTransportAssembly(..., const ScalarSourceFields& source, ...)

SO P18.4 ADDS NO ASSEMBLY PATH AT ALL (§1's "do not add a second species-source assembly path").
It produces omega_i values and fills Su. The PDE, the conservative discretisation, the closure and
the boundary treatment are all P18.2's and are untouched.
```

```text
UNITS FALL OUT OF THIS, and they are the reason the molar->mass conversion is not optional:
  P14's Su enters b_P as Su * V_P, and the transported quantity is rho Y_i [kg/m^3].
  The transient term is rho V/dt, so b has units of [kg/s].
  Therefore Su must be [kg/(m^3 s)] -- a MASS source per unit volume.
  A molar rate would be wrong by a factor of M_i, which is 2e-3 to 4.4e-2 here: not a subtle error,
  but also not one the assembler could detect.
```

## 4. Sp stays zero, and that is a deliberate scope boundary

```text
Patankar's form is S = Su + Sp * phi, with Sp <= 0 giving an implicit, stabilising contribution.
A stiff reaction rate genuinely wants one -- P18.0's audit says so explicitly.

P18.4 SETS Sp = 0 and uses Su only. Reasons, recorded rather than left implicit:
  * linearising omega_i in Y_i requires d(omega_i)/d(Y_i), which is a KINETICS derivative. P18.4
    has no kinetics (§33), so it has nothing to differentiate.
  * §23 says explicitly: "Do not build a chemistry integrator yet."
  * an Sp of 0 is the mathematically correct Patankar split for a source treated explicitly; it is
    not an omission but a choice, and it is the one that keeps P18.4 honest about what it knows.
The implicit split belongs to P18.5, which will have the rate and therefore the derivative.
```

## 5. Findings recorded, not acted on

```text
F1  NO ELEMENTAL COMPOSITION EXISTS. P18.3's SpeciesThermodynamics carries a molecular weight and a
    cp(T) model, nothing atomic. §10 anticipates this and says not to invent an elemental
    subsystem. So: ELEMENTAL VALIDATION DEFERRED, MASS CONSERVATION ENFORCED. P18.7 can own the
    broader elemental check.
F2  P18.3's molecular weights are the ONLY ones in the repository. Reactions will consume them
    through MixtureThermodynamics rather than storing their own, so §35's "a mismatch must be
    impossible by design" is satisfied by construction rather than by a validation step.
F3  omega_i is an EXPLICIT source and P18.4 does not integrate it in time. Whether
    rho Y_i + dt * omega_i stays non-negative is a property of a time integrator that does not
    exist yet. §23 asks for diagnostics rather than a clamp, and that is what is provided: the
    rate is validated, the bounded step is deferred and labelled.
```
