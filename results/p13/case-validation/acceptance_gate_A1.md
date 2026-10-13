# P13-002 — acceptance gate, amendment A1

**Control N1 was mis-designed. Its result is preserved verbatim in `logs/N1_control.log` and is
not withdrawn.** This amendment records what it actually showed, why the original control could
not have shown what it claimed, and what replaces it.

`CLAUDE.md` §5.3: preserve the original failure, investigate without changing production, derive
the replacement from first principles rather than from the observed output, and freeze the
amendment separately with its own dry-run and non-vacuity check.

---

## 1. What N1 claimed, and what it did

The frozen criterion:

> **N1** — Reword one `throwConfigError` message (e.g. `must satisfy` → `must be`). The
> string-scraping path loses the field; the structured path does not.
>
> N1 is the decisive control. If rewording a message does **not** break the current GUI, then the
> scraping is not fragile and this item's premise is wrong.

Result, on the unchanged tree:

```text
1. the test passes on the unchanged tree
   PASS -- the GUI recovers field=nx today
2. reword the message, change nothing else
   the test STILL PASSES after rewording

N1: PREMISE NOT SUPPORTED
```

## 2. Why it could not have shown what it claimed

`throwConfigError` composes its message from three parts:

```cpp
message << path.string() << ": " << fieldLabel(field) << "must satisfy " << constraint
        << "; received " << received;

// fieldLabel(field)  ->  field "nx"
```

`fieldForMessage` (the GUI's scraper) searches for the token `field "` and reads to the next
quote. It never looks at the constraint clause.

The reword I chose — `must satisfy` → `must be` — changes the **constraint clause**. The scraper
does not read it. So N1 changed a part of the message that could not affect the result it was
measuring, and "the test still passed" says nothing about fragility either way.

**This is a defect in the control, not a refutation of the premise.** It is also not a threshold
being weakened: the criterion demanded a *harder* thing than it tested.

The lesson, recorded because it generalises: a control that perturbs an input must perturb the
input the mechanism under test actually consumes. Choosing a plausible-looking perturbation is not
the same as choosing a load-bearing one.

## 3. The replacement

**N1b** — change `fieldLabel`'s own marker, which is the token the scraper reads:

```cpp
- return field.empty() ? std::string() : ("field \"" + std::string(field) + "\" ");
+ return field.empty() ? std::string() : ("key \"" + std::string(field) + "\" ");
```

`field` → `key` is the most ordinary wording change imaginable and touches nothing else. The
criterion is unchanged in substance: a maintainer's reword must break the scrape, or the scrape is
not fragile.

Derived from the mechanism, not from the observed output: the marker was identified by reading
`fieldForMessage` and `fieldLabel`, before N1b was run.

Same safety discipline as N1: sha256 before, unique-string patch asserted to match, restore and
re-hash on every exit path, and the test required to pass again afterwards.

## 4. What this changes about the item's justification — stated plainly

Whatever N1b returns, **the case for P13-002 no longer rests primarily on fragility**, because N1
showed I had not established it. It rests on two things the audit demonstrated directly, neither
of which depends on how brittle the scrape is:

1. **Absence.** For any rejection that does not use `throwConfigError`, the scrape returns nothing
   at all. That is 60 of the 63 vague rows — every manifest file reference, across all 12 case
   families — plus every `CaseBuilder` construction error. Not fragile: simply empty. The GUI's
   `ValidationPanel.qml` renders a per-field slot that is blank for all of them.
2. **Coupling.** `validateDraft` drives UI state off a prose substring:

   ```cpp
   if (message.contains(QStringLiteral("mesh is invalid"))) {
     meshQuality_ = QVariantMap{{"status", QStringLiteral("invalid")}, ...};
   }
   ```

   The mesh-quality panel's status is decided by searching an error string for a phrase. No test
   covers the coupling.

If N1b shows the marker is load-bearing, fragility is added to that list as a third, now-evidenced
reason. If N1b shows the scrape survives even a marker rename, fragility is **dropped from the
justification entirely** and the item proceeds on (1) and (2) alone — which are sufficient on
their own and are already measured.

## 5. Amended criterion

Group N's N1 is replaced by:

| id | control | must break |
| --- | --- | --- |
| N1b | `fieldLabel`'s `field "` marker renamed to `key "` | the GUI's field extraction, if the scrape is load-bearing |

and the gate's reading of the outcome is fixed **in advance**, so neither result can be spun:

```text
N1b breaks the GUI test        -> fragility is evidenced; it joins absence and coupling
N1b does not break it          -> fragility is NOT claimed anywhere in this item's evidence;
                                  the item proceeds on absence and coupling, both measured
```

Either way the item proceeds, and either way the summary states which.

## 5a. N1b result, recorded against the pre-committed reading

```text
1. the test passes on the unchanged tree
   PASS -- the GUI recovers field=nx today
2. rename the marker the scraper reads, change nothing else
   the test FAILS after the rename -- the GUI lost the field
3. restored
   sha256 0893f1b8... IDENTICAL, and the test passes again

N1b: FRAGILITY DEMONSTRATED
```

Per §5, this is the branch in which fragility **is** evidenced, so it joins absence and coupling.
All three of this item's justifications are now measurements:

| justification | measured by |
| --- | --- |
| fragility | N1b — one renamed token breaks the GUI's field extraction, no test catches it |
| absence | 60 of 63 vague rows, all 12 families, plus the 16 + 2 sites that never had a field |
| coupling | `validateDraft`'s `message.contains("mesh is invalid")` driving panel state |

N1 remains on the record as a mis-designed control that showed nothing. It is not retroactively
counted as evidence for anything.

## 6. Non-vacuity of the amendment

N1b is itself a control, so its non-vacuity is that step 1 must pass before step 2 runs: the GUI
test has to recover `field=nx` on the unchanged tree, or the control is measuring a test that was
already failing. N1 established that (step 1 PASS) and N1b re-establishes it on its own run.
