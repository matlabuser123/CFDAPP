# P12-ASAN-001 — `MeshQualityReport.DisconnectedMeshIsFatal` use-after-free (test code)

Authorized 2026-09-17 as a separately scoped fix of the known pre-push blocker (first recorded in
`results/p12-mesh-006/summary.md` §32). Test-only; no production file touched.

## 1. Reproduction before the fix — [logs/01](logs/01_reproduce_before_fix.log)

The `build/asan` tree (`CFDAPP_ENABLE_SANITIZERS=ON`, Debug, `-fsanitize=address,undefined`)
was stale (2026-09-16), so `CFDMeshTests` was rebuilt against the current sources first. It was run
under CI's sanitizer settings (`ASAN_OPTIONS=detect_leaks=1:halt_on_error=0`,
`UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=0`):

```text
ERROR: AddressSanitizer: heap-use-after-free ... READ of size 8
  #5 TestBody tests/unit/mesh/test_mesh_quality_report.cpp:489
SUMMARY: AddressSanitizer: heap-use-after-free ... basic_string::size()
```

Test source sha256 before the fix: `a92d9556…`.

## 2. Classification

This is the documented destroyed-temporary lifetime bug:

```cpp
const MeshQualityIssue* issue =
    findIssue(MeshQuality::evaluate(mesh), MeshQualitySeverity::Fatal, "connected_components");
EXPECT_EQ(issue->entity, "mesh");   // line 489: reads the destroyed report
```

1. `MeshQuality::evaluate(mesh)` returns a temporary `MeshQualityReport`.
2. `findIssue` returns a pointer into that temporary's `issues` vector.
3. The temporary is destroyed at the end of the full expression, so line 489 reads freed memory.

It is a test-code defect. Production behaviour is not involved.

## 3. Fix (test only, smallest change)

```cpp
// The report must outlive `issue`, which points into its issue list.
const MeshQualityReport report = MeshQuality::evaluate(mesh);
const MeshQualityIssue* issue =
    findIssue(report, MeshQualitySeverity::Fatal, "connected_components");
ASSERT_NE(issue, nullptr);
```

- The semantic assertions are unchanged: entity `"mesh"`, value `2.0`, and the `expectFatal`
  message check above them.
- `ASSERT_NE(issue, nullptr)` matches how every other `findIssue` call in the file guards its
  pointer, and it is strictly stronger than before.
- Test source sha256 after the fix: `37dc4cb1…`.

## 4. Verification

| run | result |
|---|---|
| [logs/02](logs/02_after_fix_single.log): the named test under ASan+UBSan, rebuilt binary `808979da…` | **PASSED**, 0 sanitizer diagnostics |
| [logs/03](logs/03_after_fix_mesh_suite.log): the whole `CFDMeshTests` under ASan+UBSan | **156 / 156 passed**, 1 disabled, 0 sanitizer diagnostics |

The rebuilt binary is newer than the test source (the harness fails closed otherwise), and it links
the same ASan `libcfdcore.a` (`5731b8ff…`) as the reproduction.

The full sanitizer regression at CI settings belongs to the final regression of the
DIFF-002 → GRAD-002 → MESH-007 chain and is recorded there.
