# P17.9 — audit of existing shock / discontinuity infrastructure

`logs/00_audit.log`, run at HEAD `3bd32f918656f471e72b5f6ac7cb6c2cbf75a274` before any P17.9 source
was written. Searched roots: `include src cuda tests apps results/p17`.

---

## 1. The term sweep (the authorization's §1 list, verbatim)

```text
term                     files  lines   what it actually is
---------------------------------------------------------------------------------------------------
Sod                         44    278   P17.7's reference data + P17.8 comments. NO benchmark runs one.
shock tube                   7     13   results/p17/audit/*.md only -- prose, not code
normal shock                12     14   prose and comments. No case, no relations implemented.
Rankine-Hugoniot            13     17   HllcFlux.hpp's derivation comment, the P17.8 amendment.
                                        The RELATIONS are documented; nothing EVALUATES them.
supersonic                  75    264   P17.8's SupersonicInflow/Outflow boundary kinds
contact discontinuity        2      2   P17_GATES.md and this audit script. Zero in code.
compression wave             1      1   this audit script only -- ABSENT
expansion wave               8     15   prose in results/p17/audit/*.md -- ABSENT in code
Riemann exact                9     13   P17.7's exact_riemann.py and comments referring to it
shock position               7      7   prose. No detector anywhere.
density ratio               26     55   P17.7's HLLC reference-data table labels
pressure ratio              26     76   likewise
Mach relation                1      1   this audit script only -- ABSENT
```

## 2. `results/p17/hllc/tools/exact_riemann.py` — real, frozen, and NOT sufficient

The one substantial asset. sha256 `a50242580f620c42280df042a3bd4802739c87d6dfcaa163b70236a54609d95c`
(post-amendment; P17.7 froze it at `cddf94e5…` and the star-energy amendment moved it).

```text
EXPORTS
  exact_star(...)              -> (p*, u*) by Newton on the pressure function
  exact_wave_extremes(...)     -> the leftmost and rightmost signal speeds
  exact_sample_at_zero(...)    -> the state at xi = x/t = 0, ONLY
  hllc(...), davis_speeds(...) -> the HLLC ALGORITHM at 60 digits, for implementation comparison
```

```text
WHY IT IS NOT ENOUGH, and both reasons matter:

1  COVERAGE. P17.7 needed the solution on one ray (the Godunov interface state), so
   exact_sample_at_zero is hard-wired to xi = 0. P17.9 needs the WHOLE PROFILE at a fixed time, and
   the four wave speeds SEPARATELY -- head, tail, contact, shock -- which that file never exposes.

2  INDEPENDENCE. P17.7's own AMENDMENT_star_energy_sign.md records how a reference can be wrong and
   still agree with the code to 3.25e-16: both were written from the same misremembered formula.
   Importing that file's star solve into P17.9 would inherit one author's understanding rather than
   provide a second opinion.
```

```text
SO: NEEDS EXTENSION. P17.9 writes its own star solve INDEPENDENTLY (tools/riemann_reference.py),
cross-checks it against P17.7's (agreement to < 1e-45, measured), and additionally asserts eight
DEFINING PHYSICAL PROPERTIES that neither implementation can satisfy by accident.
```

## 3. What P17.8 already integrated at solver level

```text
tests/integration/compressible/test_density_based_canonical.cpp
  C4  a smooth advected entropy pulse, Ma ~ 2          -> P17.9 reuses the CASE for §16
  G2  open-domain conservation balance                 -> the diagnostics P17.9 §25 reuses
  G1  closed-domain momentum vs wall impulse
  §27 low-Mach sanity
tests/unit/compressible/test_density_based_solver.cpp
  C3  an advected CONTACT, p and u uniform             -> P17.8 called this a solver-integration
                                                          test and explicitly NOT shock validation
```

## 4. The solver API P17.9 drives, unchanged

```text
DensityBasedSolver(settings, thermodynamics)
  fromPrimitive / toPrimitive / assembleResidual / globalTimeStep / applyUpdate / solve
BoundaryKind: InviscidWall, SupersonicInflow, SupersonicOutflow
  -- NO subsonic open boundary, which is why every P17.9 case is built to need only these three
MeshGeometry::createCartesian2D(nx, ny, lx, ly)   -- 2D only; every case here is 1D on a 2D mesh
```

## 5. Existing validation conventions reused rather than reinvented

```text
include/cfd/validation/GridConvergence.hpp        the refinement-study conventions
results/validation/**                              the report layout
results/p17/density_based_solver/tools/            the controls / freeze / integrity harness shapes,
                                                   and prove_format_only.py, reused verbatim
```

## 6. Requirement classification (§1)

```text
requirement                          classification    note
---------------------------------------------------------------------------------------------------
Sod shock tube                       MISSING           no benchmark runs a solver anywhere
Contact discontinuity                NEEDS EXTENSION   P17.8's C3 is a solver-integration test at
                                                       one state; P17.9 adds stationary + moving
                                                       with position, smearing and conservation
Supersonic advection                 NEEDS EXTENSION   P17.8's C4 case reused, with a POSITION
                                                       measurement (centroid) added
Normal shock                         MISSING
Compression / expansion wave         MISSING
Shock-position validation            MISSING           no detector existed
Density-ratio validation             MISSING
Pressure-ratio validation            MISSING
Velocity validation                  MISSING
Conservation validation              ALREADY SATISFIED P17.8's G1/G2 diagnostics, reused unchanged
exact Riemann reference              NEEDS EXTENSION   section 2
---------------------------------------------------------------------------------------------------
ALREADY SATISFIED 1 · NEEDS EXTENSION 3 · MISSING 7
```

## 7. One probe of mine that misreported, recorded rather than fixed silently

```text
Section 6's sanity control searched for '\bsod\b' under results/p17/hllc/ and returned 0, which
reads as "there is no reference data". The truth is that \b cannot match inside sod_literature --
underscore is a word character. The control was the thing that was blind, not the tree.
Corrected to print BOTH forms so the difference is visible: \bsod\b 0 files, plain sod 19 files.
Same fail-open shape as P17.7's audit §6 and P17.8's Q2 probe: a grep for the wrong pattern
printing a zero that reads as absence.
```
