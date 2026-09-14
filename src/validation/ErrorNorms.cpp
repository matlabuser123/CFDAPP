#include "cfd/validation/ErrorNorms.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

#include "cfd/core/Exception.hpp"

namespace cfd::validation {

using cfd::fields::ScalarField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;

namespace {

void requireCellSized(const Mesh& mesh, Index size, const char* what) {
  if (size != mesh.numberOfCells()) {
    throw InvalidArgumentError(std::string("computeErrorNorms: ") + what +
                               " size does not match mesh cell count");
  }
}

bool selected(const CellMask* mask, Index cell) { return mask == nullptr || (*mask)[cell]; }

// The one accumulation: `errorAt(cell)` gives the (already formed,
// non-negative-or-signed) scalar error of a selected cell.
template <typename ErrorAt>
ErrorNorms accumulate(const Mesh& mesh, const CellMask* mask, const ErrorAt& errorAt) {
  if (mask != nullptr && mask->size() != mesh.numberOfCells()) {
    throw InvalidArgumentError("computeErrorNorms: mask size does not match mesh cell count");
  }
  ErrorNorms norms;
  Real sumAbs = 0.0;
  Real sumSquares = 0.0;
  for (const auto& cell : mesh.cells()) {
    if (!selected(mask, cell.id())) continue;
    const Real error = errorAt(cell.id());
    if (!std::isfinite(error)) {
      throw InvalidArgumentError("computeErrorNorms: non-finite error value");
    }
    const Real magnitude = std::abs(error);
    sumAbs += magnitude * cell.volume();
    sumSquares += error * error * cell.volume();
    norms.linf = std::max(norms.linf, magnitude);
    norms.volume += cell.volume();
    ++norms.cells;
  }
  if (norms.cells == 0 || !(norms.volume > 0.0)) {
    throw InvalidArgumentError("computeErrorNorms: empty cell selection");
  }
  norms.l1 = sumAbs / norms.volume;
  norms.l2 = std::sqrt(sumSquares / norms.volume);
  return norms;
}

}  // namespace

ErrorNorms computeErrorNorms(const Mesh& mesh, const ScalarField& numeric, const ScalarField& exact,
                             const CellMask* mask) {
  requireCellSized(mesh, numeric.size(), "numeric");
  requireCellSized(mesh, exact.size(), "exact");
  return accumulate(mesh, mask, [&](Index cell) { return numeric[cell] - exact[cell]; });
}

ErrorNorms computeErrorNorms(const Mesh& mesh, const ScalarField& error, const CellMask* mask) {
  requireCellSized(mesh, error.size(), "error");
  return accumulate(mesh, mask, [&](Index cell) { return error[cell]; });
}

VectorErrorNorms computeVectorErrorNorms(const Mesh& mesh, const VectorField& numeric,
                                         const VectorField& exact, const CellMask* mask) {
  requireCellSized(mesh, numeric.size(), "numeric");
  requireCellSized(mesh, exact.size(), "exact");
  VectorErrorNorms norms;
  norms.x = accumulate(mesh, mask, [&](Index cell) { return numeric[cell].x - exact[cell].x; });
  norms.y = accumulate(mesh, mask, [&](Index cell) { return numeric[cell].y - exact[cell].y; });
  norms.magnitude = accumulate(mesh, mask, [&](Index cell) {
    const Real ex = numeric[cell].x - exact[cell].x;
    const Real ey = numeric[cell].y - exact[cell].y;
    return std::sqrt((ex * ex) + (ey * ey));
  });
  return norms;
}

ErrorNorms computeFaceErrorNorms(const Mesh& mesh, const cfd::fields::SurfaceField& numeric,
                                 const cfd::fields::SurfaceField& exact, FaceSelection selection) {
  if (numeric.size() != mesh.numberOfFaces() || exact.size() != mesh.numberOfFaces()) {
    throw InvalidArgumentError("computeFaceErrorNorms: size does not match mesh face count");
  }
  ErrorNorms norms;
  Real sumAbs = 0.0;
  Real sumSquares = 0.0;
  for (const auto& face : mesh.faces()) {
    if ((selection == FaceSelection::Internal && face.isBoundary()) ||
        (selection == FaceSelection::Boundary && !face.isBoundary())) {
      continue;
    }
    const Real error = numeric[face.id()] - exact[face.id()];
    if (!std::isfinite(error)) {
      throw InvalidArgumentError("computeFaceErrorNorms: non-finite error value");
    }
    const Real magnitude = std::abs(error);
    sumAbs += magnitude * face.area();
    sumSquares += error * error * face.area();
    norms.linf = std::max(norms.linf, magnitude);
    norms.volume += face.area();
    ++norms.cells;
  }
  if (norms.cells == 0 || !(norms.volume > 0.0)) {
    throw InvalidArgumentError("computeFaceErrorNorms: empty face selection");
  }
  norms.l1 = sumAbs / norms.volume;
  norms.l2 = std::sqrt(sumSquares / norms.volume);
  return norms;
}

Real volumeWeightedMean(const Mesh& mesh, const ScalarField& field) {
  if (field.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError("volumeWeightedMean: field size does not match mesh cell count");
  }
  Real weighted = 0.0;
  Real volume = 0.0;
  for (const auto& cell : mesh.cells()) {
    const Real value = field[cell.id()];
    if (!std::isfinite(value)) {
      throw InvalidArgumentError("volumeWeightedMean: non-finite value");
    }
    weighted += value * cell.volume();
    volume += cell.volume();
  }
  if (!(volume > 0.0)) {
    throw InvalidArgumentError("volumeWeightedMean: mesh has no volume");
  }
  return weighted / volume;
}

ScalarField removeVolumeWeightedMean(const Mesh& mesh, const ScalarField& field) {
  const Real mean = volumeWeightedMean(mesh, field);
  ScalarField shifted(field.size());
  for (Index i = 0; i < field.size(); ++i) shifted[i] = field[i] - mean;
  return shifted;
}

ErrorNorms computeGaugeInvariantErrorNorms(const Mesh& mesh, const ScalarField& numeric,
                                           const ScalarField& exact, const CellMask* mask) {
  return computeErrorNorms(mesh, removeVolumeWeightedMean(mesh, numeric),
                           removeVolumeWeightedMean(mesh, exact), mask);
}

CellMask boundaryAdjacentCells(const Mesh& mesh, Index layers) {
  if (layers == 0) {
    throw InvalidArgumentError("boundaryAdjacentCells: layers must be >= 1");
  }
  CellMask mask(mesh.numberOfCells(), false);
  for (const auto& face : mesh.faces()) {
    if (face.isBoundary()) mask[face.owner()] = true;
  }
  for (Index layer = 1; layer < layers; ++layer) {
    CellMask grown = mask;
    for (const auto& face : mesh.faces()) {
      if (face.isBoundary()) continue;
      const Index owner = face.owner();
      const Index neighbor = *face.neighbor();
      if (mask[owner]) grown[neighbor] = true;
      if (mask[neighbor]) grown[owner] = true;
    }
    mask = std::move(grown);
  }
  return mask;
}

ErrorNorms computeSampleErrorNorms(const std::vector<Real>& numeric,
                                   const std::vector<Real>& reference) {
  if (numeric.size() != reference.size()) {
    throw InvalidArgumentError("computeSampleErrorNorms: numeric and reference sizes differ");
  }
  if (numeric.empty()) throw InvalidArgumentError("computeSampleErrorNorms: no samples");
  ErrorNorms norms;
  Real sumAbs = 0.0;
  Real sumSquares = 0.0;
  for (std::size_t k = 0; k < numeric.size(); ++k) {
    if (!std::isfinite(numeric[k]) || !std::isfinite(reference[k])) {
      throw InvalidArgumentError("computeSampleErrorNorms: non-finite value at sample " +
                                 std::to_string(k));
    }
    const Real error = std::abs(numeric[k] - reference[k]);
    sumAbs += error;
    sumSquares += error * error;
    norms.linf = std::max(norms.linf, error);
  }
  const auto count = static_cast<Real>(numeric.size());
  norms.l1 = sumAbs / count;
  norms.l2 = std::sqrt(sumSquares / count);
  norms.cells = static_cast<Index>(numeric.size());
  norms.volume = count;
  return norms;
}

CellMask invertMask(const CellMask& mask) {
  CellMask inverted(mask.size());
  for (std::size_t i = 0; i < mask.size(); ++i) inverted[i] = !mask[i];
  return inverted;
}

}  // namespace cfd::validation
