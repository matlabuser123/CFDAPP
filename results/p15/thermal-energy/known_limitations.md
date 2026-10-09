# P15 — known limitations

Written for: a reviewer deciding whether these are acceptable to ship, and the
engineer who picks up whichever one bites first.

Everything here is a real gap, stated plainly. None of it is hidden behind a
passing test.

---

## 1. `metadata.json` reports a variable property as a single number

`ThermalRunMetadata` carries `conductivity` and `specificHeat` as scalars, and
`ProjectRunner` fills them from `setup.thermal`, which for a model holds its
value **at the initial temperature**.

So a run that correctly used `k(T) = −8.4 + 0.03T` exports
`"conductivity": 0.6`. That is not wrong — 0.6 is genuinely `k(300)` — but it is
incomplete: a user reading `metadata.json` cannot tell the run used a model, nor
what the model was, and two runs differing only in whether the property varied
produce the same metadata line.

**Why it was not fixed in P15:** it was found while the final regression was
already running, and this project's standing rule is that production sources do
not change underneath an active regression. Fixing it afterwards would have
invalidated that regression and required a full re-run for a reporting field.

**The fix, when someone takes it:** add an optional model description to
`ThermalRunMetadata` (coefficients and validity range), populate it from
`setup.conductivityModel` / `setup.specificHeatModel`, and keep the existing
scalar so nothing reading `metadata.json` today breaks. `test_json_writer.cpp`
is where the schema is pinned.

**Severity:** reporting only. The solve itself is correct and is proven correct
by `VariablePropertyProductionCase.MatchesTheClosedFormForVariableConductivity`.

---

## 2. Robin boundaries force a case onto the CPU

A mesh carrying a `ConvectiveHeatTransfer` patch is classified GPU-unsupported
and runs on the CPU. The reason is structural, not effort — see
`cpu_gpu_equivalence.md` and `robin_architecture_decision.md`. Option C,
adopted and closed by the user.

The refusal is graceful and names its cause. It is not silent, and it does not
approximate.

---

## 3. No full thermal CUDA solver

Explicitly out of scope by the user's instruction. The device path handles the
terms it already handled; nothing thermal-specific was added to it.

---

## 4. Property positivity is checked by sampling

`TemperatureDependentProperty` samples 1024 points across the validity range.
A polynomial with a dip narrower than the sample spacing could pass
construction and then produce a non-positive value at solve time.

That residual case is **caught, not missed** — `evaluatePropertyField` checks
positivity per cell and the failure surfaces as
`ThermalStatus::NonFiniteState` naming the property. Sampling is a
construction-time convenience, not the only guard.

Root-finding over an arbitrary-degree polynomial would close it exactly. It was
judged not worth the machinery for property curves of the degree materials data
actually produces.

---

## 5. Only the `polynomial` model is reachable from a case file

`ConstantProperty`, `LinearProperty` and `TabulatedProperty` exist in
`cfd::physics` and have since P3-PHYS-003, but `physics.json` only accepts
`"model": "polynomial"`.

This is a small gap by construction: the case format already dispatches on
`"model"`, so adding the others needs a parser branch and nothing else — no new
convention, no new abstraction. A constant is also already expressible as a
one-coefficient polynomial, or as the plain-number form.

An unknown model name is **rejected**, not defaulted, so a case asking for
`"table"` today fails loudly rather than silently solving with a constant.

---

## 6. The transient term exists in two places

`cfd::transport::applyTransientContribution` and
`cfd::thermal::assembleThermalTransientContribution` implement the same
conservative implicit-Euler form. Same convention, two implementations. The
merge is blocked by the Robin boundary and becomes possible when Option B lands
— rationale in `p14_classification.md`.

---

## 7. Deferred second-order Dirichlet reconstruction (P12-DIFF-002)

Pre-existing, not introduced by P15, but it shapes how P15's tests are written:
the assembled system depends on the current iterate, so a single solve does not
give the converged answer. Every validation case iterates to convergence
(`solveToConvergence`) rather than solving once. A test that solved once would
measure the wrong thing — an early P14 version did exactly that and reported a
max error of 231 on a field of range 400.
