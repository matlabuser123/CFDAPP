#include <gtest/gtest.h>

#include <memory>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/pressure_velocity/PressureCorrectionEquation.hpp"

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::pressure_velocity::assemblePressureCorrection;
using cfd::pressure_velocity::correctFaceMassFlux;

namespace {
BoundaryConditionSet makeZeroGradientPressureBoundaries(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    boundaries.set(mesh, patch.name(), std::make_unique<FixedGradient>(0.0));
  }
  return boundaries;
}
}  // namespace

TEST(FluxCorrectionTest, InternalFaceCorrectionMatchesDCoefficientTimesPressureDifference) {
  // TODO.md section 56: for a known small p' field, F_f' must equal
  // faceCoefficient * (p'_owner - p'_neighbor) exactly, and the
  // corrected flux must equal predictor + that correction exactly.
  const Mesh mesh = MeshGeometry::createCartesian2D(3, 1, 3.0, 1.0);
  const ScalarField responseCoefficient(mesh.numberOfCells(), 2.0);
  SurfaceField predictorFlux(mesh.numberOfFaces(), 0.0);
  for (const auto& face : mesh.faces()) {
    if (!face.isBoundary()) predictorFlux[face.id()] = 1.5;
  }

  const auto assembly = assemblePressureCorrection(
      mesh, predictorFlux, responseCoefficient, responseCoefficient, /*density=*/1.0,
      /*referenceCell=*/0, makeZeroGradientPressureBoundaries(mesh));

  ScalarField pPrime(mesh.numberOfCells());
  pPrime[0] = 0.0;
  pPrime[1] = 0.1;
  pPrime[2] = -0.2;

  const SurfaceField corrected =
      correctFaceMassFlux(mesh, predictorFlux, assembly.faceCoefficient, pPrime);

  for (const auto& face : mesh.faces()) {
    if (face.isBoundary()) continue;
    const Real expectedCorrection =
        assembly.faceCoefficient[face.id()] * (pPrime[face.owner()] - pPrime[*face.neighbor()]);
    EXPECT_NEAR(corrected[face.id()], predictorFlux[face.id()] + expectedCorrection, 1e-12);
  }
}

TEST(FluxCorrectionTest, BoundaryFluxIsUnchangedByCorrection) {
  // TODO.md section 32: impermeable wall / fixed inlet flux must stay
  // exactly what the predictor gave it -- this phase's boundary
  // treatment gives every boundary face zero coefficient (see
  // PressureCorrectionEquation.hpp), so correction there is always 0.
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const ScalarField responseCoefficient(mesh.numberOfCells(), 1.0);
  SurfaceField predictorFlux(mesh.numberOfFaces(), 0.0);
  for (const auto& face : mesh.faces()) {
    if (face.isBoundary()) predictorFlux[face.id()] = 0.0;  // e.g. a closed cavity
  }

  const auto assembly =
      assemblePressureCorrection(mesh, predictorFlux, responseCoefficient, responseCoefficient, 1.0,
                                 0, makeZeroGradientPressureBoundaries(mesh));

  ScalarField pPrime(mesh.numberOfCells());
  for (Index i = 0; i < pPrime.size(); ++i) pPrime[i] = 0.01 * static_cast<Real>(i);

  const SurfaceField corrected =
      correctFaceMassFlux(mesh, predictorFlux, assembly.faceCoefficient, pPrime);
  for (const auto& face : mesh.faces()) {
    if (face.isBoundary()) {
      EXPECT_NEAR(corrected[face.id()], predictorFlux[face.id()], 1e-12);
    }
  }
}

TEST(FluxCorrectionTest, ConstantPressureCorrectionGivesZeroFluxCorrectionEverywhere) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const ScalarField responseCoefficient(mesh.numberOfCells(), 1.0);
  SurfaceField predictorFlux(mesh.numberOfFaces());
  for (Index i = 0; i < predictorFlux.size(); ++i) predictorFlux[i] = 0.05 * static_cast<Real>(i);

  const auto assembly =
      assemblePressureCorrection(mesh, predictorFlux, responseCoefficient, responseCoefficient, 1.0,
                                 0, makeZeroGradientPressureBoundaries(mesh));
  const ScalarField pPrime(mesh.numberOfCells(), 7.0);

  const SurfaceField corrected =
      correctFaceMassFlux(mesh, predictorFlux, assembly.faceCoefficient, pPrime);
  for (Index i = 0; i < corrected.size(); ++i) {
    EXPECT_NEAR(corrected[i], predictorFlux[i], 1e-12);
  }
}

TEST(FluxCorrectionTest, MismatchedFaceCoefficientSizeThrows) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const SurfaceField predictorFlux(mesh.numberOfFaces(), 0.0);
  const SurfaceField badFaceCoefficient(mesh.numberOfFaces() + 1, 0.0);
  const ScalarField pPrime(mesh.numberOfCells(), 0.0);

  EXPECT_THROW((void)correctFaceMassFlux(mesh, predictorFlux, badFaceCoefficient, pPrime),
               InvalidArgumentError);
}
