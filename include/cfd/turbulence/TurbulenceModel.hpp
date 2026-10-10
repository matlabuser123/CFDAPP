#pragma once

#include <optional>
#include <string_view>

#include "cfd/core/Types.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/mesh/Mesh.hpp"

namespace cfd::gpu {
// P16 Step 4: a CUDA-free pimpl facade. Only its NAME crosses this header, so a
// CPU-only translation unit including this file never sees a device type.
class GpuSimpleDiscretization;
}  // namespace cfd::gpu

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
// P16: the default turbulent Prandtl number for the Reynolds analogy
// k_t = mu_t c_p / Pr_t.
//
// 0.85 is the standard value for wall-bounded shear flows and is what the
// wall-function architecture in this milestone is built around; it is not a
// tuned quantity and nothing in this codebase fits it. A case may override it
// through physics.json's turbulence block.
//
// It lives HERE, on the turbulence side, rather than in the thermal solver:
// Pr_t exists only because mu_t does, and a thermal component that carried its
// own default could silently disagree with the configured one.
inline constexpr Real kDefaultTurbulentPrandtl = 0.85;

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

  // P16 Step 4 -- can this model be corrected from GPU-RESIDENT state, without
  // reading the host velocity?
  //
  // WHY THIS IS A CAPABILITY AND NOT A MODEL NAME. The resident SIMPLE loop
  // deliberately leaves the host velocity frozen at iteration 0. A model whose
  // correct() reads it would therefore be fed stale data for the whole solve.
  // That is the single fact the old `name() == "laminar"` guard was standing in
  // for -- laminar happened to be immune because its correct() is a no-op.
  //
  // Answering true is a statement about EVERY link in the chain being present
  // and qualified: resident velocity and corrected face flux, device gradient,
  // GPU P_k, compact CPU wall treatment, resident k and epsilon solves, device
  // mu_t and mu_eff, and the GPU wall-face viscosity override. A model that has
  // only some of those must answer false -- there is no partially resident
  // path, and a name comparison could not express the difference.
  //
  // The default is false, so a model added later is excluded until it says
  // otherwise rather than being silently admitted.
  [[nodiscard]] virtual bool supportsResidentGpuCorrection() const noexcept { return false; }

  // The resident correction itself, driven from the discretization's own device
  // state rather than from a host velocity field. Returns false when the model
  // cannot do it, or when the discretization has no resident state yet -- the
  // caller must then fall back to correct(), which is exactly what happens on
  // the first outer iteration, before anything has been uploaded.
  //
  // Default: refuse. A model that does not implement this keeps the
  // per-iteration host path it has today.
  [[nodiscard]] virtual bool correctResidentGpu(const cfd::mesh::Mesh&,
                                                cfd::gpu::GpuSimpleDiscretization&) {
    return false;
  }

  // P16: a per-FACE effective viscosity for wall faces, or nullptr when this
  // model applies no wall treatment.
  //
  // This is how a wall function reaches momentum. The momentum assembly keeps
  // its existing mu*U_P/y_P boundary term and is handed a different mu at that
  // face -- there is no second momentum discretisation, and nothing in the
  // assembly needs to know a wall function exists.
  //
  // Not pure virtual, and nullptr by default, for the same reason
  // convergenceResidual is optional: a model with no wall treatment (Laminar,
  // and k-epsilon with wall functions off) should not be forced to implement a
  // meaningless override, and a caller threading the result straight through
  // provably changes nothing for those models.
  [[nodiscard]] virtual const cfd::fields::SurfaceField* wallViscosity() const { return nullptr; }

  // P16-TWF -- the wall state the THERMAL wall function needs, exposed on the
  // same terms as wallViscosity() above: nullptr by default, non-null only
  // when a model actually ran a wall treatment.
  //
  // Named with a `Field` suffix rather than reusing the concrete models' own
  // wallShearStress()/yPlus() accessors, which return references and would be
  // hidden by a virtual of the same name.
  //
  // These TRANSPORT the model's own wall state. A consumer must not
  // reconstruct u_tau from a velocity secant -- the whole point is that the
  // thermal law is driven by the same wall shear the momentum wall function
  // produced.
  [[nodiscard]] virtual const cfd::fields::ScalarField* wallShearStressField() const {
    return nullptr;
  }
  [[nodiscard]] virtual const cfd::fields::ScalarField* wallYPlusField() const { return nullptr; }
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
