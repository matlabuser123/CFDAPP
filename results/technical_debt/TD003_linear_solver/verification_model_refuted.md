# TD-003 C3 — the recursion model is REFUTED by its own pre-registered check. SEVENTH failure. C3 STOPS HERE.

```text
STATUS:      **MODEL REFUTED, and C3's hypothesis cycle is STOPPED.**
             The pre-registered test failed on both criteria:
               rho(M)        1.000000 after hitting the 500-iteration cap -- a NON-RESULT, not
                             a measurement, and an instrument defect of mine (§2)
               injection     8.305241e-09 .. 1.684183e-07, a 20.28x SPREAD over 40 steps --
                             the recursion REQUIRES s to be roughly constant (§3)
               prediction    1.031354e+04 against 8.822866e-07, ratio 1.17e+10, FAIL -- but
                             this is division by (1 - 1.000000), so it carries NO information
                             about the fixed point (§2.1)
             **The second check is what genuinely refutes the model**, independently of the
             broken gain: s is not constant, and its upper end is within 5x of the entire
             deviation it was meant to explain.
             This is the SEVENTH failed hypothesis in this criterion. Three of the seven died
             because their instruments measured something DERIVED from their own subject. That
             is a signal about the approach, not only about the code, so the cycle stops.
SCOPE:       one instrumented run, read-only, on a copy outside the repository. No production
             change, no test change; the 1e-8 stands untouched and the test still fails.
EVIDENCE:    logs/10_gain_prediction.log · tools/instrument_full.patch
COMMIT:      carried as evidence. C3 is left OPEN with the open question stated in §5.
```

---

## 1. THE PRE-REGISTERED TEST, AND ITS VERDICT

The model: `d_new = M d_old + s`, with `M = A^-1 diag(rho V/dt)`.

Pre-registered in `td003_patch8_gain.py` before the run, with the verdict computed *by the binary*
rather than by me afterwards:

```text
gain      rho(M) by POWER ITERATION -- exact arithmetic on the assembled operator
inject    s[c] = d_new[c] - (A^-1 D d_old)[c], per cell, from fields already in hand
predict   d* = max|s| / (1 - rho)
PASS iff  1/3 <= d* / 8.822866e-07 <= 3
```

```text
rho(M)                        1.000000   (500 iters -- the cap)
injection max|s|, 40 steps    8.305241e-09 .. 1.684183e-07   spread 20.28x
PREDICTED d*                  1.031354e+04
MEASURED                      8.822866e-07
ratio                         11689557948.75        ***FAIL***
```

## 2. THE GAIN FIGURE IS AN INSTRUMENT DEFECT OF MINE, NOT A PROPERTY OF `M`

`rho(M) = 1.000000` arrived *with the iteration count pinned at the 500 cap*. A spectral radius
landing on exactly `1.000000` while failing to converge is not a dominant eigenvalue; it is a
power iteration that never settled.

The likely cause is my own normalisation: `v` is scaled to max-norm 1 each sweep and `||z||_inf` is
then read as the eigenvalue estimate. That is a valid Rayleigh-style estimate only once the
iteration converges. **It did not, and the instrument reported the number anyway instead of
treating non-convergence as invalid.** A correct implementation returns "did not converge" and the
caller refuses to predict.

### 2.1 So the prediction carries no information

`d* = max|s| / (1 - rho)` divides by `1 - 1.000000`. The resulting `1.03e+04` -- a mass-fraction
deviation of ten thousand -- is division by approximately zero, not a near-miss. **The ratio of
1.17e+10 is therefore a real FAIL of the pre-registered criterion but says nothing about where the
fixed point actually is.** Recorded so no later reader mistakes it for a measurement of the
mechanism.

## 3. THE CHECK THAT GENUINELY REFUTES THE MODEL

The second check needed no gain at all, which is why it was included:

```text
max|s| over 40 steps     8.305241e-09 .. 1.684183e-07     spread 20.28x
```

`s` was defined as the per-step SOLVE error, so the recursion requires it to be roughly constant.
It varies by a factor of 20. And its upper end, `1.684183e-07`, is **within a factor of 5 of the
entire observed deviation of 8.822866e-07** -- so `s` is not a small perturbation driving a slow
accumulation toward a fixed point; it is comparable to the quantity it was introduced to explain.

**`d_new = M d_old + s` is therefore refuted as a description, whatever `rho(M)` turns out to be.**

## 4. WHY THE CYCLE STOPS HERE

Seven hypotheses have now failed in this one criterion:

```text
1  tolerance-limited solve         refuted: spread 4.8x, not constant; non-monotonic in tolerance
2  round-off accumulation          refuted: steps x2.89 while the deviation FELL x0.57
3  the Patankar source sum         refuted by inspection: the transport source is an explicit ZERO
4  kappa x residual (magnitude)    refuted: kappa measured 26.28, not the assumed 1000
5  round-off x near-singular op    refuted: condition number is 33; r is 5.6e+08 x eps*||b||
                                   -- and this one had already been COMMITTED as a finding
6  feedback via the closure        refuted by a causal control: removing it changed r by 14 %
7  the recursion d = M d + s       refuted here, by its own independent check
```

**Three of the seven (1, 5, 7) failed because the instrument measured something derived from its
own subject**, and one more (6) tested a term that contributes 3.85 %. That pattern is about the
method, not the code, and another instrument is more likely to produce an eighth plausible story
than an answer.

Two facts also make continuing poor value:

```text
C3 is MIS-SCOPED        no composition-write site is the linear solver, the solve contributes
                        ~1 %, and an exact solve still fails the gate by 230x. C3 does not belong
                        to a linear-solver item (verification_instrumented_residuals.md).
its threshold is INVALID 1e-8 is in no frozen gate, was never derived, and has never been met in
                        81 recorded measurements (verification_gate_provenance.md).
```

So the criterion is gated by a number that was never derived, for a defect that is not this
item's, with a mechanism that seven attempts have failed to pin down.

## 5. WHAT IS ESTABLISHED, AND THE OPEN QUESTION AS IT STANDS

```text
ESTABLISHED
  the SOLVE contributes ~1 %: an exact Thomas solve of the same systems leaves 99.97 % of the
    deviation, 2.301527e-06, still 230x the gate
  the TRANSIENT term produces 98.79 % of the RHS residual r, attributed by isolation with the
    decomposition CLOSING (sum of parts / total = 1.0263)
  r^trans = rho V/dt * d_old -- so the transient CARRIES d; it is the correct non-conservative
    rho dY/dt form and is probably not a defect
  the per-species matrices are BIT-IDENTICAL, so the telescoping sum_i(A y_i) = A(sum_i y_i) is exact
  the INITIAL CONDITION is not the seed: the seeded profile closes to 1.110223e-16 (0.5 ulp)
  the chemistry half CANNOT seed: sum_i dY_i = 0 exactly, by exact negation at two species
  d is already 4.633173e-09 by the end of step 1
  A is WELL-CONDITIONED: ||A||_inf * ||A^-1||_inf = 33
  the 1e-8 bound is underived and has never been met: 0 of 81 measurements

THE OPEN QUESTION, stated for whoever picks this up
  Why does d reach ~1e-6 and stay there? The transient explains PERSISTENCE and the chemistry and
  seed are excluded, but no mechanism accounts for the MAGNITUDE. Seven candidate mechanisms have
  been refuted; their refutations are recorded so none is re-attempted.
  The honest next step is probably NOT another instrument on this fixture. Either
    (a) re-derive what sum(Y) closure an ITERATE can be held to -- the repo already distinguishes
        this from G2(a)'s step-boundary bound and says an iterate's "closure is the linear
        solve's" (ProjectRunner.hpp) -- and gate on THAT, or
    (b) move C3 to the species RHS assembly, where the measurement says it belongs.
  Both are register-structure and threshold decisions, which belong to the user, not to a further
  measurement.
```
