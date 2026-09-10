#pragma once

#include "cfd/turbulence/TurbulenceModel.hpp"

namespace cfd::turbulence {

// P2-TURB-002: the trivial concrete TurbulenceModel -- mu_t = 0 in
// every cell, so mu_eff (via the base class's inherited
// effectiveViscosity()) reduces to exactly the molecular viscosity.
// Exists to prove the interface itself introduces no numerical change
// before any real RANS model does (TODO.md P2 -- Turbulence): this class
// implements no turbulence physics whatsoever -- no k/epsilon/omega
// transport, no eddy-viscosity formula, no wall distance/y+/wall
// functions, no production/dissipation terms. It is the shared solver-
// facing abstraction laminar and future RANS models both plug into, not
// a step toward RANS itself.
class LaminarModel final : public TurbulenceModel {
 public:
  // mu_t is sized to mesh.numberOfCells() and set to exactly 0.0 in
  // every cell at construction -- never lazily/partially initialized.
  explicit LaminarModel(const cfd::mesh::Mesh& mesh);

  [[nodiscard]] std::string_view name() const noexcept override;

  [[nodiscard]] const cfd::fields::ScalarField& turbulentViscosity() const override;

  // A documented no-op: laminar flow has zero eddy viscosity regardless
  // of the current flow state, so there is nothing for this call to
  // recompute -- not an unimplemented placeholder. `mesh`/`velocity`/
  // `pressure` are accepted (matching TurbulenceModel's own interface
  // shape, which every future concrete model must also satisfy) but
  // deliberately unused and unvalidated here: this model reads none of
  // them, so a mismatched size in an argument it never touches cannot
  // affect its result (mu_t stays exactly what the constructor already
  // established) and there is nothing real to fail-fast against.
  void correct(const cfd::mesh::Mesh& mesh, const cfd::fields::VectorField& velocity,
               const cfd::fields::ScalarField& pressure) override;

 private:
  cfd::fields::ScalarField turbulentViscosity_;
};

}  // namespace cfd::turbulence
