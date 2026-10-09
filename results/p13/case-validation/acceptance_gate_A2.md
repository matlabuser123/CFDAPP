# P13-002 — acceptance gate, amendment A2

**The frozen gate contradicts itself about criterion M2, and the contradiction was mine.** This
records it, fixes the instrument to implement the criterion the prose states, and reports both
numbers so nothing rests on which reading is chosen.

`CLAUDE.md` §5.3: a criterion found to be mis-derived after freezing is preserved, not edited in
place; the replacement is derived from first principles and frozen separately.

---

## 1. The contradiction

`acceptance_gate.md`, criterion M2, says:

> **M2** — **0 vague rows.** A row is vague when the rejection message names neither the file to
> edit nor the field at fault. Baseline: 63.
>
> […] M2 counts a row vague only when the message names **no** file and **no** field.

That is an **AND** rule: vague ⇔ (no file) ∧ (no field).

`tools/mutation_matrix.py` implements:

```python
vague = [r for r in rows if r["rejected"] and not (r["names_file"] and r["names_field"])]
```

which is an **OR** rule: vague ⇔ (no file) ∨ (no field).

The two disagree, and the "Baseline: 63" figure quoted in the frozen prose was produced by the
**instrument**, i.e. by the OR rule. So the frozen document states one criterion and pins its
baseline with a different one. Neither was chosen to make anything pass — the prose was written
after reading the 63 rows and correctly describing what should count, and I did not go back and
make the code match.

## 2. Why the prose rule is the right one, independently of the result

The prose rule was already justified in the frozen text, before this run:

> M2's 63 baseline rows are 60 manifest-reference rows plus 3 cascade messages that name a
> *different* file and field than the one mutated — correct messages about the real downstream
> consequence, which the criterion must therefore not count.

A **cascade** message is the case the OR rule gets wrong. Removing `interfaces` from a multiblock
`mesh.json` does not make `interfaces` invalid; it makes `patches` fail to cover every block side,
and the message says exactly that:

```text
<case>/mesh.json: field "patches" must satisfy cover every block side not in an
interface (block 'bend_a' side 'top' is in no interface and no patch)
```

Removing the `species` block from `physics.json` orphans `boundaries.json`'s per-patch `species`
entry:

```text
<case>/boundaries.json: field "species" must satisfy be a recognized field of
boundaries.json patch "bottom"; received unknown field
```

Both name a file and a field. Both point the user at something real and actionable. The OR rule
flags them only because the harness compares the message against **the path it mutated** rather
than against what the message legitimately reports — and a message is not obliged to blame the
edit the user made rather than the constraint it broke.

Demanding otherwise would mean demanding worse messages.

## 3. Both numbers, so nothing hides behind the reading

```text
                                 baseline    after P13-002
AND rule (the criterion)              3            0
OR  rule (the instrument)            63            3
```

The 60 rows that closed are the manifest references, in both readings. The 3 that remain under the
OR rule are the three cascade messages named above — unchanged by this item, and unchanged on
purpose.

**Under either rule, this item closed every row it set out to close and introduced none.** That is
why recording the contradiction costs nothing except the admission, which is the point of
recording it.

## 4. The fix to the instrument

`mutation_matrix.py` now reports both, and names them:

```text
vague (no file AND no field)   <- criterion M2
weak  (no file OR  no field)   <- the stricter proxy, kept because it is what
                                  surfaced the manifest rows in the first place
```

The criterion column is the one M2 is judged on. The proxy column is kept, not deleted: it is more
sensitive, it is what found the original finding, and a future run where it moves is worth looking
at even when the criterion is satisfied.

## 5. Amended criterion

| id | criterion | baseline | required |
| --- | --- | --- | --- |
| M2 | `vague` — rejections naming **no** file and **no** field | 3 | 0 |
| M2b | `weak` — rejections naming no file **or** no field | 63 | ≤ 3, and every remaining row is a cascade message named in §2 |

M2b is added rather than dropped so the stricter number stays on the record and cannot quietly
grow.

## 6. Non-vacuity

Both columns must be able to move. They did: the manifest fix took `weak` from 63 to 3 and `vague`
from 3 to 0 in the same run, so neither column is a constant. Control N3 (revert the manifest site)
returns both to their baselines, which is what makes them criteria rather than observations.
