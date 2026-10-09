# P18 — Known limitations

Every entry is a **demonstrated** limitation with the evidence that demonstrates it. Nothing
speculative is listed. Sections 1 and 2 separate what P18 is responsible for from what it inherited
(§49), because conflating the two would make P18 look answerable for other phases' debt.

---

## 1. P18's own limitations

### 1.1 G8 is NOT closed — no canonical reacting validation

G8 requires a **1D laminar premixed flame** with flame speed inside a **cited published spread**,
plus peak temperature and species profiles. None of it exists.

Closing it needs three things P18 never built: a reacting-flow solver, a stiff integrator, and
flame-speed extraction. P18.7's audit recorded this before the phase began; P18.9 did not build them
because that is new feature development, which its authorization excludes.

**Evidence:** `results/p18/gates/P18_GATES.md` G8 · `results/p18/conservation_validation/audit.md` §1

**Consequence:** P18 is **PARTIAL**, not COMPLETE. This is the single blocking gate.

### 1.2 No stiff chemistry integrator

The reactor march is **explicit and first order**. There is no implicit or sub-stepped integrator, so
every reacting case must stay inside the explicit stability envelope, and the suites assert that they
do rather than relying on it.

The march carries a **measured** step budget: the `ΣY = 1` tolerance is exceeded after roughly 72
steps for 3 species and **sooner for fewer species**, because P18.1's bound is `4·eps·N_species` and
therefore tighter when `N_species` is small. The error grows linearly at ≈ 0.17 eps/step.

**Evidence:** `results/p18/heat_release/` · `TODO.md` technical-debt entries from P18.6

### 1.3 Elemental conservation is NOT MODELED

No elemental metadata exists. What P18 conserves is **mass** through `Σ_i M_i ν_i = 0`, which is a
property of the stoichiometry **as written**: a mechanism whose stoichiometry did not conserve
elements would still satisfy every P18 conservation criterion.

P18.9 did **not** implement an elemental subsystem to convert this N/A into a PASS (§19 forbids
exactly that).

**Evidence:** `results/p18/cpu_gpu_equivalence/conservation.md` · P18.7's summary

### 1.4 Library-level only — no case file can reach any of P18.2–P18.8

This is the widest limitation and it spans **seven phases**. Measured: **0 files outside `species/`
reference the chemistry**, and **0 case keys define a mechanism**. There is no
`case format → parser → builder → solver dispatch → results/export → CLI → GUI` path to any of it.

Under CLAUDE.md §9 that means species transport, mixture thermodynamics, reaction sources, Arrhenius
kinetics, heat release, conservation and the GPU chemistry are **not production capability**. They are
a verified library.

**Evidence:** P18.2–P18.8 summaries · `ROADMAP.md`

### 1.5 Bitwise CPU/GPU equality is unreachable for transcendental-dependent quantities

Not a defect — a property of the toolchain. `species_detail::expR / logR / powR` dispatch to **glibc**
under gcc and **CUDA libdevice** under nvcc. IEEE-754 requires correct rounding only for `+ − × ÷`
and **`sqrt`**, so two conforming libms may differ in the last bits of `exp`, `log` and `pow`.
P17.7/P17.10 reached bitwise because their shared header calls **only `sqrt`**.

Measured: `k`, `q`, `ω_i` and `S_E` differ by **≤ 3 ULP**; everything else is bitwise, including the
marched state. Removing the difference would mean replacing the host libm, i.e. changing the CPU
reference. Hence amendment **A2**.

**Evidence:** `results/p18/cpu_gpu_equivalence/libm_divergence.md` · `acceptance_gate_A2.md`

### 1.6 The device thermochemistry carries constant `cp` only

`DeviceSpecies` holds a single `Real specificHeat` per species. The CPU `MixtureThermodynamics`
supports **ranged / polynomial `cp(T)`** — P18.6 fixed a real defect in exactly that machinery.

**So P18's CPU/GPU equivalence is established for constant-`cp` mixtures and says nothing about the
ranged-`cp` path.** It also makes G5's "× 5 temperatures" clause inert on the device.

**Evidence:** `results/p18/cpu_gpu_equivalence/kernel_inventory.md` §4 ·
`acceptance_gate_A2_erratum.md` §3

### 1.7 Device capacity and reaction-order limits

* `kMaxDeviceSpecies = 8`, `kMaxDeviceReactions = 4`. A larger mechanism cannot be uploaded; the
  suite asserts the test mechanisms fit.
* All qualified reaction orders are **integer 1**, for which `pow(x,1)` is exact. Fractional orders
  would route through `pow` and acquire the same libm difference — bounded by A2, but **not measured**.

**Evidence:** `kernel_inventory.md` §4

### 1.8 Species transport is not implemented on the device

The GPU has the **thermochemistry only**: mixture properties, EOS, kinetics, sources, heat release,
energy and one reactor step. Convection and diffusion of `Y_i`, and the `ΣJ_i` correction, are CPU
only. This is why P18.8's control **N3** (`ΣJ_i` omitted) is marked **N/A** in the frozen gate itself
rather than left unimplemented, and why §32's "GPU omitted diffusion closure" control has nothing to
cover.

**Evidence:** `results/p18/cpu_gpu_equivalence/audit.md` §5 · `kernel_inventory.md` §3

### 1.9 No performance claim of any kind

P18 makes **none**. P18.8's E12 reports transfer **counts**, not timings. No speed-up, throughput,
crossover or GPU-versus-CPU comparison is claimed anywhere, and CLAUDE.md §4 would require
end-to-end measurement for one. There is also no reacting solve to measure, per 1.1 and 1.4.

### 1.10 No reacting-shock, detonation or supersonic combustion qualification

P18 adds chemistry inside P17's qualified **subsonic** envelope. The canonical reacting case is a
**steady 1D adiabatic plug flow** — deliberately not a flame, shock or detonation.

**Evidence:** P18.7 summary §"Canonical reacting flow"

### 1.11 No 3D reacting-flow qualification

Every P18 reacting case is 0D (closed reactor) or 1D (plug flow). The 3D foundation exists but no
reacting case exercises it.

### 1.12 Environment: WSL2 only

Every P18 number was produced on WSL2 Ubuntu 22.04 with nvcc 12.9.86, g++ 11.4.0, glibc 2.35, an
RTX 5000 Ada (cc 8.9, driver 580.97). **WSL2 + CUDA evidence does not establish native Windows +
CUDA** (CLAUDE.md §4). The libm finding in 1.5 is specific to this glibc/libdevice pair.

---

## 2. Inherited — NOT P18's

### 2.1 P17 — G6 PARTIAL

HLLC star-density positivity fails for admissible head-on input. Closing it needs a wave-speed
estimator change with its own frozen gate. **P18 did not reopen it and P18 qualification does not
relabel P17 complete.**

**Evidence:** `results/p17/KNOWN_LIMITATIONS.md` · `ROADMAP.md`

### 2.2 P17 — G10 PARTIAL, and the Debug timeout contract is undecided

Debug+GUI has **4 inherited `DriverSeegmiller*` timeouts** at the frozen 7200 s cap. All four pass in
**both** Release configurations (measured 1012–2011 s), so they are a Debug-build timing matter, not
a correctness one.

`P18_GATES.md` G11 states these explicitly as **"INHERITED, NOT P18's"**. P18 reproduced them as
baseline behaviour and **did not raise the timeout** to clear them.

`DriverSeegmiller.MeshSequence` sits closest to the cap and now has **8 recorded measurements in
Debug: 5 pass / 3 timeout, every one within 6 % of the cap** (7123.03 P, 7201.94 T, 6764.75 P,
7034.57 P, 7200.12 T, 7168.06 P, 6963.18 P, 7200.08 T). `ROADMAP.md` records that closing G10 needs a
decision about the Debug timeout contract, which is outside a qualification phase's remit.

### 2.3 P16 — deferred SST near-wall work

Advanced SST near-wall and separated-flow validation is deferred to **P20**. P18 did not reopen it.
The frozen exclusion list carries 4 SST/wall patterns for this reason.

### 2.4 Formatting and whitespace debt

`git diff --check` reports **328 warnings**, measured per file:

| file | warnings | attribution |
| --- | --- | --- |
| `cuda/CMakeLists.txt` | 206 | CRLF file **already modified** before P18.8; P18 added ~50 lines |
| `tests/unit/gpu/CMakeLists.txt` | 90 | same; P18 added ~15 lines |
| `tests/unit/compressible/CMakeLists.txt` | 32 | **not P18's** — P17 |

All three are CRLF files under `.gitattributes` `* -text`, where **every** added line trips the check.
P18's contribution is the lines it added to the first two; it did not introduce the condition.
`clang-format-18` reports **0 violations** across all P18 sources.

**This remains a push blocker** and is recorded as such in `TODO.md`.

### 2.5 The dirty working tree

126 tracked modifications and 168 untracked paths, spanning the uncommitted **P13–P18** series.
Nothing in P18 is committed. P18.9 did not clean, reset or stash any of it (§50).

---

## 3. Limitations that were considered and are NOT claimed

Listed so the absence is deliberate rather than an omission:

* **"GPU small-case slowdown"** — not claimed, because **no timing was measured at all** (1.9). A
  slowdown is as unevidenced as a speed-up.
* **"Limited reaction mechanism model"** — the capacity limits are real and are stated precisely in
  1.7. A vaguer claim would add nothing.
* **"CPU/GPU host-control boundaries"** — P18.8 measured the residency boundary exactly (0 transfers
  in the resident loop, with the bookends non-zero), so this is a measurement, not a limitation.
