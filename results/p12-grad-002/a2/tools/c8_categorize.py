#!/usr/bin/env python3
"""P12-GRAD-002 A2, C8: every test whose printed numbers differ between cur and nograd must fall into
a pre-registered, justified category (acceptance_gate_A2.md section 4). An uncategorized change is an
unexplained change and fails C8.

usage: c8_categorize.py <compare_ctest_numbers.py log>

A  the test's mesh is non-orthogonal, skewed, translated by a non-representable offset, or moving:
   GRAD-002 intentionally changes the boundary-cell gradient there (first -> second order).
B  the test's mesh is aligned (Cartesian, rectilinear graded, rectangular multi-block, Cartesian
   hexahedra): C11(a) bounds the boundary-gradient change at 6e-16, so only the solver trajectory
   changes at round-off, visible in stopping-point diagnostics (residuals, imbalances, symmetry
   defects, iteration counts, inner-solver totals) and in runs that amplify round-off (deliberately
   diverging robustness runs).
T  a wall-clock-derived value the timing mask cannot recognise (a ratio of two timings).
"""
import re
import sys

CATEGORIES = [
    # --- A: non-orthogonal / skewed / translated / moving meshes ---
    (r"^AlePiso", "A", "moving meshes (MESH-007 ALE): translated and deformed grids"),
    (r"^DiffusionTest\.ClosedDomainGlobalBalance", "A", "distorted 10x10 mesh (0.4 h)"),
    (r"^GradedMeshProductionCase\.GradedVertexGridOnNonOrthogonalStructuredQuad", "A",
     "graded structured_quad mesh, non-orthogonal"),
    (r"^GridRefinementTest\..*Distorted", "A", "distorted refinement family"),
    (r"^MeshQualityCampaign\.", "A", "MESH-004 distorted quality families"),
    (r"^MomentumMMS\.DistortedMesh", "A", "distorted MMS mesh"),
    (r"^MultiBlockProductionCase\.(CurvedChannel|SectorConduction)", "A", "polar multi-block (curved)"),
    (r"^ObliqueNeumannGradient\.", "A", "tilted grid lines at the walls"),
    (r"^SIMPLEMMS\.DistortedMesh", "A", "distorted MMS mesh"),
    (r"^SIMPLENonOrthogonalTest\.DistortedMesh", "A", "distorted cavity (0.45 h)"),
    (r"^ScalarMMS\.DistortedMesh", "A", "distorted MMS mesh"),
    (r"^SkewnessTest\.", "A", "distorted 10x10 mesh (0.45 h)"),
    (r"^StructuredQuadProductionCase\.", "A", "MESH-001 structured_quad meshes (up to 48 deg)"),
    (r"^ThermalNonOrthogonalTest\.", "A", "non-orthogonal thermal mesh"),
    (r"^GradientBoundaryConsistency\.", "A",
     "GRAD-002's own regression tests (translated, sheared, distorted and 3D meshes)"),
    # --- T: timing-derived ---
    (r"^SIMPLERobustnessTest\.BookkeepingOverheadIsSmall$", "T", "ratio of two wall-clock times"),
    (r"^SolverRobustnessPerformance\.", "T", "wall-clock cost per iteration"),
    # --- B: aligned meshes ---
    (r"^CFDAppCli3D_valid_(cube|duct)3d", "B", "Cartesian hexahedra (CLI smoke)"),
    (r"^CFDAppCli_", "B", "Cartesian / graded CLI fixtures"),
    (r"^CompressibleSimpleMMS\.", "B", "Cartesian MMS"),
    (r"^(Duct3DProductionCase|LidDrivenCube3DProductionCase|SIMPLE3D|MMS3DTest|MMSSimple3DTest)\.", "B",
     "Cartesian hexahedra"),
    (r"^GradedMeshProductionCase\.(Committed|GradedGridConvergence|GradedMeshBeatsUniform|XAndYGrading)",
     "B", "rectilinear graded (aligned) channel"),
    (r"^LowMachRegressionTest\.", "B", "Cartesian channel"),
    (r"^(MomentumMMS\.UConverges|SIMPLEMMS\.(ConvergesFromNonExact|VelocityConverges)|ScalarMMS\.AdvectionDiffusion)",
     "B", "Cartesian MMS"),
    (r"^MultiBlockProductionCase\.(ObstacleChannel|StepChannel)", "B", "rectangular multi-block (aligned)"),
    (r"^(NaturalConvectionValidation|PoiseuilleValidation|SchemeValidation|CavityGhiaValidation|"
     r"TurbulentChannelValidation|ChannelFlowValidation|TransientCavity)\.", "B", "Cartesian validation case"),
    (r"^SIMPLENonOrthogonalTest\.Cartesian", "B", "Cartesian cavity"),
    (r"^SIMPLERobustnessTest\.", "B", "Cartesian cavity / channel (incl. deliberately diverging runs)"),
]


def categorize(name):
    for pattern, cat, why in CATEGORIES:
        if re.search(pattern, name):
            return cat, why
    return None, None


def self_test():
    """Non-vacuity: an unknown test must be unexplained; an aligned and a distorted test must map."""
    ok = (categorize("SomeNewSuite.Unknown")[0] is None
          and categorize("StructuredQuadProductionCase.X")[0] == "A"
          and categorize("PoiseuilleValidation.GridConvergence")[0] == "B")
    print(f"self-test: {'OK' if ok else 'INSTRUMENT BROKEN'}")
    return ok


def main():
    if not self_test():
        return 2
    changed = []
    with open(sys.argv[1]) as fh:
        for line in fh:
            m = re.match(r"^(CHANGED|TEXT|STATUS)\s+(\S+?):", line)
            if m:
                changed.append((m.group(1), m.group(2), line.rstrip()))
    counts = {}
    unexplained = 0
    for kind, name, line in changed:
        for pattern, cat, why in CATEGORIES:
            if re.search(pattern, name):
                counts[cat] = counts.get(cat, 0) + 1
                print(f"{cat}  {kind:7s} {name}  -- {why}")
                break
        else:
            unexplained += 1
            print(f"??  {kind:7s} {name}  -- UNEXPLAINED")
    print("categories: " + ", ".join(f"{k} {v}" for k, v in sorted(counts.items()))
          + f"; unexplained {unexplained}")
    return 1 if unexplained else 0


if __name__ == "__main__":
    sys.exit(main())
