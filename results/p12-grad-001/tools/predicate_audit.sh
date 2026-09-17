#!/usr/bin/env bash
# P12-GRAD-001 Part A: audit every exact-geometry predicate in the production sources and classify it
# as discontinuous (an O(1) change of formulation across the branch) or continuous (the branch's own
# correction is proportional to the tested quantity, so a flip changes results by round-off).
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
L=$R/results/p12-grad-001/logs/00_predicate_audit.log
cd $R
{
  echo "# P12-GRAD-001 exact-geometry predicate audit; $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# every occurrence of an exact floating-point geometric test in include/ and src/:"
  grep -rn -- "== Vector3{}\|!= Vector3{}\|exactlyParallel\|isAxisAligned" include src | sed 's/^/  /'
  echo
  echo "# classification:"
  cat <<'TXT'
  DISCONTINUOUS (fixed by P12-GRAD-001):
    src/discretization/Gradient.cpp tryPairedBoundaryContribution -- selects between the paired
      quadratic boundary fit (2nd order) and plain Green-Gauss (1st order at the boundary). The two
      formulations differ by O(h), so a round-off flip changes the boundary gradient by O(1) relative.
  CONTINUOUS (left unchanged; a flip changes results by O(round-off)):
    src/discretization/Gradient.cpp obliqueNeumannFace -- the oblique-Neumann correction is
      proportional to the tangential offset d_t, which vanishes with the misalignment.
    src/mesh/MeshGeometry.cpp decomposeAreaVector -- the exactly-parallel shortcut returns what the
      general formula returns in the limit; the non-orthogonal part is the tested quantity itself.
    src/mesh/MeshGeometry.cpp ownerNeighborCrossing -- the skew vector is the tested quantity.
    src/pressure_velocity/PressureCorrectionEquation.cpp isAxisAligned/exactlyParallel -- selects
      whether one or three response-coefficient components are interpolated; the others are
      multiplied by a (near-)zero area component.
    src/discretization/Interpolation.cpp, Gradient.cpp (skew correction), VectorGradient.cpp --
      the skewness correction is proportional to the skew vector.
  The boundary/opposite AREA match inside tryPairedBoundaryContribution (fixed 1e-12 relative) is
  measured in logs/01: exactly 0 on every translated Cartesian mesh (both faces span identical
  coordinate ranges) and 3.6e-2 on the distorted Q16 mesh, so it is left unchanged.
TXT
} > $L 2>&1
cat $L
