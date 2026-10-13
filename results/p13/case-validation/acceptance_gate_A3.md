# P13-002 — acceptance gate, amendment A3

**Amendment A2 is wrong, and control N3 is what found it.** A2 is preserved verbatim and not
withdrawn; this records what it got wrong, the measured numbers, and which criterion actually
measures this item.

---

## 1. What A2 claimed

A2 reported:

```text
                                 baseline    after P13-002
AND rule (the criterion)              3            0
OR  rule (the instrument)            63            3
```

and amended M2 to the AND rule ("vague ⇔ names no file **and** no field"), baseline 3, required 0.

## 2. What is actually true

Re-derived from the **frozen** baseline corpus — the per-row `names_file` / `names_field` columns
recorded before any production change — and from the post-change corpus:

```text
                      rejected   vague(AND)   weak(OR)
  BEFORE P13-002        2537          0          63
  AFTER  P13-002        2537          0           3
```

**The AND-rule baseline was 0, not 3.**

So criterion M2 as amended by A2 was **already satisfied before the change**. It is vacuous: it
returns 0 on the unfixed tree and 0 on the fixed one, and no possible implementation of this item
could make it fail.

## 3. Why A2 got it wrong

A2 reasoned about the 63 rows as "60 manifest + 3 cascade" and inferred that the AND rule would
count the 3 cascades. It would not, and neither group was ever AND-vague:

| row group | names file | names field | AND-vague | OR-weak |
| --- | --- | --- | --- | --- |
| manifest reference (60) | no — the old message named no `.json` file | **yes** — "required **geometry** configuration not found" contains the key | no | yes |
| cascade (3) | **yes** — names `mesh.json` / `boundaries.json` | no — names a different field than the one mutated | no | yes |

Every row named *one* of the two. Nothing in this corpus ever named neither.

**The error was inferring a baseline instead of measuring one.** The numbers were sitting in the
frozen CSVs the whole time; A2 quoted a figure it had not computed. This is the exact failure this
project already has a name for — a criterion the baseline passes vacuously is un-dry-run — and
writing it into an amendment did not make it less so.

## 4. What control N3 did

N3 reverts the manifest site and requires the criterion to regress:

```text
with the manifest site reverted: vague=0 weak=5   (the script expected 5 and 5)
N3: UNDETECTED
```

N3 is **correct**, and its "UNDETECTED" is the finding: `weak` moved (0 → 5 on the single case,
matching 63 across the sweep) and `vague` did not move, because `vague` never could. A control
whose job is to prove a criterion can fail did exactly that, and reported that one of the two
criteria cannot.

## 5. The amended criterion

M2 (AND rule) is **withdrawn as a criterion**. It is vacuous for this corpus and would be
dishonest to claim as a pass. It stays in the instrument as a reported column, because a future
rejection that genuinely names neither file nor field should be visible, and because deleting a
column to make a document tidier is how measurements get lost.

The criterion this item is judged on:

| id | criterion | baseline | after | non-vacuity |
| --- | --- | --- | --- | --- |
| **M2b** | `weak` — a rejection naming no file **or** no field | **63** | **3** | N3: reverting the manifest fix restores the rows |
| M2 | `vague` — naming neither | 0 | 0 | **VACUOUS — withdrawn, reported only** |

M2b's residual 3 are the cascade rows documented in A2 §2, unchanged by this item and correct as
they stand: a message that blames the constraint the edit broke, rather than the edit, is the more
useful message.

## 6. What this does and does not change about the item

Nothing about the implementation, and nothing about the other evidence. What changes is which
number is quoted as the criterion:

```text
claimed before A3:  "M2: 3 -> 0, criterion met"        vacuous, withdrawn
correct:            "M2b: 63 -> 3, criterion met"      non-vacuous, proved by N3
```

The item's other results are untouched and were never in question: 2537 mutations with 0 accepted,
2532 of 2537 messages byte-identical, and the marker-rename regression that no longer breaks the
GUI.

## 7. N3 re-runs with the corrected expectation

The script required **both** columns to move. Only `weak` can. Corrected to require `weak` to
regress, and to report `vague` without gating on it — which is the same change this amendment makes
to the criterion, applied to the control that tests it.
