# P17.1 — constant-density preservation

Gate group **C**. The question: did adding a variable-density precondition change
anything about the constant-density and incompressible paths? Measured answer: **no, and
one of the four criteria is structural rather than numerical, so it cannot drift.**

---

## C1 — compressible at uniform density equals incompressible, BITWISE

```text
measured on the UNCHANGED tree (dry-run, logs/00_dryrun.log group F):
  faces where compressible != incompressible    0    of 7      BITWISE
measured after the change:
  UniformDensityMatchesIncompressibleMassFluxExactly            PASS
  ValidUniformDensityIsUnaffectedByTheNewValidation  (new)      PASS
    asserts EXPECT_EQ(flux[f], 2.0) on the internal face -- the exact value the
    pre-change dry-run recorded, so the reference is a measurement, not a memory
```

The dry-run comparison was bitwise (`!=` on doubles, 0 differing faces). The existing
in-suite test uses `EXPECT_NEAR(..., 1e-9)`; the new test asserts exact equality against
the dry-run value, which is tighter.

## C2 — incompressible SIMPLE remains EOS-free, structurally

```text
references to EquationOfState / IdealGasEOS / ThermodynamicProperties under
  src/pressure_velocity/  and  include/cfd/pressure_velocity/

  before P17.1     0
  after  P17.1     0
```

This is the criterion worth dwelling on. It is **not** a numerical check that could drift
— it is a statement about the dependency graph, verifiable by grep. Incompressible SIMPLE
cannot silently become EOS-dependent, because it does not name the EOS at all. The
authorization's concern that "P17.1 must not accidentally cause incompressible SIMPLE to
become EOS dependent" is therefore answered by construction rather than by testing, which
is a stronger guarantee than any tolerance.

Re-measured at the end of the run: still `0` (`logs/08_fingerprint.log`).

## C3 / G7 — the P13-001 SIMPLE fingerprint is BITWISE IDENTICAL

The sharpest instrument the project owns, and the one P17.1 most had to leave alone.

```text
probe        results/p13/restart/tools/solve_fingerprint.cpp   (unmodified)
reference    results/p13/restart/logs/fingerprint-after-histories.log
invocation   OMP_NUM_THREADS=1 /tmp/p17_1_fp
cases        10
lines        130
diff         EMPTY -- 0 bytes, preserved as logs/08_fp_diff.txt
verdict      BITWISE IDENTICAL -- P17.1 moved no bit of the incompressible solver
```

An empty diff file is preserved deliberately: the absence of a difference is the
evidence, and deleting the file because it is empty would discard it.

### A deliberate improvement over the inherited invocation

P17.2's own G7 ran this probe **without** `OMP_NUM_THREADS=1`. For a bitwise-identity
claim that is the wrong instrument, because OpenMP reduction order can depend on thread
count — a run that happens to match at 25 threads has not established thread-independent
identity. P17.1 sets the prefix, matching the frozen execution policy used everywhere
else, which makes the measurement thread-count independent.

It also avoids a trap that cost real time here: run unprefixed alongside a `ctest -j 8`,
the probe took 25 cores and had not finished after 12m42s, because oversubscribed OpenMP
threads spin rather than progress. Prefixed and uncontended it completed in **12 seconds**.

## C4 — a constant density stays constant

```text
conservation probe (logs/07_conservation.log), case (b) uniform rho:
  max cell imbalance        0
  total |imbalance|         0
  global net boundary flux  0
  mdot_in / mdot_out        -8 / +8, sum exactly 0
```

And the new validation cannot perturb a valid constant field: it either throws or does
nothing at all — there is no code path that modifies `density`. That is checkable from the
change itself (a `for` loop containing only a `throw`), not merely from test outcomes.

## Preservation suites, at the frozen narrow regexes

```text
P14 transport     68 / 68        Transport|LegacyMatrix|ScalarTransport
P15 thermal      161 / 161       EnergyEquation|Enthalpy|ThermalPropert|…
P16 turbulence   315 / 315       Turbulence|EffectiveViscosity|WallFunction|…
compressible     137 / 137       Compressible|IdealGas|Thermodynamic|LowMach|MMSCompressible
full regression 2505 / 2505      Release+CUDA
                2305 / 2305      CPU-only
```

Every number is the P17.0 baseline plus exactly the eight new tests where the regex
includes them, and unchanged where it does not. The P14/P15/P16 figures are identical to
the baseline because the frozen regexes do not match any compressible test — which is
itself the point of using narrow, pre-registered regexes rather than a keyword sweep.

## Summary

```text
id   criterion                                        verdict
--------------------------------------------------------------------------------
C1   uniform-rho compressible == incompressible        PASS, BITWISE (0 of 7 faces)
C2   incompressible path structurally EOS-free         PASS, 0 references, before
                                                       and after
C3   P13-001 SIMPLE fingerprint unchanged              PASS, BITWISE, empty diff
C4   a constant density stays constant                 PASS, imbalance exactly 0
```
