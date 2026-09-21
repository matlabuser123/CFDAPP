# GPU-PIPE-001 — full regression

CUDA-enabled Release build (`build/cuda`, `CFDAPP_ENABLE_CUDA=ON`, native sm_80+sm_89), so the GPU
code paths are present and exercised alongside every CPU test.

```text
ctest --test-dir build/cuda -j16 --output-on-failure --timeout 7200

100% tests passed, 0 tests failed out of 1998
Total Test time (real) = 227.22 s
2043 listed = 1998 executed + 45 disabled
```

Zero failures, zero timeouts, zero exceptions.

## Generated-file audit

The run rewrote 50 tracked `results/validation/**` reports. Classified before restoring:

```text
non-timing numeric pairs with |v| > 1e-6 that differ:  0
max relative difference:                               0.000e+00
```

**Every difference was a timing field.** Unlike the Phase-1 Debug run (which showed round-off-level
differences up to 1.8e-5 from Debug optimization), this Release regression changed no computed value
at all. Filenames captured in `11_generated_files.txt`; all 50 restored with `git checkout`;
`results/validation` is clean.
