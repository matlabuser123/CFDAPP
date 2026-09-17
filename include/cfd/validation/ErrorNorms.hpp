#pragma once

#include <vector>

#include "cfd/core/Types.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/mesh/Mesh.hpp"

// P12-NUM-006 -- the one authoritative implementation of the discrete error
// norms used for verification against an exact (analytical / manufactured)
// solution, and of the pressure-gauge removal needed to compare an
// incompressible pressure field. Every MMS test and report computes its
// norms here; none re-implements a formula.
namespace cfd::validation {

// Volume-weighted cell norms of an error field e over a set of cells C
// (all cells, or those selected by a mask):
//   L1   = sum_C |e_P| V_P / sum_C V_P           (mean absolute error)
//   L2   = sqrt( sum_C e_P^2 V_P / sum_C V_P )   (RMS error)
//   Linf = max_C |e_P|
// The weighting makes L1/L2 approximations of the continuous
// (1/|Omega|) integral norms, so they are comparable across grids and
// meshes. `cells` and `volume` record how many cells / how much volume
// entered the norm.
struct ErrorNorms {
  Real l1{0.0};
  Real l2{0.0};
  Real linf{0.0};
  Index cells{0};
  Real volume{0.0};
};

// A cell selection: mask[i] == true includes cell i. nullptr = all cells.
using CellMask = std::vector<bool>;

// Norms of numeric - exact. Throws InvalidArgumentError if a size does
// not match mesh.numberOfCells() (the mask included), the selection is
// empty, or any value is non-finite (an error norm is never computed from
// a non-finite field -- a failed solve must be rejected before this).
[[nodiscard]] ErrorNorms computeErrorNorms(const cfd::mesh::Mesh& mesh,
                                           const cfd::fields::ScalarField& numeric,
                                           const cfd::fields::ScalarField& exact,
                                           const CellMask* mask = nullptr);

// Norms of an already-formed error field (same rules).
[[nodiscard]] ErrorNorms computeErrorNorms(const cfd::mesh::Mesh& mesh,
                                           const cfd::fields::ScalarField& error,
                                           const CellMask* mask = nullptr);

// Vector error: each component separately and the pointwise magnitude of
// the error vector |e| = sqrt(e_x^2 + e_y^2 + e_z^2) (L2 of the magnitude is
// the RMS vector error; Linf the largest vector error). P12-MESH-005: `z`
// and the z term are the third component (identically zero for 2D fields,
// whose x, y and magnitude norms are therefore exactly the 2D ones).
struct VectorErrorNorms {
  ErrorNorms x;
  ErrorNorms y;
  ErrorNorms magnitude;
  ErrorNorms z;
};
[[nodiscard]] VectorErrorNorms computeVectorErrorNorms(const cfd::mesh::Mesh& mesh,
                                                       const cfd::fields::VectorField& numeric,
                                                       const cfd::fields::VectorField& exact,
                                                       const CellMask* mask = nullptr);

// Face norms of a per-face error (e.g. a face-flux or normal-velocity
// error), weighted by face area over all faces (or only boundary /
// internal ones): L1 = sum|e_f| A_f / sum A_f, L2 = sqrt(sum e_f^2 A_f /
// sum A_f), Linf = max|e_f|. In the result `cells` counts the faces and
// `volume` is the total face area. Same rejection rules as above.
enum class FaceSelection { All, Internal, Boundary };
[[nodiscard]] ErrorNorms computeFaceErrorNorms(const cfd::mesh::Mesh& mesh,
                                               const cfd::fields::SurfaceField& numeric,
                                               const cfd::fields::SurfaceField& exact,
                                               FaceSelection selection = FaceSelection::All);

// sum_P f_P V_P / sum_P V_P over ALL cells. Throws InvalidArgumentError on
// a size mismatch, an empty mesh or a non-finite value.
[[nodiscard]] Real volumeWeightedMean(const cfd::mesh::Mesh& mesh,
                                      const cfd::fields::ScalarField& field);

// field - volumeWeightedMean(field): the representative of an
// incompressible pressure field (defined only up to a constant) with zero
// volume-weighted mean.
[[nodiscard]] cfd::fields::ScalarField removeVolumeWeightedMean(
    const cfd::mesh::Mesh& mesh, const cfd::fields::ScalarField& field);

// Gauge-invariant pressure error: both fields are shifted to zero
// volume-weighted mean over ALL cells (the gauge convention), then the
// norms of the difference are taken over the selection. Adding any
// constant to either field leaves the result unchanged (to round-off).
[[nodiscard]] ErrorNorms computeGaugeInvariantErrorNorms(const cfd::mesh::Mesh& mesh,
                                                         const cfd::fields::ScalarField& numeric,
                                                         const cfd::fields::ScalarField& exact,
                                                         const CellMask* mask = nullptr);

// Cells within `layers` face-neighbour steps of the domain boundary: layer
// 1 = every cell owning a boundary face, layer 2 adds their face
// neighbours, and so on. Used to separate boundary-adjacent from interior
// error. Throws InvalidArgumentError if layers == 0.
[[nodiscard]] CellMask boundaryAdjacentCells(const cfd::mesh::Mesh& mesh, Index layers = 1);

// The complement of a mask.
[[nodiscard]] CellMask invertMask(const CellMask& mask);

// P12-NUM-007: norms against a reference given only at discrete sample
// points (a published benchmark table, e.g. Ghia et al. centerline data),
// every sample weighted equally -- the tabulated stations carry no cell
// volume:
//   L1 = mean |e_k|,  L2 = sqrt(mean e_k^2),  Linf = max |e_k|
// with e_k = numeric[k] - reference[k]. In the result `cells` is the
// number of samples and `volume` equals it (unit weights). Throws
// InvalidArgumentError if the sizes differ, there are no samples, or any
// value is non-finite.
[[nodiscard]] ErrorNorms computeSampleErrorNorms(const std::vector<Real>& numeric,
                                                 const std::vector<Real>& reference);

}  // namespace cfd::validation
