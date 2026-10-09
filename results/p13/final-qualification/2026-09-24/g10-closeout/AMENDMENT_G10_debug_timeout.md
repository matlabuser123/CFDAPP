# AMENDMENT — G10 execution policy: the Debug+GUI timeout

Authorized 2026-09-25. **This is an execution-policy amendment, not a
test-acceptance change.** No numerical criterion is weakened, no exclusion added,
no production or test source modified.

---

## What changes

For the **Debug+GUI configuration only**:

```text
                   before        after
OMP_NUM_THREADS    1             1          unchanged
-j                 8             8          unchanged
--timeout          7200          14400      CHANGED
exclusions         5 documented  5 documented   unchanged, verbatim
test set           2550          2550       unchanged
```

Release+CUDA and CPU-only are untouched: both passed at 7200 s and are not re-run.

## Why this is an execution-environment setting, not a criterion

`AMENDMENT_G3.md` establishes the classification, and G10 has already relied on it
twice:

> `OMP_NUM_THREADS=1` is an execution-environment setting, not a test criterion.

The same is true of `-j` and `--timeout`. All three govern *how much machine* a
test gets and *how long it is allowed*, never *what result is accepted*. G3's own
amendment changed `-j16 -> -j 8` and the default 1500 s -> 7200 s on exactly that
reasoning. This amendment changes one number in the same family, for one
configuration, on measured grounds.

What would have been a criterion change, and was **not** done:

* raising a tolerance so a slower result passes — no tolerance exists here
* excluding the three tests — forbidden, and it would hide a measured fact
* shortening, disabling or altering the tests — not touched
* accepting 2547/2550 as the final answer — explicitly rejected

## Why 7200 s was not enough — measured, not assumed

The three tests pass in both other qualified configurations:

```text
test                                Release+CUDA   CPU-only     Debug+GUI @7200
MappedInletPreservesTheMassFlow     1529.35 s      1170.41 s    Timeout
CorrectedSequenceIsDeterministic    1950.04 s      1466.65 s    Timeout
MediumGridAB                        2029.67 s      1479.59 s    Timeout
```

Zero assertion failures in any configuration. Only `CMAKE_BUILD_TYPE` differs.

The Debug/Release runtime ratio was measured over **123 tests** appearing in both
G10 logs and taking more than 5 s in Release:

```text
samples        123
mean ratio     5.0x
median ratio   5.4x
min / max      0.9x / 6.9x
```

Projected Debug runtimes at the mean ratio:

```text
MappedInletPreservesTheMassFlow    1529 s  ->  ~7582 s
CorrectedSequenceIsDeterministic   1950 s  ->  ~9668 s
MediumGridAB                       2030 s  -> ~10063 s
```

All three exceed 7200 s; all three fit inside 14400 s with margin. **14400 s is
1.43x the largest projection**, which is a real margin rather than a value tuned
to the observed numbers — and the ceiling is still finite, so a genuine hang would
still be caught.

## What this amendment does not license

* It applies to Debug+GUI only. Release+CUDA and CPU-only keep 7200 s.
* It does not make a Debug timeout acceptable in general. If a test that currently
  fits inside 14400 s later exceeds it, that is a new finding, not a licence to
  raise the number again.
* It does not claim these three tests are fast. They are the slowest non-excluded
  tests in the suite, and that remains recorded.

## Verification required by this amendment

1. The three tests re-run under the amended timeout must **pass**, not merely
   avoid timing out.
2. The full Debug+GUI suite must then pass at 2550/2550.
3. `SOURCE_FINGERPRINT_V2` must remain `8652dda0…`, proving nothing was changed to
   obtain the result.

Results are recorded in `debug_gui_regression.md` and `final_closeout.md`.

---

## Outcome — measured 2026-09-26T02:15Z

```text
Debug+GUI, --timeout 14400        2550 / 2550, 100% passed, 0 failed
runtime                           11850 s (197.5 min)
Not Run 0                         Disabled 98 (pre-existing DISABLED_ prefixes)

MappedInletPreservesTheMassFlow    Passed   9390.87 s
CorrectedSequenceIsDeterministic   Passed  11622.20 s
MediumGridAB                       Passed  11849.28 s

SOURCE_FINGERPRINT_V2 before       8652dda040ac451e0fab8491fa90f495976513a91a30a45ef71cbf65eb0f292c
SOURCE_FINGERPRINT_V2 after        8652dda040ac451e0fab8491fa90f495976513a91a30a45ef71cbf65eb0f292c
                                   MATCH -- nothing was changed to obtain this
```

All three PASS, not merely "did not time out". The two G4-added rows also passed
(0.29 s, 0.24 s), so the amended run executed them rather than certifying them
unseen.

### My projection was 20% low, and the real margin was tighter than I stated

```text
test                                projected   actual      projected ratio  actual ratio
MappedInletPreservesTheMassFlow       7582 s     9390.87 s        5.0x           6.1x
CorrectedSequenceIsDeterministic      9668 s    11622.20 s        5.0x           6.0x
MediumGridAB                         10063 s    11849.28 s        5.0x           5.8x
```

The 123-sample mean of 5.0x underestimated these three. Their true Debug/Release
ratio is 5.8-6.1x, near the measured maximum of 6.9x rather than the mean --
unsurprising in hindsight, since they are the largest and longest-running cases in
the suite and therefore the least like the median sample.

Consequence, recorded rather than glossed: the amendment claimed 14400 s was
**1.43x** the largest projection. Against the actual worst case of 11849 s the
margin is **1.22x**. That is still a real margin and the ceiling is still finite,
so a genuine hang is still caught — but it is tighter than the amendment stated,
and anything that slows these tests by a further ~20% would exceed it. Flagged so
a future reader does not inherit the 1.43x figure as headroom that exists.
