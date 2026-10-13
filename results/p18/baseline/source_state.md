# P18.0 — starting source state (§1, §4)

```text
P18_START_SOURCE_FINGERPRINT = 60cf951bde2a4eb2fc32d21470377090fa3453c6b5b435c130b2247164f89318

  IDENTICAL to P17's final qualification fingerprint. Nothing in the code changed between P17.11
  closing and P18.0 opening, which is the cleanest possible starting condition: the tree P18 audits
  is exactly the tree P17 qualified.

  components  tracked-diff  b4500002367b115c3b321114b598ffa98bd1965d089f9aa1c7fc8d4527e8831e
              untracked-set ff02adfe311c6832573842cda6d35175f724046d0395cbb1aa5b1c0988f8cdcf
  methodology P13/P17's, unchanged -- sha256 of the tracked diff against HEAD plus every untracked
              file, both restricted to code. results/ and the top-level control docs are excluded by
              construction, so writing audit evidence cannot move the fingerprint of the code being
              audited.
```

## Repository

```text
HEAD                    3bd32f918656f471e72b5f6ac7cb6c2cbf75a274
branch                  main
commits ahead of origin 0
submodules              0
working tree            DIRTY -- 266 entries
                        (265 at P17.11 close, +1 for the new results/p18/ directory)

  tracked modifications, code only    126
  untracked files, code only          150
  by status letter                    129 M   ·   137 ??

NOTHING WAS COMMITTED, PUSHED, RESET, CLEANED OR STASHED. The dirty tree is preserved as a fact, as
§1 requires, not tidied to make the baseline look cleaner than it is. P17.11 classified all 265
entries into 14 buckets with 0 unexplained (results/p17/qualification/logs/96_tree_integrity.log);
P18.0 adds only evidence under results/p18/.
```

## Toolchain actually used

```text
  g++ / gcc     11.4.0  (Ubuntu 11.4.0-1ubuntu1~22.04.3)
  clang         14.0.0
  cmake         3.22.1
  ninja         1.10.1
  python3       3.10.12
  nvcc          /usr/local/cuda-12.9/bin/nvcc -- release 12.9, V12.9.86
  sanitizer     compute-sanitizer 2025.2.1.0
  kernel        Linux 6.18.33.2-microsoft-standard-WSL2 x86_64
  distro        Ubuntu 22.04.5 LTS (WSL2)

  RECORDED BECAUSE IT IS AMBIGUOUS, and P17.11 was caught by it once: a BARE login shell resolves
  nvcc to the apt package (11.5) and compute-sanitizer to 2021.3.1. Every harness exports
  /usr/local/cuda-12.9/bin first, and both CUDA build trees record
  CMAKE_CUDA_COMPILER=/usr/local/cuda-12.9/bin/nvcc. The versions above are the ones that build
  what is qualified.
```

## GPU

```text
  NVIDIA RTX 5000 Ada Generation Laptop GPU
  driver 580.97 · 15352 MiB · compute capability 8.9
```

## Build configurations

```text
  qual-final     BUILD_TYPE=Release  GUI=OFF  CUDA=ON   OPENMP=ON
  qual-cpuonly   BUILD_TYPE=Release  GUI=OFF  CUDA=OFF  OPENMP=ON
  qual-debug     BUILD_TYPE=Debug    GUI=ON   CUDA=ON   OPENMP=OFF

  THESE TREES MUST NOT BE DELETED. Their configuration exists only as CMake cache state -- it is in
  no preset and, before P17.11, in no harness. Deleting one silently reconfigures it to the project
  defaults (Debug, OpenMP OFF), which cost P17.10 two regression re-runs and presented as three test
  failures. The P18 baseline harness inherits P17.11's fix: it ASSERTS all four options after
  configuring and fails closed on a mismatch.
  results/p17/cpu_gpu_equivalence/HARNESS_FINDINGS.md section 2
```

## Inherited P17 status, preserved unchanged (§2)

```text
  P17 OVERALL: PARTIAL -- and P18.0 neither reopened nor repaired it.

  G6  PARTIAL   HLLC star-density positivity fails for admissible head-on input above a
                wall-normal Mach of ~1.76. NOT touched.
  G10 PARTIAL   4 pre-P17 DriverSeegmiller tests time out under Debug+GUI at the frozen 7200 s.
                NOT touched -- and the P18 baseline REPRODUCES them by design, as inherited
                baseline behaviour rather than a P18 regression.

  All four inherited P17 gate documents verified unmoved at P18 gate-freeze time:
    P17_GATES.md                          3809c100...
    density_based_solver/acceptance_gate  a88738e4...
    shock_validation/acceptance_gate      b7185cea...
    cpu_gpu_equivalence/acceptance_gate   c5b86211...
```
