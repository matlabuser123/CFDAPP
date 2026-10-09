# P18.2 — audit of the P18.1 / P14 integration point (§1)

```text
WRITTEN BEFORE ANY CODE CHANGE.
AUTHORITATIVE INPUTS  results/p18/audit/*.md · results/p18/multi_species_state/* · results/p14/
GATE                  results/p18/gates/P18_GATES.md, sha256 re-verified at P18.2 start:
                      921616544e3631d40de3ea629a1617e8ec014f89fcb1d7b8a728db7132d618f3  UNCHANGED
P18.2 START STATE     P18_START_SOURCE_FINGERPRINT
                      aa6c1c770a14d02921121133bbeedb9e745fc64983f20aab007ef2bb8fc155a3
                      (differs from P18.1's start, correctly: P18.1 added code. P18.1's two
                      production sources hash exactly as its summary recorded them.)
EVIDENCE PATH NOTE    the frozen gate names `results/p18/transport/` as G4's evidence root; the
                      P18.2 authorization §44 names `results/p18/species_transport/`. The
                      authorization is the later and more specific instruction, so evidence lives
                      at species_transport/. The frozen gate is NOT edited to match.
```

---

## 1. Requirement classification (§1)

| P18.2 requirement | Class | Basis |
|---|---|---|
| `∂(ρY)/∂t` transient | **DIRECT REUSE** | `TransportTimeState` already carries `rhoOld`/`rhoNew` and documents `a_P = ρ^{n+1}V/dt`, `b_P = ρ^n φ^n V/dt` — the conservative form, already correct |
| ρ-weighted convection | **DIRECT REUSE** | `assembleScalarConvectionContribution` on the authoritative `massFlux`, which already carries ρ. Both stacks already consume `physics::calculateMassFlux` |
| Species-dependent `D_i` | **DIRECT REUSE** | `assembleScalarDiffusionContribution` takes `diffusivity` as a **ScalarField** with `faceDiffusivity` interpolation. One field per species gives each its own `D_i` at no new cost |
| Source `ω_i` (= 0 here) | **DIRECT REUSE** | `ScalarSourceFields{Su, Sp}` + `applyImplicitScalarSource`. P18.2 passes an explicit zero |
| Internal-face conservation | **DIRECT REUSE** | P14 computes each face once and applies owner/neighbour from the same `terms` |
| Exact gradient boundary | **REUSE WITH EXTENSION** | **the real gap.** P14 carries the same lag as species (`ScalarTransport.cpp:129`). Must be applied deliberately — G4's own note says it cannot be inherited |
| Multi-species diffusive closure `ΣJ_i = 0` | **MISSING** | cross-species by nature; no per-scalar assembler can express it. The one genuinely new numerical object in P18.2 |
| `ΣY_i = 1` through transport | **MISSING** | emerges from the closure above; nothing in either stack provides it |
| Bounds after update | **MISSING in the assemblers, PRESENT in P18.1** | `MassFractionState` already enforces finiteness, `[0,1]` and the sum; P18.2 reuses it as the acceptance check |

**No `SpeciesConvectionAssembler` or `SpeciesDiffusionAssembler` is created** (§1). P18.2 calls
P14's assemblers directly. The only new assembly-level code is the cross-species flux correction,
which is not a duplicate of anything because nothing per-scalar can express it.

## 2. What P14 already gets right, verified by reading it

```text
TRANSIENT       ScalarTransport.hpp states the conservative discretisation explicitly and warns
                against precisely the error §34 asks for a control against:
                  "Using rho^{n+1} phi^n on the right-hand side is NOT conservative when the
                   density changes, and is the single easiest mistake to make here."
                rhoOld and rhoNew are separate, separately-named pointers so writing the wrong
                form requires actively naming the wrong one.
CONVENTION      a_P phi_P + sum_N a_N phi_N = b_P, stated once in the header for every assembler.
CONVECTION      upwind on the owner-oriented face mass flux F_f, which ALREADY CARRIES rho.
                The transported quantity enters with coefficient 1.
DIFFUSION       Gamma_f |Sf| / d, Gamma face-interpolated (Arithmetic default = the existing
                production convention, so a uniform field reproduces constant-D exactly).
SOURCE          Patankar: a_P -= Sp_P V_P ; b_P += Su_P V_P.
FACE ASSEMBLY   one `terms` per face, applied to owner and neighbour from the same value.
```

**P14 is now the ONLY scalar transport assembly in the codebase.** Its header records that all six
production turbulence equations (k-ε k and ε, k-ω k and ω, SST k and ω) route through it via
`cfd::turbulence::solveRelaxedScalarTransport`. That is the blast radius of any change to it, and
it drives the decision in §4.

## 3. The lag, located exactly

```text
src/transport/ScalarTransport.cpp, the boundary branch of the diffusion assembler:

    const auto terms = boundaryFaceDiffusionTerms(mesh, face, gammaFace, distance, gradPhiPtr,
                                                  prescribesBoundaryValue(bc.type()));
    const Real phiB = scalarBc->boundaryValue(phi[ownerId], distance);   // <-- LAGGED
    rhs[ownerId] += terms.explicitFlux;
    builder.add(ownerId, ownerId, terms.coefficient);                    // <-- non-zero diagonal
    rhs[ownerId] += terms.boundaryValueCoefficient * phiB;               // <-- lagged value on RHS

src/thermal/EnergyEquation.cpp, the same decision made correctly:

    if (prescribesBoundaryValue(bc.type())) {
      return {coefficient, bvc.value_or(coefficient) * boundaryValue(temperature[owner], d)};
    }
    // T_b = T_P + g d: the flow Df (T_P - T_b) = -Df g d is known exactly
    // (boundaryValue(0, d) = g d); nothing depends on T_P.
    return {0.0, coefficient * boundaryValue(0.0, d)};
```

**Why the exact form is right, derived rather than copied.** For `dφ/dn = g` the boundary value is
`φ_b = φ_P + g d`, so the diffusive flux into the owner is

```text
    Df (phi_b - phi_P) = Df (g d) = Gamma |S| g          with Df = Gamma |S| / d
```

which contains **no `φ_P`**. The assembled row holds `-flux_into_owner`, so the correct
contribution is a **zero diagonal** and `rhs += Df · g d`, and `boundaryValue(0.0, d)` returns
exactly `g d`. The lagged form instead adds `+Df` to the diagonal and `-Df φ_P^{old}` to the RHS;
these cancel only at convergence, which is why the answer is right but the cost is 335 outer
iterations against thermal's 2. It is a fixed-point iteration for a quantity that has a closed
form.

## 4. THE ARCHITECTURAL DECISION: the fix is opt-in, and that is P14's own convention

Fixing the lag in place would change every existing P14 caller — all six turbulence equations of a
**qualified** phase — during a phase authorized for species. Three inputs decide this:

```text
1. THE AUTHORIZATION, §17:  "Do not fix unrelated boundary infrastructure."
2. THE FROZEN ORACLE:  tests/unit/turbulence/test_p14_migration_equivalence.cpp freezes matrix,
   RHS and solved-field values for a case with FixedGradient(0.0) on two patches. An in-place fix
   changes them. (tests/unit/transport/test_transport_characterization.cpp has ZERO FixedGradient
   coverage -- a real gap in the oracle, recorded in §6.)
3. P14'S OWN ESTABLISHED PATTERN, twice in its own header:
     FaceDiffusivityInterpolation -- "Selecting it is deliberate and never automatic: the default
       keeps every existing result bit-for-bit."
     requireNonPositiveSp -- "defaults to OFF, because turning it on by default would change the
       behaviour of the existing turbulence call sites, which is not P14's business."
```

```text
DECISION   add an explicit, named parameter to assembleScalarDiffusionContribution:

               enum class GradientBoundaryTreatment { Lagged, Exact };

           defaulting to Lagged, so EVERY EXISTING CALLER IS BITWISE UNCHANGED, and the species
           path passes Exact.

WHY NOT an in-place fix: it changes a qualified phase's numbers inside a species phase, against
           §17, and invalidates a frozen oracle whose replacement would have to be re-derived --
           work that belongs to whoever authorizes the turbulence side.
WHY NOT a species-only duplicate: that is the second diffusion assembler §1 forbids.
WHY THIS IS NOT "a conditional bolted onto shared code" (CLAUDE.md §6): it is the third instance
           of a pattern P14 introduced and documented for exactly this situation -- a named,
           defaulted selector whose default preserves existing behaviour bit-for-bit.

CONSEQUENCE, recorded as debt rather than silently left: turbulence keeps the lagged treatment.
           Flipping it to Exact is a separate, separately-authorizable change that needs its own
           frozen gate and a re-derivation of the migration oracle. Reported for TODO.md.
```

## 5. Inherited tolerances for G4(a)(b)

G4 says these are "the existing frozen tolerances, INHERITED VERBATIM from
`tests/integration/species/`". Read from that directory, not chosen here:

```text
tests/integration/species/test_species_diffusion.cpp
    3 grids, each:            l2 < 1e-6 and linf < 1e-6
    boundary flux:            EXPECT_NEAR(flux, exactFlux, 1e-6)
    left/right flux equality: EXPECT_NEAR(leftFlux, rightFlux, 1e-9)
```

G4(c) is `<= 2 outer iterations`, derived in the gate from the thermal path's measured 2.
G4(d) is `EXACTLY 0, bitwise` for a constant field's diffusive flux.

## 6. Findings recorded, not acted on

```text
F1  tests/unit/transport/test_transport_characterization.cpp -- the frozen oracle for P14's matrix,
    sparsity and RHS -- contains ZERO FixedGradient cases. The gradient boundary branch of the
    only scalar transport assembly in the codebase is not characterised at all. That is why the
    lag survived P14's migration gates. P18.2 adds Exact-path coverage; characterising the LAGGED
    branch belongs with whoever owns the turbulence fix.
F2  the species boundary lag remains in P14 for turbulence (§4). Debt, with the reason.
F3  cfd::species::SpeciesSolver::solve() takes ONE species and a single `Real volumetricSource`.
    P18.2 does not change its signature -- the existing shipped passive path must keep working
    byte-identically (G1(b) is bitwise). The new multi-species transport is a separate entry point.
```
