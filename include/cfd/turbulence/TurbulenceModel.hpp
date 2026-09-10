#pragma once

#include <optional>
#include <string_view>

#include "cfd/core/Types.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/mesh/Mesh.hpp"

namespace cfd::turbulence {

// P2-TURB-001: the abstraction the pressure-velocity solvers will
// eventually query for a turbulence-model-provided effective viscosity,
// without SIMPLE/PISO/MomentumEquation containing any k-epsilon/k-omega/
// SST-specific formula themselves (TODO.md P2 -- Turbulence). This task
// only establishes the interface: no concrete RANS model exists yet
// (k/epsilon/omega transport, wall distance, y+, production/dissipation,
// blending functions, and wall functions are all later tasks --
// P2-TURB-002 onward), and neither SIMPLE nor PISO nor MomentumEquation
// is wired to call this yet -- their laminar behavior stays bit-
// identical to before this task.
//
// Units: every viscosity here is *dynamic* viscosity (Pa*s = kg/(m*s)),
// the same convention physics::FluidProperties::dynamicViscosity()
// already uses -- never kinematic viscosity (nu = mu/rho,
// FluidProperties::kinematicViscosity()). A concrete model that computes
// internally in kinematic terms (common for RANS formulations) must
// convert back to dynamic before returning anything through this
// interface, so callers never need to know or ask which convention a
// given model used internally.
//
// Ownership: physics::FluidProperties continues to own rho and the
// molecular (laminar) mu exactly as it already does for every existing
// P0/P1/P2 phase -- a TurbulenceModel is never handed one to mutate, and
// never stores a duplicate molecular viscosity of its own. It owns only
// mu_t (and, in a later task, whatever model-internal transport fields a
// concrete RANS model needs); effectiveViscosity() below combines the
// two without either side needing to know about the other's storage.
class TurbulenceModel {
 public:
  virtual ~TurbulenceModel() = default;

  // A short, stable label (e.g. "kEpsilon", "kOmegaSST" once those exist)
  // -- the exact model identity, matching this codebase's established
  // "the exact enum/type name, never a second invented vocabulary"
  // convention (see e.g. pressure_velocity::SIMPLEStatus's own naming in
  // JSONWriter.cpp), applied here to a model name instead of a status.
  [[nodiscard]] virtual std::string_view name() const noexcept = 0;

  // Turbulent (eddy) dynamic viscosity mu_t, one value per mesh cell.
  // Zero is a valid value everywhere (e.g. before a model has been
  // corrected even once, or in a genuinely laminar region a low-Re model
  // predicts) -- concrete implementations must validate whatever field
  // they expose here with validateTurbulentViscosityField() below
  // (non-finite or negative values are never valid and must never be
  // silently clamped).
  [[nodiscard]] virtual const cfd::fields::ScalarField& turbulentViscosity() const = 0;

  // mu_eff = mu + mu_t (both dynamic viscosity -- see this class's own
  // header comment on units). Deliberately NOT virtual: this formula is
  // identical for every RANS model there will ever be, so making it pure
  // virtual would only force k-epsilon/k-omega/SST to each reimplement
  // the same one-line sum -- a single concrete method here, built from
  // turbulentViscosity(), is the actual minimal interface (this
  // codebase's own "keep the interface minimal" guidance for this task).
  // Throws InvalidArgumentError if molecularViscosity is not finite and
  // > 0 (matches physics::FluidProperties's own validation of the same
  // quantity).
  [[nodiscard]] cfd::fields::ScalarField effectiveViscosity(Real molecularViscosity) const;

  // Recomputes mu_t (and, in a later task, any model-internal transport
  // fields -- none exist yet, see this file's own header comment) from
  // the current flow state. A model that has never been corrected yet is
  // expected to already expose a valid turbulentViscosity() (e.g. zero,
  // or a documented initial guess) -- establishing that invariant is a
  // concrete model's constructor's responsibility, not something callers
  // must arrange by calling correct() first.
  virtual void correct(const cfd::mesh::Mesh& mesh, const cfd::fields::VectorField& velocity,
                       const cfd::fields::ScalarField& pressure) = 0;

  // P2-TURB-004 section 23-24: an optional, model-reported measure of how
  // much this model's own internal state changed during its *last*
  // correct() call -- e.g. KEpsilonModel returns the largest absolute
  // change in k or epsilon across all cells; a model with no internal
  // transport state of its own (LaminarModel, or any future model that
  // does not override this) returns std::nullopt, meaning "nothing to
  // gate outer convergence on". Deliberately NOT pure virtual (unlike
  // correct()): adding a genuinely new per-model quantity here should
  // never force every existing and future trivial model to implement a
  // meaningless override, the same reasoning behind effectiveViscosity()
  // not being pure virtual either, just inverted (that one is shared and
  // fixed; this one is model-specific and optional). SIMPLE checks this
  // after every outer iteration (SIMPLESettings::turbulenceTolerance) so
  // that U/V/P settling while k/epsilon are still visibly changing is
  // never reported as a converged RANS solve.
  [[nodiscard]] virtual std::optional<Real> convergenceResidual() const { return std::nullopt; }
};

// Throws InvalidArgumentError if turbulentViscosity.size() !=
// mesh.numberOfCells(), or if any entry is not finite or is negative
// (zero is valid -- see TurbulenceModel::turbulentViscosity()'s own
// comment). Concrete models are expected to call this on any field they
// are about to expose through turbulentViscosity() -- centralized here,
// the same way this codebase's other validated value types (e.g.
// physics::FluidProperties, thermal::ThermalProperties) validate at the
// one point invalid data could enter, so every current and future model
// shares one policy instead of each reimplementing it.
void validateTurbulentViscosityField(const cfd::mesh::Mesh& mesh,
                                     const cfd::fields::ScalarField& turbulentViscosity);

}  // namespace cfd::turbulence
