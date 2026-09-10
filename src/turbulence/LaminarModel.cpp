#include "cfd/turbulence/LaminarModel.hpp"

namespace cfd::turbulence {

using cfd::fields::ScalarField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;

LaminarModel::LaminarModel(const Mesh& mesh) : turbulentViscosity_(mesh.numberOfCells(), 0.0) {}

std::string_view LaminarModel::name() const noexcept { return "laminar"; }

const ScalarField& LaminarModel::turbulentViscosity() const { return turbulentViscosity_; }

void LaminarModel::correct(const Mesh& /*mesh*/, const VectorField& /*velocity*/,
                           const ScalarField& /*pressure*/) {
  // Intentional no-op -- see this class's own header comment.
}

}  // namespace cfd::turbulence
