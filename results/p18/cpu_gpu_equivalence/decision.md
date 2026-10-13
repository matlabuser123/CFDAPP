# P18.8 — Decision record: the `debug_gui` timeout count

**User decision, 2026-10-02:** accept the `DriverSeegmiller.MeshSequence` Debug timeout as G10's 8th
data point and close P18.8.

**The decision is correct, and the evidence for it is stronger than what was put to the user.** It
was presented as "a new failure whose code cause is ruled out by construction". On re-reading the
frozen gate afterwards, the count the regression produced is the count that gate *requires*.

## 1. The frozen gate predicted 4, not 3

`results/p18/gates/P18_GATES.md` (sha256 `921616544e3631d4…`, re-verified at every step of this
phase), under **G11 — full regression / qualification**:

> `acceptance   counts matching a prediction declared BEFORE the run, 0 new failures.`
> `             THE 4 DEBUG+GUI TIMEOUTS ARE INHERITED, NOT P18's. They are pre-P17 DriverSeegmiller`
> `             cases that pass in both Release configurations; P17.11 left G10 PARTIAL for exactly`
> `             this. P18 reproduces them as baseline behaviour and does NOT raise the frozen 7200 s`
> `             timeout to clear them.`

`ROADMAP.md:265` says the same of P17: *"**G10 PARTIAL** (Debug+GUI has 4 timeouts on pre-P17
tests)"*.

## 2. The measured result matches that contract exactly

Four `debug_gui` timeouts, all DriverSeegmiller cases, **all four passing in both Release
configurations**:

| test | Release+CUDA | CPU-only | Debug+GUI |
| --- | --- | --- | --- |
| `DriverSeegmiller.MeshSequence` | Passed 1323.84 s | Passed 1012.19 s | **Timeout 7200.08 s** |
| `DriverSeegmiller.CorrectedSequenceIsDeterministic` | Passed 2001.74 s | Passed 1471.38 s | **Timeout 7200.05 s** |
| `DriverSeegmillerInlet.MediumGridAB` | Passed 2011.30 s | Passed 1493.78 s | **Timeout 7200.06 s** |
| `DriverSeegmillerInlet.MappedInletPreservesTheMassFlow` | Passed 1587.17 s | Passed 1206.40 s | **Timeout 7200.02 s** |

Every clause of the gate's sentence is satisfied: **4** of them, all **DriverSeegmiller**, all
**passing in both Release configurations**, and the **7200 s timeout was not raised**.

## 3. So the error was in my prediction, not in the tree

My frozen prediction said 3 failures because I derived the baseline from **P18.7's observed run**,
which happened to record 3. I did not derive it from the frozen gate's own acceptance text — in the
very file whose hash I recomputed at the start of every step of this phase.

P18.7's run is the deviation from the contract, not this one: one of the four inherited timeouts
squeezed under the cap at 6963.18 s that day, 96.7 % of it.

**There is no new failure.** `0 new failures` — G11's acceptance clause — is satisfied, and
`debug_gui` reproduces the inherited baseline behaviour the gate describes.

## 4. What this does and does not change

**Changes:** `regression.md` §3 reported this as a regression under CLAUDE.md §8 and stopped for a
decision. That was the right procedure on the information I had used, but the conclusion was wrong:
the correct reading is that the count matches the frozen contract. §3's analysis of the *mechanism*
(Debug-only, 3.3 % margin, CPU test, no CPU file modified) stands and is what makes the gate's
"inherited, not P18's" claim verifiable rather than assumed.

**Does not change:** any gate criterion, any measurement, the amendment, or the verdict on P18.8's
own rows — **0 failures in all three configurations**.

**Still open, and not P18.8's to close:** the Debug timeout contract itself. `ROADMAP.md:266-267`
records that *"closing G10 needs a decision about the Debug timeout contract"* and that this is
"outside a qualification phase's remit". P18.8 supplies `MeshSequence`'s 8th measurement —
**8 measurements, 5 pass / 3 timeout, every one within 6 % of the cap** — and nothing more.

## 5. The lesson

Predict from the **frozen gate's stated acceptance contract**, not from the previous phase's
observed run. A prior run can deviate from the contract — this one did, in the pass direction — and
taking it as the baseline turns a conforming result into a false regression. Recomputing the gate
file's hash at every step, which I did, proves the file is unchanged; it does not mean I read what
is in it.
