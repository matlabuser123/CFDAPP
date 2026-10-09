# P17.7 — CPU/GPU equivalence and determinism

Covers §27–§30 of the authorization and gate criteria G1–G4. Sources: `logs/04_gpu_attempt1.log`,
`logs/05_fma_experiment.log`.

---

## 0. The scope conflict, named rather than silently resolved

```text
P17_GATES.md G8, as of P17.0:  "GPU compressible work is outside the authorized P17 scope",
                               and G8 is marked VACUOUS / DEFERRED.
P17.7's authorization:         sections 27, 28, 29 and 42 REQUIRE a GPU implementation and
                               CPU/GPU equivalence.
```

The authorization is newer and more specific, so it governs this item, and G8 is no longer vacuous for
the flux layer. This is flagged here and in `audit.md` §5 rather than quietly overridden — a frozen
gate document should not be contradicted without saying so.

```text
WHAT THIS DOES AND DOES NOT SETTLE. G8's own text asks for "equivalent numerical SOLVES, not kernel
microbenchmarks", and lists density, momentum, pressure, residual histories, conservation and
iteration control. P17.7 provides none of that, because there IS no GPU compressible solve -- there is
a flux function. So G8 is satisfied FOR THE HLLC FLUX and remains open for the solver. P17.10 is not
claimed. The gate matrix entry should read "G8: flux layer PASS, solver DEFERRED" rather than PASS.
```

## 1. §27/§28 — the architecture: one source, not two

The project's convention for GPU work is a `.cu` that re-expresses the CPU algorithm; there are
twenty-odd such kernels. **P17.7 does not follow it**, because §28 says to avoid copy-pasted
mathematical logic *"if a common host/device-compatible implementation is practical"*, and for a
pointwise flux with no mesh and no solver state it is not merely practical but obvious.

```text
include/cfd/compressible/HllcFlux.hpp    ALL of the numerics, marked CFD_HLLC_HOST_DEVICE
                                         compiled by gcc for the host AND by nvcc for the device
cuda/kernels/DeviceHllcFluxKernel.cu     marshalling only

  #if defined(__CUDACC__)
  #define CFD_HLLC_HOST_DEVICE __host__ __device__
  #else
  #define CFD_HLLC_HOST_DEVICE
  #endif
```

Criterion G1, verified structurally:

```text
wave-speed / star-state / shock-factor formulas in the .cu   0
calls INTO the shared header from the .cu                    2  (hllcFlux, hllFluxForControlOnly)
```

### The three constraints that shaped the interface

```text
1  NO EXCEPTIONS in the shared core. Device code cannot throw, so validation returns a typed
   StateValidity and the host-only hllcFluxChecked() translates it into InvalidArgumentError. Failure
   semantics are therefore explicit on both sides rather than different on each.
2  NO cfd::Vector3 OPERATORS. Vector3's operators, dot, cross, magnitude and isFinite are plain
   `inline`, NOT __host__ __device__, so device code cannot call them. Vector3 is a P12 core type used
   by the whole codebase and P17.7 does NOT modify it -- verified UNTOUCHED by SHA-256. The header
   holds Vector3 (a POD aggregate, which device code may construct, copy and read) and does its
   arithmetic on .x/.y/.z, with dotHD() reproducing cfd::dot BIT FOR BIT, including its deliberate
   treatment of an exactly-zero z product.
3  NO THERMODYNAMICS. ThermodynamicProperties and TotalEnergyState are host-only classes in .cpp
   files, so the device could not call them even if the design wanted it to. PrimitiveState arrives
   carrying p, T, a and rhoE -- produced on the host by the authoritative path -- and the flux only
   consumes them. The device-compatibility constraint and section 3's "no second ad-hoc EOS" rule
   happen to demand the same architecture.
```

## 2. §29 — the comparison, and why it covers the intermediates

§29: *"Do not merely compare final flux if internal differences could choose different branches."*
So every intermediate the algorithm exposes is compared, with `EXPECT_EQ`:

```text
S_L, S*, S_R, the contact denominator, rho*_L, rho*_R, e*_L, e*_R,
the selected branch, the validity code, and all 5 flux components
```

### The state matrix

Every row §29 names, plus two the reference table cannot supply:

```text
equal states                uniform_x, uniform_oblique
subsonic                    sod_atmospheric, low_pressure, moving_contact, ...
supersonic left / right     supersonic_left (F_R), supersonic_right (F_L)
stationary contact          stationary_contact
moving contact              moving_contact
Sod initial discontinuity   sod_literature, sod_atmospheric
strong pressure ratio       strong_pressure_ratio  (1e5)
strong density ratio        strong_density_ratio   (1e6)
oblique normal              uniform_oblique, (2,-3,6)/7
near-sonic                  near_sonic_left
+ constructed_right_star    the F*_R branch, which appears in NO reference case, so without this row
                            that branch would go uncompared on device
+ sod_davis_estimator       the Davis estimator on device, so the control path is not CPU-only
                            17 rows total
```

## 3. THE RESULT — bitwise, with zero difference

```text
case                       branch     max|dF|      max|dS|
uniform_x                  1          0.000e+00    0.000e+00
uniform_oblique            1          0.000e+00    0.000e+00
sod_literature             1          0.000e+00    0.000e+00
sod_atmospheric            1          0.000e+00    0.000e+00
sod_y                      1          0.000e+00    0.000e+00
sod_z                      1          0.000e+00    0.000e+00
stationary_contact         1          0.000e+00    0.000e+00
moving_contact             1          0.000e+00    0.000e+00
tangential_shear           1          0.000e+00    0.000e+00
supersonic_right           0          0.000e+00    0.000e+00
supersonic_left            3          0.000e+00    0.000e+00
strong_pressure_ratio      1          0.000e+00    0.000e+00
strong_density_ratio       1          0.000e+00    0.000e+00
near_sonic_left            0          0.000e+00    0.000e+00
low_pressure               1          0.000e+00    0.000e+00
constructed_right_star     2          0.000e+00    0.000e+00
sod_davis_estimator        1          0.000e+00    0.000e+00

rows compared               : 17
rows BITWISE identical      : 17
branch-selection differences : 0
max |flux difference|       : 0
max |wave-speed difference| : 0
```

Branch codes `0/1/2/3` = `F_L / F*_L / F*_R / F_R`, so **all four branches were exercised on device**,
not just the common one.

```text
This is the strongest form G8 admits: "where the operation order is identical, BITWISE is the correct
requirement and anything looser is a concession that must be argued for, not assumed." One shared
source makes the order identical, so no concession is needed and none is made. There is no tolerance
in this comparison at all -- every assertion is EXPECT_EQ.
```

### Refusals match too

```text
G2_InadmissibleInputsAreRefusedIdenticallyOnDevice:
  8 inadmissible inputs (rho = 0, rho < 0, NaN p, negative p, NaN velocity, an area vector,
  a zero normal, gamma = 1)
  device refused all 8, each with the SAME StateValidity code as the host
```

§29 also asks for the *fallback decision* to be compared. There is no fallback in this layer — the
denominator is provably non-degenerate — so the validity code is compared instead, which is the
closest real analogue. Stated rather than silently skipped.

## 4. §30 — determinism

```text
CPU   15 cases evaluated twice in-process:      flux, S_L, S*, S_R, branch, rho*_L  BITWISE identical
GPU   17 rows through TWO separate kernel launches:                                 BITWISE identical
```

§30 specifically asks whether branch selection can vary between repeats. It cannot: the branch is a
pure function of the computed wave speeds, and the layer has no uninitialised state, no reduction and
no atomics. Both tests assert the branch explicitly.

## 5. Criterion G3 — what FMA contraction is worth

The kernel is built with `--fmad=false`. G3 asks for the cost of that choice to be reported, so it was
measured as a controlled experiment (`tools/fma_experiment.sh`): flip the flag, rebuild, measure,
restore, prove the restore.

```text
                       rows BITWISE   max |flux diff|            max |wave-speed diff|
--fmad=false  (prod)   17 of 17       0                          0
--fmad=true            8 of 17        1.52587890625e-04          0
restored               17 of 17       0                          0
```

```text
WHAT THE NUMBERS MEAN. The largest absolute difference, 1.526e-04, is on strong_pressure_ratio's
energy flux, whose magnitude is 8.19e10 -- so it is 1.9e-15 relative, i.e. a few ulp. Contraction is
not "wrong"; it is a different, generally MORE accurate rounding. The point is that it is DIFFERENT,
and a bitwise claim cannot survive it.
TWO DETAILS WORTH RECORDING:
  * the WAVE SPEEDS were unaffected either way (max |dS| = 0 in both settings), so contraction touched
    only the flux and the star internal energy.
  * branch selection NEVER differed, so contraction was never close to changing the physics -- which
    is the reassuring version of this result, and the reason --fmad=false is a cosmetic-precision
    choice rather than a correctness one.
The restore was verified by SHA-256 on cuda/CMakeLists.txt and the bitwise result re-confirmed.
```

### This is the project's established convention, not a new concession

```text
cuda/CMakeLists.txt already applies -fmad=false to the twenty-odd existing kernels, for exactly this
reason. P17.7 uses a SEPARATE set_source_files_properties for its own kernel only so that
fma_experiment.sh can flip one translation unit without disturbing the rest of the library.
```

## 6. What is NOT claimed

```text
NOT CLAIMED   a GPU compressible SOLVER. There is none. This is a flux function that happens to run
              on device; G8's own wording about "equivalent numerical solves" is not satisfied by it,
              and P17.10 is untouched.
NOT CLAIMED   performance. No timing is reported and no speed-up is claimed. The kernel exists to
              prove numerical equivalence, and a 17-element launch would say nothing about throughput
              anyway. G8's "no GPU performance claim without equivalent numerical solves" is
              respected by making no performance claim at all.
NOT CLAIMED   equivalence on a device other than the one measured. The result is from the development
              GPU via CUDA 12.9 with CUDA_ARCHITECTURES 80;89. A different architecture could in
              principle round differently; --fmad=false removes the most likely cause but the claim is
              "measured bitwise here", not "bitwise on every device".
NOT CLAIMED   anything when no GPU is present. All four GPU tests GTEST_SKIP with an explicit message
              saying equivalence is NOT CLAIMED rather than passed -- a skip must never read as a pass.
```
