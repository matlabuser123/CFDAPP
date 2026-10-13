# P13-007 — acceptance gate, amendment A1

**The threshold in the frozen gate was derived from the wrong noise measurement.** Recorded here
rather than quietly adjusted, because the number was quoted as the justification for the criterion.

## What the gate said

> **Threshold, derived not chosen:** worst measured run-to-run spread on an admitted metric is
> **7.9%** […] The threshold is **1.25x**, roughly 3x that worst case.

## What was actually measured afterwards

The same binary, recorded four separate times on this host:

```text
                              20x20      40x40      80x80    runtime_per_iteration_ms
noise probe (5-run median)   1.5030     6.8588    30.5893
comparator baseline          1.5358     7.3029    30.8892
suite baseline               2.0648     8.8989    37.4993
suite current                1.8631     8.4574    36.2463

                    max/min    1.37x      1.30x      1.23x
```

**Nothing changed between those runs but the machine's load.** The two slow recordings were taken
while builds were running concurrently.

So `7.9%` was **within-session** spread — five runs back to back, minutes apart, on an otherwise
quiet machine. It is the wrong quantity to set a threshold against a *stored* baseline, where the
machine's state at record time and at check time are unrelated. A 1.25x threshold against a stored
baseline would have fired on 20x20 (1.37x) with no code change at all: the exact false-positive the
gate's own reasoning said it wanted to avoid, and which ends with someone disabling the check.

I quoted a real measurement for a criterion it did not support. That is the error.

## Resolution — control the variable, do not widen the tolerance

The threshold is **not** raised. A tolerance loose enough to accept 1.37x could not detect a 30%
regression, which would make the check worse than useless: it would report PASS over a real
slowdown.

Instead the driver now **refuses to measure on a busy machine**:

```text
1-minute load average > 25% of core count  ->  exit 2, nothing recorded
--allow-loaded                             ->  records anyway, and stamps the environment file
                                               "loaded: yes -- NOT COMPARABLE"
```

`loadavg` is captured into `suite_environment.txt` for every run, so a recording carries the
evidence of the conditions it was taken under rather than requiring trust.

With load controlled, the within-session figure (3.5–7.9%) *is* the applicable one, and 1.25x
remains roughly 3x it.

## The limitation that remains, stated rather than hidden

A load guard makes recordings comparable; it does not make them portable. A baseline recorded on
this i9-14900HX is not a valid baseline for any other machine, and the comparator's environment
check (criterion E2) is what stops that comparison from being made silently.

And the blind spot in the tool's own documentation still stands unchanged: **a regression smaller
than 25% is invisible to this check.**

## Criteria affected

| id | change |
| --- | --- |
| R7 | The threshold is quoted against **within-session** spread (3.5–7.9%), and the gate now states that cross-session variation reaches 1.37x and is controlled by the load guard rather than absorbed by the threshold. |
| S2 | The environment capture must include `loadavg` and whether the run was recorded loaded. |
| **O6** (new) | The driver refuses to record on a machine whose 1-minute load exceeds 25% of core count, unless `--allow-loaded` is given, in which case the run is stamped NOT COMPARABLE. |
| **NV7** (new) | Removing the load guard must let a run be recorded under artificial load — proving the guard is what prevents it, not luck. |

No threshold was weakened: 1.25x is unchanged, and a new criterion (O6) was added that the tree
must pass.
