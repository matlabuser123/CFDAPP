# P13-003 — CLI / GUI consistency: audit

Written before any production change, on the tree that closed P13-002.

---

## 1. There is already exactly one execution path, and it is provable

```text
apps/cli/main.cpp:251        ProjectRunner::run(caseDirectory, options)
apps/gui/…Controller.cpp:234 session_.run(options)
src/app/CaseSession.cpp:197    -> ProjectRunner::run(*dir, options)
```

and neither client constructs a solver of its own:

```bash
grep -rn 'SIMPLE(\|\.solve(' apps/cli apps/gui --include=*.cpp   # no hits
```

`CaseSession::run` forwards the options untouched, with one deliberate exception: it OR-s the
caller's `cancellationCheck` with its own `requestCancel()` flag, so a Stop button works even for
a caller that supplied no check of its own.

So P13-003 is **not** about merging two solver paths. `ProjectRunner.hpp`'s "ONE SOLVER BACKEND"
rule has held since P5. What this item is about is what sits *on top* of that one path.

## 2. Finding 1 — the two clients expose disjoint subsets of the same backend

`ProjectRunOptions` has five fields. Neither client sets more than three of them:

| option | CLI | GUI |
| --- | --- | --- |
| `progressCallback` | no | **yes** |
| `cancellationCheck` | no (documented: the CLI never cancels) | **yes**, via `CaseSession` |
| `checkpointPath` | **yes** (`--checkpoint`) | no |
| `checkpointInterval` | **yes** (`--checkpoint-every`) | no |
| `resumeFrom` | **yes** (`--resume`) | no |

```bash
grep -rn 'checkpoint\|resume' apps/gui/*.hpp apps/gui/*.cpp apps/gui/qml/*.qml   # no hits
```

Checkpoint and resume are **not reachable from the GUI at all** — no property, no invokable, no
QML. This is the asymmetry the brief names ("checkpoint/resume uses common implementation"): the
common implementation exists and the GUI simply cannot ask for it.

Note what this is *not*: it is not a second code path, and it is not a numerical difference. The
plumbing from `SimulationController` through `CaseSession` to `ProjectRunner` already carries all
three options untouched. Only the surface is missing.

## 3. Finding 2 — the existing equivalence check is weaker than it reads

`apps/gui/tests/test_case_editing.cpp` calls itself the "CLI/GUI Round-Trip Gate":

```cpp
const cfd::app::ProjectRunResult cliEquivalentRun = cfd::app::ProjectRunner::run(target.path());
EXPECT_EQ(cliEquivalentRun.status, cfd::app::ProjectRunStatus::Converged);
```

It asserts that a GUI-saved case **converges** when run through the CLI's entry point. It does not
compare anything the brief asks for:

```text
required by the brief        checked today
  solver backend               no
  settings                     no
  final fields                 no
  iteration count              no
  convergence state            status only
  residual history             no
  mass balance                 no
  exported results             no
```

Two runs that converge to different answers would pass it. That is a weak gate wearing a strong
name, and it is the one place where "CLI == GUI" is currently asserted rather than measured.

## 4. Finding 3 — CI does not cover the CLI binary's own argument handling

Carried forward deliberately from P13-001 (`results/p13/restart/summary.md` §6a). The
checkpoint/resume round trip is covered in CI through `ProjectRunner`; the `cfdapp` binary's flag
parsing — `--checkpoint`, `--checkpoint-every`, `--resume` and their rejection paths — is covered
only by a local shell harness. The ctest entries were written during P13-001 and deferred to this
item, whose gate already owns "error/validation parity".

## 5. What is NOT a divergence, checked rather than assumed

* **Case loading** — both call `CaseReader` through `ProjectRunner`. The GUI additionally has
  `validateDraft`, which writes the in-memory draft to a scratch directory and runs
  `CaseReader` + `CaseBuilder` over it. That is a different *operation* (validate an unsaved edit),
  not a second validation core: it uses the same two classes, and after P13-002 it consumes the
  same `cfd::CaseDiagnostic`.
* **Backend selection** — `solver.json` only, for both. Neither client can override it.
* **Solver configuration** — `CaseBuilder` only, for both.
* **Export** — `ProjectRunner` calls `ResultExporter` once, after the solve, for both.
* **Errors** — one `ProjectRunStatus`, one `exitCodeFor`. After P13-002 both clients also receive
  the same `caseDiagnostic`.

## 6. Scope this item will not take

* **No GUI redesign.** The brief says "do not refactor unrelated GUI code".
* **No claim of visual GUI acceptance.** `CLAUDE.md` §4: automated GUI tests do not replace human
  acceptance, and this session cannot click a window. Whatever is added is verified at the
  controller/service layer, and the limit is stated rather than glossed.
* **No CLI cancellation.** The CLI has no event loop and no second thread; adding one to reach
  parity on `cancellationCheck` would be a new feature, not a consistency fix. Recorded as the
  documented, intentional asymmetry it already is.
