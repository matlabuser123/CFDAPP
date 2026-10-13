# P17.10 — equivalence tolerance policy

**Written and frozen BEFORE any CUDA source for the density-based path existed**, so no threshold
here can have been fitted to an observed CPU/GPU difference. Authorization §31.

The policy is short because its answer is mostly the same word.

---

## 0. The default is BITWISE, and that is a demand rather than a hope

The frozen `P17_GATES.md` G8 says it first:

> *"TOLERANCE REQUIRES JUSTIFICATION — derive from the actual reduction-order difference between the
> CPU and GPU implementations. Where the operation order is identical, BITWISE is the correct
> requirement and anything looser is a concession that must be argued for, not assumed."*

For this solver the operation order **can** be made identical, and the audit establishes why:

```text
1  ONE SOURCE FOR THE MATHEMATICS. hllcFlux(), eulerFlux(), toConserved(),
   reconstructFirstOrder() and the wave-speed estimator are all marked CFD_HLLC_HOST_DEVICE in
   include/cfd/compressible/HllcFlux.hpp, and gcc and nvcc compile that same text. There is no
   second formulation to drift.

2  THE GATHER ORDER EQUALS THE CPU'S ACCUMULATION ORDER. Face ids are assigned from a monotonically
   increasing counter and appended as faces are created, so every cell's face list is ASCENDING.
   The CPU loops faces in ascending id order accumulating into owner and neighbour; a per-cell
   gather over cellFaceIds therefore adds the same terms in the same sequence. (audit.md §4)

3  THE GEOMETRY IS UPLOADED, NOT RECOMPUTED. cuda/kernels/DeviceMeshCuda.cpp:72 stores
   `area[f] = face.area()` -- the host's own value. Area vectors likewise. So a device normal
   areaVector/area is bitwise equal to the CPU's.

4  FMA CONTRACTION IS ALREADY DISABLED. nvcc fuses a*b+c by default and the x86-64 host build does
   not; -fmad=false has been the project convention since before P17.7 and is applied to the new
   kernels for the same reason, NOT to force this phase's agreement. P17.7 measured the cost:
   17/17 bitwise with it, 8/17 without. (authorization §30)

5  THE GLOBAL dt IS A MIN, NOT A SUM. min is associative and commutative in IEEE-754 for non-NaN
   operands, so a parallel min reduction gives the same answer as a sequential one regardless of
   tree shape. dt can therefore be bitwise even though it is reduced in parallel.
```

## 1. The policy, per equivalence level

| Level | What is compared | Required | Why that requirement |
| --- | --- | --- | --- |
| **A** arithmetic | EOS, state conversion, sound speed, HLLC flux, wave speeds, star states, branch | **BITWISE** | one shared source; `-fmad=false`; no reordering |
| **A** arithmetic | device recovery vs `TotalEnergyState::recover()` | **BITWISE** | two closed-form lines, same operand order (audit.md §5) |
| **B** local operator | face flux, per-cell residual, boundary exterior states | **BITWISE** | reasons 1–4 above |
| **C** one step | `Q` after one update from identical `Q0` | **BITWISE** | `Q − (dt/V)R` with identical `Q`, `dt`, `R` and identical order |
| **C** control | local `dt`, global `dt` | **BITWISE** | reason 5 |
| **D** short trajectory | `Q` after 1, 2, 5, 10, … steps | **BITWISE** | bitwise at step 1 composes; any drift means a real defect |
| **E** full benchmark | final fields, wave positions, conservation | **BITWISE** | same |
| — | residual norms, conservation sums | **BITWISE, by construction** | computed on the HOST from the state, by the same code, so they agree exactly whenever the state does (audit.md §6) |
| — | positivity / non-finite / retry / rejection decisions | **EXACTLY EQUAL counts and identical first-offending cell** | these are control-flow decisions, not floating-point results; "close" is meaningless for an integer count |

## 2. There is no fallback tolerance, deliberately

```text
IF A LEVEL IS NOT BITWISE, THAT IS A FINDING, NOT A NUMBER TO BE CHOSEN.

This policy does not carry a secondary "…or within 1e-14" clause, because such a clause is exactly
how a real reordering defect gets absorbed. If a comparison fails, the response is:
  1  preserve the failure verbatim;
  2  identify WHICH of the five reasons above has been violated -- they are individually checkable;
  3  fix the GPU implementation, not the threshold;
  4  and only if the violation turns out to be irreducible (for example a reduction that genuinely
     cannot be ordered), derive a tolerance from THAT specific mechanism, record it as an
     amendment with its derivation, and re-run.
Authorization §2: "If GPU differs, diagnose the GPU implementation first."
```

## 3. What a bitwise result would and would not prove

```text
WOULD PROVE   the two paths execute the same arithmetic, in the same order, on the same data --
              which is the strongest available form of "the same governing equations", and is
              exactly what G8 asks for.

WOULD NOT PROVE   that either is CORRECT. Correctness is P17.7's (flux), P17.8's (solver) and
              P17.9's (shock validation) job, and all three are already evidenced. A bitwise-equal
              pair of wrong implementations would still be wrong, which is why the negative controls
              of §29 are run on BOTH backends rather than on the CPU alone.

WOULD NOT PROVE   anything about performance. §32 forbids a speed claim without equivalence, and
              this phase makes none even with it.
```

## 4. Inherited thresholds that are NOT renegotiated here

```text
P17.9's shock-validation bounds        both backends must independently stay inside them
P17.8's conservation floors            8 eps N_cells, unchanged
P17.7's flux equivalence               already BITWISE, 17/17; re-run, not re-derived
P17.6's low-Mach thresholds            unchanged; the GPU low-Mach case must not move them
```

## 5. Determinism, stated separately for each backend

```text
CPU repeats   BITWISE. P17.8's D1 already requires it and it holds.
GPU repeats   BITWISE REQUIRED, and achievable because the residual gather uses NO ATOMICS and the
              only reduction is a min. If a future parallel SUM reduction were added, this line
              would have to change -- and §21 says such a change must be documented and quantified,
              never quietly absorbed. It is not needed for this kernel set.
```
