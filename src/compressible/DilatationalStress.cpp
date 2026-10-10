#include "cfd/compressible/DilatationalStress.hpp"

#include <cmath>
#include <memory>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/discretization/Divergence.hpp"
#include "cfd/fields/ScalarField.hpp"

namespace cfd::compressible {

namespace {

// Zero-gradient on every patch, for the derived scalar div U. There is no physical boundary
// condition for dilatation; see the header for the accuracy consequence.
cfd::boundary::BoundaryConditionSet zeroGradientForDivergence(const cfd::mesh::Mesh& mesh) {
  cfd::boundary::BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    boundaries.set(mesh, patch.name(), std::make_unique<cfd::boundary::FixedGradient>(0.0));
  }
  return boundaries;
}

}  // namespace

void assembleDilatationalStressContribution(
    const cfd::mesh::Mesh& mesh, const cfd::fields::VectorField& velocity,
    const cfd::boundary::BoundaryConditionSet& velocityBoundaries, Real dynamicViscosity,
    cfd::physics::VelocityComponent component, cfd::algebra::Vector& rhs,
    cfd::discretization::GradientScheme scheme) {
  const Index n = mesh.numberOfCells();
  if (velocity.size() != n) {
    throw InvalidArgumentError(
        "assembleDilatationalStressContribution: velocity size does not match mesh cell count");
  }
  if (rhs.size() != n) {
    throw InvalidArgumentError(
        "assembleDilatationalStressContribution: rhs size does not match mesh cell count");
  }
  // mu == 0 is admissible (an inviscid case contributes nothing); negative or non-finite is
  // not. Same style and wording as the neighbouring validators.
  if (!std::isfinite(dynamicViscosity) || dynamicViscosity < 0.0) {
    throw InvalidArgumentError(
        "assembleDilatationalStressContribution: dynamicViscosity must be finite and >= 0");
  }

  // div U, from the SAME velocity boundary conditions the mass flux uses -- so the face
  // velocities entering the divergence are the ones the rest of the solver sees.
  const cfd::fields::ScalarField divergenceU =
      cfd::discretization::divergence(mesh, velocity, velocityBoundaries);

  const cfd::boundary::BoundaryConditionSet divergenceBoundaries = zeroGradientForDivergence(mesh);
  const cfd::fields::VectorField gradientOfDivergence =
      cfd::discretization::gradient(mesh, divergenceU, divergenceBoundaries, scheme);

  constexpr Real kTwoThirds = 2.0 / 3.0;
  for (const auto& cell : mesh.cells()) {
    const Real gradComponent =
        cfd::physics::velocityComponentValue(gradientOfDivergence[cell.id()], component);
    rhs[cell.id()] += -kTwoThirds * dynamicViscosity * gradComponent * cell.volume();
  }
}

}  // namespace cfd::compressible
