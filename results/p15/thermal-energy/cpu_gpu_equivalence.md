# P15 — CPU/GPU equivalence and the device thermal scope

Written for: a reviewer checking what P15 claims about the GPU, and an engineer
who later wants to put the thermal path on the device.

---

## Environment

Recorded at the time the GPU gates ran (`logs/final/gpu_health2.log`):

```
GPU     NVIDIA RTX 5000 Ada Generation Laptop GPU
driver  580.97, CUDA capability 13.0
WSL     /dev/dxg present, nvidia-smi working
nvcc    release 12.9, V12.9.86     <- the QUALIFIED toolchain, unchanged
```

**On the 13.0-vs-12.9 difference:** a CUDA 13.0-capable driver runs a 12.9
toolkit's binaries; that is the normal forward-compatibility direction, not a
mismatch. The qualified toolchain was deliberately left at 12.9. No build or
runtime failure was observed that would justify moving it, and moving a
qualified toolchain without such evidence would discard the qualification.

(Note for anyone reading an earlier summary: the distro's default `/usr/bin/nvcc`
is 11.5, but the build does not use it — `PATH` puts `/usr/local/cuda-12.9/bin`
first. The toolchain actually in use is 12.9.)

---

## What P15 changed about GPU behaviour

Nothing, for any configuration that worked before. P15 added a boundary
condition the device cannot represent, so the only GPU-facing work was making
that refusal **safe and honest**.

### The structural reason a Robin wall cannot go on the device

```
G_f = 1 / (d/k_f + 1/h_c)
```

needs the runtime conductivity `k_f`. Three facts make that unavailable where
the plan is built:

1. a device plan is constructed **once**, from mesh and boundaries alone;
2. it is constructed **before** Γ exists;
3. the device encoder represents a boundary condition by probing
   `boundaryValue()`, an interface that is not given `k`.

After P15, `k` can also vary with temperature, so it is not even fixed for the
duration of a solve.

Changing `ScalarBoundaryCondition::boundaryValue` to accept a diffusivity
touches **7 scalar BCs, 5 vector BCs and 51 call sites**. That is Option B in
`robin_architecture_decision.md`, and **Option C — defer, classify the mesh
unsupported — was adopted and closed by the user**.

### A full thermal CUDA solver was explicitly not attempted

The user's instruction was direct: do not implement one unless the existing
architecture already supports it cleanly, and document it as deferred rather
than expanding P15. The architecture does not support it cleanly, for the reason
above. It is deferred, and this file is that documentation.

---

## Acceptance criteria and their results

| # | Criterion | Result |
|---|---|---|
| 1 | Encoder reports Robin unsupported rather than throwing | **PASS** |
| 2 | Refusal independent of `h_c`, including the insulated limit | **PASS** |
| 3 | Plan construction does not throw | **PASS** |
| 4 | Plan classified unsupported | **PASS** |
| 5 | Reason names the condition **and** the runtime-conductivity cause | **PASS** (after a fix — below) |
| 6 | Every previously-supported encoding unchanged | **PASS** |
| 7 | Encodings reproduce their condition **bitwise** | **PASS** |

Criteria 1–2 and 6–7: `tests/unit/gpu` — `BoundaryEncoding.*`, 9/9.
Criteria 3–5: the plan-level probe, `tools/plan_reason.sh`, **PASS, 0 failures**.

Criterion 7 is the one that makes "equivalent" mean something here: the fitted
form must reproduce `boundaryValue()` **bit for bit** over a spread of owner
values, not approximately.

---

## Criterion 5 failed first, and the failure was instructive

The reason both plans reported was:

```
gradient plan unusable: face 4: boundary condition 'ConvectiveHeatTransfer'
is not bitwise-reproducible as a constant, a shift or an affine function of
phi_P; mesh rejected for the device gradient path
```

Two defects, and only the first is what the criterion asked about:

1. it does not name the cause;
2. **it is untrue**. A Robin wall *is* affine in T_P — the encoder could
   represent that form. What it cannot obtain is `k`. A refusal that misstates
   its cause sends a user to fix a boundary condition that is not broken.

The type-specific reason already present in `DeviceDiffusionPlan` and
`DeviceConvectionPlan` was never reached: the **gradient** plan builds first and
its generic message shadowed both. Two plans had been taught the real cause and
a third had not — and the third ran first.

**The fix was consolidation, not a third copy.** `BoundaryEncoding.hpp` now owns
`unsupportedEncodingReason()`, used at all four scalar rejection sites. Its own
header comment already said why: *"Moved, not copied: two copies of this would
drift."* For every condition other than Robin the message is reproduced **byte
for byte**, including each site's own noun (`boundary condition` vs
`scalar condition`) and its own path clause. No test pinned any of these
strings, and the byte-identical reconstruction is what keeps it that way.

The two **vector** sites were deliberately left alone: their wording differs
("the identity or the …"), and `ConvectiveHeatTransfer` is a scalar condition
that cannot reach them.

After the fix:

```
face 4: boundary condition 'ConvectiveHeatTransfer' requires the runtime
thermal conductivity to form its coefficients and is not supported by the
current device gradient plan; the CPU thermal assembly handles it
```

The probe was then **tightened** to fail if the message ever again claims the
condition is not representable. The defect is now a gate rather than a memory.

---

## What "equivalence" means for the paths that do run on the device

Unchanged from GPU-DISC-001. The CPU and device assemblies differ only in
**association order** — the host accumulates into one builder while the device
adds completed totals — which is why source-only systems are 100 % bitwise
identical and assembled ones need `-fmad=false` per kernel to match. P15 added
no kernel and changed no arithmetic, so that analysis carries over untouched.

The final regression's own SIMPLE fingerprint check (P13-001) is the standing
guard that it still holds; see `regression.md`.

---

## If someone picks up device Robin later

The work is Option B, and it is bounded rather than open-ended:

1. give the boundary-value query access to the local diffusivity (7 scalar BCs,
   5 vector BCs, 51 call sites);
2. the encoder can then fit Robin as the **affine** form it already is — no new
   encoding kind is needed;
3. the plan must be rebuilt when Γ changes, which for a temperature-dependent
   `k` means every outer iteration. That cost, not the encoding, is the real
   design question.

Point 3 is why this was deferred rather than merely postponed for time.
