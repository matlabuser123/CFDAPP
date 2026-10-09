# P13-005 — acceptance gate, amendment A1

**The gate as frozen contradicts itself.** Group L assigns levels (`L1` Error, `L5`/`L6`/`L7`
Error, `L8` Info); Group V's `V4` requires the default run's stderr to be **unchanged** from the
baseline. The default level is `Info`, so any point at Info or above prints by default. The two
groups cannot both hold.

I froze this without dry-running the interaction between them. That is the same class of error as
A2/A3 in P13-002: a criterion stated but not exercised against the thing it constrains.

## Measured, not argued

`logs/02_V4_FAIL_as_frozen.log` — every log point wired, no default changed:

```text
stdout differing 0 / 7      C2 PASS
stderr differing 7 / 7      V4 FAIL
```

The three rejection cases are the informative ones. Their baseline stderr was **not** empty (103,
217, 152 bytes): the CLI already prints the rejection. The `[ERROR]` line is a verbatim **second
copy** of a message the user is already shown.

## Resolution — V4 is binding and is NOT weakened

V4 stands exactly as frozen: the default run's stderr must be byte-identical to the baseline for
all 7 cases. The level assignments in Group L are what change, and they change because of what the
measurement showed, not to make a criterion pass:

**Rule, stated once and applied everywhere:** `ProjectRunner` returns every failure to its caller
and the caller reports it. It therefore does **not** also log failures at Error. A second copy on
the same channel is noise, not diagnostics — measured above for L1, and L7 would do the same to
`Run error:`.

| point | frozen level | amended level | why |
| --- | --- | --- | --- |
| L1 case rejection | Debug / **Error** | **Debug** | the frozen row already offered Debug; the CLI prints the identical text (measured) |
| L5 linear-solver failure | Error | **Debug** | reported by the client on stdout (`Converged: no (momentum solve failed)`) |
| L6 non-finite state | Error | **Debug** | same |
| L7 export failure | Error | **Debug** | the CLI prints `Run error: <same text>` on stderr |
| L2 fallback | Warning | Warning | **unchanged** — a request silently not honoured is returned nowhere |
| L3, L4, L8 | Debug / Info | unchanged | |

And the CLI's default level becomes **Warning**, so the Info tier (`L4`, `L8`) is opt-in. That is
V4's own stated rationale — *"diagnostics are available on request, not imposed"* — made literal.

**This changes no existing behaviour, and that is measured rather than assumed:** the audit found
exactly 2 logging call sites in the whole tree, both `warning()`. Warning is still emitted at the
CLI's new default, so C3 holds. Nothing logs at Info or Debug today, so nothing that used to appear
stops appearing.

## What the replacement must still pass

The rule that disqualified the frozen levels must not disqualify the replacement: the points must
still be **emitted and observable**, not quietly dropped. That is criterion **V3**, which is
unchanged and now carries the weight — `--log-level debug` must show strictly more than
`--log-level error`, and the L1–L8 points must be visible in it. A control is added:

| id | control | must break |
| --- | --- | --- |
| N4 | delete one L point's emission | V3's debug-run line count, and the L-point check |

## What this costs, stated rather than hidden

`--log-level error` now shows nothing from `ProjectRunner`. That is deliberate: the log level
controls the diagnostic **trail**, never the outcome. The outcome is returned in
`ProjectRunResult` and printed by the client unconditionally, so an operator cannot miss a failure
by choosing a quiet level. Recorded in `docs/user_guide/cli.md`.
