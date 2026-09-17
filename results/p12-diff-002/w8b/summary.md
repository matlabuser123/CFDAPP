# P12-DIFF-002 W8B — amended W8: result

**Verdict: amended W8 PASSES.** All six conditions of the frozen verdict rule
([acceptance_gate.md](acceptance_gate.md) §8) hold.

This record was written after the run, from the logs listed below. The run itself wrote no verdict
line.

| rule | evidence | result |
|---|---|---|
| (a) repository tests = frozen candidate | [logs/01_integrity_build.log](logs/01_integrity_build.log) §(a): `test_structured_quad_production_case.cpp` `7acf926d…`, `test_multiblock_production_case.cpp` `b2b87bb7…` | OK |
| (b) case edits are exactly DRIFT-001 F1 | §(b): only the `face_flux` key was added; the reverse-applied files equal the pre-edit hashes; the rest of `cases/` is unchanged | OK |
| (c) no production change | §(c): `src/` + `include/` tree `0bc5c6c3…` (WSL hash) | OK |
| (d) historical evidence byte-identical | §(d): 13 files OK | OK |
| (e) fresh clean-first build; both W8 tests and both suites pass | §"configure + clean-first rebuild": BUILD OK, 0 warnings in the two test sources. [logs/02_W8B.log](logs/02_W8B.log): **15/15**, exit 0 | OK |
| (f) the §7 control outcomes reproduce | [logs/post_driver.log](logs/post_driver.log): candidate 15/15, linear 13/15, nodiff 10/15, far-cell ×2 7/15, sign flip 6/15, equal to the pre-freeze dry-run | OK |

## Measured (fresh run, logs/02)

**StructuredQuad** (distorted Poiseuille, Rhie–Chow):

- velocity L2 is 1.0921e-2, 4.6453e-3 and 2.0069e-3 at 64×8, 96×12 and 144×18. The Cartesian
  references are 8.4927e-3, 3.7905e-3 and 1.6879e-3, so every grid is within the 1.5× bound.
- The velocity order is 2.108 and 2.070, against ≥ 1.5.
- The signed dp/dx error is −3.735e-3, +1.252e-3 and +1.280e-3. It is printed only: the family is
  pre-asymptotic for dp/dx (DRIFT-001 §3).
- **W8B-4:** at 144×18, a re-solve with every tolerance ÷ 100 moves velocity L2 by **0.043 %** of
  the error, against the 10 % limit.
- **Activation check (S1):** the uncorrected velocity L2 is 1.6331e-2, above the 1.2739e-2
  requirement; the corrected one is 1.0921e-2, which passes.

**MultiBlock** (curved channel, Rhie–Chow):

- velocity L2 is 1.1164e-2, 4.8376e-3 and 2.0936e-3;
- the observed orders are 2.063 and 2.066 (velocity), 1.950 and 2.031 (G), and 3.297 and 4.935
  (radial rise);
- the relative G error is 6.75e-3, 3.06e-3 and 1.34e-3;
- **W8B-4:** at 18×45, a re-solve with tolerances ÷ 100 moves velocity by **0.681 %** and G by
  **0.409 %** of their errors, against the 10 % limit;
- the sector conduction orders are 1.945 and 1.968 (heat flow) and 2.998 and 2.999 (temperature).

## Later changes to the two test files (layout only)

P12-DIFF-002-FORMAT-001 reformatted both files after this run:

| file | at this run | after formatting |
|---|---|---|
| structured quad | `7acf926d…` | `41a4d506…` |
| multiblock | `b2b87bb7…` | `7176b3de…` |

`format-001/logs/02_token_proof.log` proves the change is layout only:

- the code tokens are identical;
- the `#include` sets are identical;
- the comment words are identical.

The W10 regression re-ran both suites on the formatted files.
