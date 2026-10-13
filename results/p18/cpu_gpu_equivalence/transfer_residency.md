<!-- P18.8 evidence. Environment for every number in this file: WSL2 Ubuntu 22.04,
     nvcc 12.9.86 (/usr/local/cuda-12.9/bin/nvcc -- NOT the 11.5 first on PATH),
     g++ 11.4.0, glibc 2.35, RTX 5000 Ada cc 8.9 driver 580.97. --use_fast_math: 0.
     WSL2 evidence does not establish native Windows CUDA. -->

# P18.8 — Transfer and residency (E12)

**0 host↔device calls and 0 bytes inside the resident loop, measured rather than inspected.**

## The measurement

4 096 cells, 3 species, 2 reactions, 10 steps. `GPUExecutionStats` counters are reset around each
phase, so the three are separated as the frozen criterion requires.

| Phase | H2D calls | H2D bytes | D2H calls | D2H bytes |
| --- | --- | --- | --- | --- |
| initialization (upload once) | **> 0**, asserted | > 0 | 0 | 0 |
| **the 10-step resident loop** | **0** | **0** | **0** | **0** |
| final output (download once) | 0 | 0 | **> 0**, asserted | > 0 |

Also asserted: `largestDeviceToHostBytes` inside the loop is **strictly less than one field** —
which is the quantity that actually distinguishes "many small scalars" from "one field", and the one
P16 learned to use after two attempts at a total-bytes bound failed.

The bookends are asserted **non-zero**. Without that, a run that uploaded nothing and downloaded
nothing — because it did nothing — would satisfy the residency claim.

## How the loop avoids a transfer

The launchers take caller-owned `DeviceBuffer`s and **never allocate, never upload and never
download**, so a per-step full-field transfer cannot hide inside one. The step's output becomes the
next step's input by swapping the buffers — a device-to-device hand-off with no host round trip.

And the loop did real work: the final temperature differs from the initial one, asserted.

## Control N12 — the instrument can see the defect it excludes

A clean transfer count is worthless if the counters cannot register a transfer. So the control
injects a full-field round trip and checks the instrument notices:

| | `largestDeviceToHostBytes` | `deviceToHostCalls` |
| --- | --- | --- |
| clean (no transfer) | 0 | 0 |
| **after an injected field-sized round trip** | **≥ one field** | **> 0** |

and it asserts explicitly that the comparison E12 relies on **would fail** under the injected
defect. That is what makes E12's clean result meaningful.

## No performance claim

The authorization is explicit: no performance claim without equivalent numerical solves. **P18.8
makes none.** These are transfer counts, not timings; no speed-up, throughput or crossover is
claimed anywhere in this phase. A kernel-only comparison would not qualify as performance evidence
under CLAUDE.md §4 in any case, and there is no end-to-end reacting solve to measure because none
exists (`kernel_inventory.md` §3).
