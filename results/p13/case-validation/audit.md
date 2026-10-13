# P13-002 — Robust case validation: audit

Written before any production change, on the tree that closed P13-001.

The brief asks for an inventory of "all currently accepted case inputs" and a list of what must be
validated. Inventorying by hand produces a list of what I thought of. This audit is built the other
way round: a **mutation matrix** generated from each case's own JSON, so a configuration field
added after this was written is swept without the harness knowing about it.

---

## 1. What already exists

`TODO.md` lists this item as unstarted. As with P13-001, that is not what the source says.

```text
src/io/case/JsonUtil.cpp            221 lines   16 rejection sites
src/io/case/CaseConfigParser.cpp     62            1
src/io/case/GeometryConfigParser.cpp 54            6
src/io/case/MeshConfigParser.cpp    385           32
src/io/case/PhysicsConfigParser.cpp 603           42
src/io/case/BoundaryConfigParser.cpp 219           6
src/io/case/SolverConfigParser.cpp  315           24
src/io/CaseReader.cpp               270           12   (cross-file checks)
src/io/CaseBuilder.cpp              691           ~12  (construction-time refusals)
                                              -------
                                                 ~151 distinct rejections
```

The error model is already structured, just not as a type. `detail::throwConfigError` emits

```text
<file>: field "<field>" must satisfy <constraint>; received <value>
```

which carries all four things the brief asks a structured diagnostic to carry. `rejectUnknownKeys`
means a mistyped key is a rejection rather than a silently-ignored setting, and `CaseReader` adds
cross-file checks (a 3D box needs `nz`; every mesh patch needs a boundary condition; no patch may
name something the mesh does not have).

Mesh validity is a separate, already-built layer: `MeshQuality.hpp` grades a mesh
`Valid` / `ValidWithWarnings` / `Invalid`, and `CaseBuilder` refuses an `Invalid` one.

So this item is **not** a rewrite. It is: find what the existing 151 rejections miss, close those
gaps, and make sure every rejection says something a user can act on.

---

## 2. The instrument

`tools/mutation_matrix.py`. For a known-good case it walks every JSON leaf and every JSON object,
applies type-appropriate mutations, runs the **production binary**, and classifies by exit code.

The pass criterion is decidable without inside knowledge:

```text
exit 2  InvalidCase       the reader or builder refused it -- ProjectRunner returns
                          before SIMPLE::solve is called
exit 0  Converged         it RAN
exit 3  DidNotConverge    it RAN
exit 4  NumericalFailure  it RAN
```

so "rejected before solver execution" is exactly `exit 2`, and every other code means the mutated
case was accepted.

### 2.1 Only mutations that are invalid for every field

```text
type      a number or array replaced by a string
enum      a string replaced by an unrecognised value
missing   the key removed
unknown   an unrecognised key added to an object
nan/inf   a numeric leaf replaced by a raw NaN / Infinity literal
```

A `negative` or `zero` mutation is **not** applied blindly. Negative is valid for a velocity
component and invalid for a density; a sweep that could not tell them apart would report its own
assumption as a defect. Those live in an explicit list, one field at a time, each with the reason
it is positive.

### 2.2 The instrument had to be corrected twice, and that is recorded

The first run reported **5 findings**. All five were the instrument's fault, and checking them
against the parsers is what showed it:

| reported | actually |
| --- | --- |
| `case.json name` accepts any string | it is free text — `getRequiredString`, no vocabulary |
| `case.json description` accepts any string | free text, and optional |
| `case.json description` may be missing | `getOptionalString(..., "")` — a documented default |
| `case.json format_version` may be missing | `getOptionalInt(..., 1)` — a documented default |
| `physics.json reynolds_number` may be missing | `PhysicsConfig.hpp`: "optional reporting-only metadata" |

A matrix that treats every key as required reports every documented default as a gap. Fixed by
naming the optional keys and the free-text keys explicitly, each citing the parser line that makes
it so — not by loosening the criterion.

The `unknown` mutation was also mis-attributed: it adds a sibling key, so it belongs to the
enclosing object, not to whichever leaf the sweep was visiting. Applying it once per object removed
~40 duplicate rows and made each message attributable to the key actually at fault.

**Non-vacuity** is asserted before any row is believed: the unmutated case must RUN. A baseline
that was already rejected would make every row meaningless, and the harness stops rather than
reporting.

---

## 3. Result on the reference case

`tests/data/cases/valid_cavity`, budget lowered to 2 iterations so an accepted mutation returns
immediately:

```text
mutations applied      190
rejected before solve  190
ACCEPTED               0
```

Every single mutation of every field is refused before the solver runs. That is a strong statement
about the existing pipeline, and it is the reason this item is about gaps rather than foundations.

---

## 4. The one finding

Five rows are rejected but their message does not name the file a user must edit:

```text
case.json "geometry": "geomety.json"      (a plausible typo)

  Case configuration error: required geometry configuration not found:
  /tmp/.../geomety.json
```

Compare the same class of mistake one level down:

```text
physics.json "densty": 1.0

  Case configuration error: /tmp/.../physics.json: field "densty" must satisfy
  be a recognized field of physics.json; received unknown field
```

The second names the file, the field, the constraint and the received value. The first names the
role and the resolved path — enough to see the typo, but it does not say that `case.json`'s
`geometry` key is the thing to fix. It is the only place in the case pipeline where a rejection
drops out of the project's own message convention, and it affects all five manifest references.

Not a correctness defect: the case is refused, with exit 2, before anything is solved. A
diagnostics defect, which is exactly what this item is for.

---

## 5. What the sweep still has to cover

One 2D laminar cavity exercises a fraction of the configuration surface. `tools/sweep.sh` runs the
same matrix over every family — 3D, thermal, species, thermal+species, multiphase, compressible,
coupled compressible, graded, multiblock, non-orthogonal quad, and a derived k-epsilon case
(no turbulence case ships under `cases/`, so one is built from the cavity rather than leaving that
surface unswept).

Findings from that sweep are recorded in `summary.md`, including any that turn out to be the
instrument's fault again.

## 6. What the matrix deliberately does not reach

* **Inside a list of objects** — species entries, multiblock blocks, boundary patches. Mutating
  those needs per-schema knowledge, which is exactly the assumption this sweep avoids. They are
  covered instead by the existing fixture cases (`bad_boundary`, `bad_physics`, `bad_solver`,
  `missing_mesh`, `malformed_json`, the three `mesh_quality_*` cases) and by hand-written rows.
* **Cross-patch physical well-posedness** — an inlet with no outlet, or an open boundary with no
  pressure reference. Per-patch type and value checking exists; a whole-domain check does not.
  Whether that is a gap worth closing, and what the correct criterion is, is decided in
  `acceptance_gate.md` rather than assumed here.
* **The graceful GPU→CPU linear-solver fallback.** Documented, deliberate behaviour (P6-GPU-002)
  reported through `SolverResult::backendUsed` and `gpuBackendFallbacks`. A fallback is not an
  unsupported configuration, and turning it into a rejection would be a behaviour change this item
  does not have authorization for.
