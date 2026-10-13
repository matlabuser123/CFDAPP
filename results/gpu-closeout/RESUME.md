# GPU closeout — where to pick this up

**State: pushed, locally qualified, waiting on GitHub Actions.**

```text
origin/main   3bd32f918656f471e72b5f6ac7cb6c2cbf75a274   == local HEAD
CI run        35579942600   FAILURE, 17/17 jobs, 3 attempts, no runner ever assigned
KNOWN-GOOD    548401aef0d3cbab9552b1964ceac7efd7ad6154 / CI 35385979133  (unchanged, correctly)
```

## What is left to do, in order

1. **Clear the CI blocker** (needs the account, not this repository — see below).
2. `gh run rerun 35579942600 --failed`, or push the pending metadata commit to trigger a fresh run.
3. Watch all 17 jobs to completion. Require zero unexpected failures.
4. **Phase 9** — update `TODO.md`:
   * `KNOWN-GOOD` → `3bd32f9…` + the green run id + `17/17 PASS` + duration;
   * delete the `PUSHED, NOT YET CI-QUALIFIED` block;
   * delete the `CI BLOCKER` block.
5. **Phase 10** — commit the five pending evidence entries plus that TODO update as a single
   metadata-only follow-up, and push it.

### The convention to use in step 4

`KNOWN-GOOD` should name **`3bd32f9`, the implementation commit**, not the metadata commit that
records it. That is the convention this repository already uses: `d7d74f5
docs(ci-perf-001): record the exact-SHA CI qualification` is exactly such a follow-up, and it
records the SHA of the commit *before* it. Following it avoids the loop in which the recorded SHA
can never equal HEAD.

## The five pending entries

```text
 M TODO.md                                        the post-push state, already written
?? results/gpu-closeout/ci/                       the CI failure evidence
?? results/gpu-closeout/push/push-record.txt
?? results/gpu-closeout/commit/commit-record.txt
?? results/gpu-closeout/summary.md
```

They are deliberately uncommitted: their content is the CI result, and there is none yet.

## One inaccuracy inside the pushed commit

`3bd32f9`'s own `TODO.md` still contains:

```text
PUSH BLOCKER
  none outstanding. The GPU-DISC-001 + GPU-PIPE-001 lineage is uncommitted ...
```

which stopped being true the moment that commit was created. It is corrected in the working tree
and will land with the Phase 10 metadata commit. Recorded here so it is not mistaken for drift.

## What to look at to clear the blocker

The failure signature is: job created, **`runner_name` empty**, **`steps` array empty**, logs
`404 BlobNotFound`, 1–3 seconds, all 17 jobs, three attempts. Nothing ran, so nothing in the pushed
tree can be the cause.

Already ruled out from this side:

| hypothesis | finding |
| --- | --- |
| Actions minutes exhausted | repository is **public** — GitHub-hosted minutes are free and unmetered |
| Actions disabled on the repo | `enabled=true`, `allowed_actions=all` |
| GitHub platform incident | githubstatus.com: Actions operational |
| broken workflow YAML | `ci.yml` unchanged by this lineage; the run built the correct 17-job graph |
| transient | three attempts, one after a deliberate pause, all identical |

Re-probed read-only at 2026-09-21T09:05Z (`ci/reprobe-2.txt`): signature identical on all 17 jobs
of attempt 3, no new run, repo Actions still `enabled=true`. One new observation — a second,
unrelated check provider (`sourcery-ai`) has been `queued` on the same commit since 08:49:48Z and
was never allocated either. Two independent providers stalling on one commit points away from the
pushed tree; it is recorded as an observation, not a diagnosis.

Worth checking in the browser, in this order:

1. **`github.com/settings/billing`** — any payment/spending flag, even on a public repo, can stop
   job allocation account-wide.
2. **The repository's Actions tab** — open run 35579942600. The web UI often shows a banner the
   REST API does not expose, for example a spending-limit notice or "This workflow was not run
   because …".
3. **`github.com/settings/actions`** — account-level Actions permissions and any restriction.
4. **Account status** — a flagged or rate-limited account fails job allocation exactly this way.
   GitHub Support is the route if nothing above shows anything.

## Nothing was weakened

No test, threshold, tolerance or CI configuration was changed to get past this, and no workflow file
was edited. The blocker is recorded rather than worked around.
