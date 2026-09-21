#pragma once

// GPU-DISC-001D -- CUDA-only. See DeviceBuffer.hpp's "never include from a
// CPU-only-compiled file" rule.
//
// CUDA port of the production convection machinery:
//
//   * the four face-value schemes (Upwind, Central, LinearUpwind, QUICK) with
//     P12-NUM-001's Sweby/van Leer limiting, reproducing
//     cfd::discretization::convection();
//   * the production SCALAR implicit convection assembly, reproducing
//     thermal::assembleThermalConvectionContribution (both overloads), which is
//     what thermal, turbulence and species actually call.
//
// NOT ported: physics::assembleConvectionContribution, the momentum/vector
// assembly. It is the only production path that SELECTS between the four
// schemes, but it is momentum assembly and it resolves VectorBoundaryCondition
// -- both explicitly out of scope this phase. See
// results/gpu-disc-001/convection/audit.md section 2; the TODO item stays
// unchecked because of it.
//
// Design follows 001B/001C: mesh-dependent quantities are built once on the
// host from the production MeshGeometry functions, and the kernels evaluate
// only what depends on the fields.

#include <string>

#include "cfd/core/Types.hpp"
#include "cfd/gpu/BoundaryEncoding.hpp"
#include "cfd/gpu/DeviceBuffer.hpp"
#include "cfd/gpu/DeviceGradient.hpp"

namespace cfd::mesh {
class Mesh;
}
namespace cfd::boundary {
class BoundaryConditionSet;
}

namespace cfd::gpu {

// Mirrors cfd::discretization::ConvectionScheme, in the same order, so a caller
// can cast between them. Kept as plain constants rather than an enum class so
// the value can be passed to a kernel and compared without a cast.
inline constexpr cfd::Index kConvectionUpwind = 0;
inline constexpr cfd::Index kConvectionCentral = 1;
inline constexpr cfd::Index kConvectionLinearUpwind = 2;
inline constexpr cfd::Index kConvectionQUICK = 3;

// Mirrors physics::VelocityComponent. Defined here rather than in
// DeviceMomentumConvection.hpp so the shared face-term headers can use them
// without a circular include.
inline constexpr cfd::Index kVelocityU = 0;
inline constexpr cfd::Index kVelocityV = 1;
inline constexpr cfd::Index kVelocityW = 2;

// Assembled scalar convection contribution, device-resident. Same candidate-CSR
// contract as DeviceDiffusionSystem: row P holds column P plus every cell
// sharing an internal face with P, ascending, with entries the CPU builder
// would drop as exactly zero retained structurally so the differential can
// check the pattern rather than assume it.
struct DeviceConvectionSystem {
  DeviceBuffer<cfd::Index> rowOffsets;
  DeviceBuffer<cfd::Index> columnIndices;
  DeviceBuffer<cfd::Real> values;
  DeviceBuffer<cfd::Real> rhs;
};

// Defined in DeviceConvectionKernel.cu: the bundle of raw device pointers the
// kernels take. Declared here only so the plan can grant it access to its own
// buffers without making them public.
struct ConvectionPlanView;

class DeviceConvectionPlan {
 public:
  bool build(const cfd::mesh::Mesh& mesh, const cfd::boundary::BoundaryConditionSet& boundaries);

  [[nodiscard]] bool usable() const noexcept { return usable_; }
  [[nodiscard]] const std::string& unsupportedReason() const noexcept { return unsupportedReason_; }
  [[nodiscard]] const DeviceMesh& mesh() const noexcept { return gradient_.mesh(); }
  // Internal faces having a far-upstream cell with the owner upwind / with the
  // neighbour upwind. Reported so a differential run can show QUICK and the
  // limiter were actually reachable rather than degrading everywhere.
  [[nodiscard]] cfd::Index farUpstreamOwnerUpwind() const noexcept { return farOwnerUpwind_; }
  [[nodiscard]] cfd::Index farUpstreamNeighborUpwind() const noexcept { return farNeighborUpwind_; }
  [[nodiscard]] std::size_t residentBytes() const noexcept;

 private:
  friend void fillConvectionPlanView(const DeviceConvectionPlan&, ConvectionPlanView&);
  friend void convectionDevice(const DeviceConvectionPlan&, const DeviceBuffer<cfd::Real>&,
                               const DeviceBuffer<cfd::Real>&, cfd::Index,
                               DeviceBuffer<cfd::Real>&);
  friend void assembleScalarConvectionDevice(const DeviceConvectionPlan&,
                                             const DeviceBuffer<cfd::Real>&,
                                             const DeviceBuffer<cfd::Real>&,
                                             const DeviceBuffer<cfd::Real>*, cfd::Real,
                                             DeviceConvectionSystem&);

  DeviceGradientPlan gradient_;
  bool usable_{false};
  std::string unsupportedReason_;
  cfd::Index farOwnerUpwind_{0};
  cfd::Index farNeighborUpwind_{0};
  cfd::Index cellCount_{0};
  cfd::Index entryCount_{0};

  // Interior interpolation distances. They double as QUICK's hUf/hfD: with the
  // owner upwind hUf = dPf and hfD = dNf, with the neighbour upwind they swap.
  DeviceBuffer<cfd::Real> faceDPf_, faceDNf_;
  // Face-centre offsets for LinearUpwind's Taylor term, one per orientation.
  DeviceBuffer<cfd::Real> offsetOwnerX_, offsetOwnerY_, offsetOwnerZ_;
  DeviceBuffer<cfd::Real> offsetNeighborX_, offsetNeighborY_, offsetNeighborZ_;
  // Far-upstream cell, per orientation. The upwind side is decided by the sign
  // of the mass flux -- a field -- so both must be precomputed.
  DeviceBuffer<cfd::Index> farOwnerValid_, farOwnerCell_;
  DeviceBuffer<cfd::Real> farOwnerHCU_;
  DeviceBuffer<cfd::Index> farNeighborValid_, farNeighborCell_;
  DeviceBuffer<cfd::Real> farNeighborHCU_;
  // Boundary faces: owner distance and the bitwise-verified boundaryValue
  // encoding at that distance.
  DeviceBuffer<cfd::Real> bDistance_;
  DeviceBuffer<cfd::Real> bValueA_, bValueB_;
  DeviceBuffer<cfd::Index> bValueKind_;

  // Assembly gather structure: incident faces sorted by FACE ID (the order the
  // production face loop writes triplets), plus the candidate CSR pattern.
  DeviceBuffer<cfd::Index> cellFaceOffsets_, cellFaceSorted_;
  DeviceBuffer<cfd::Index> rowOffsets_, columnIndices_;

  mutable DeviceBuffer<cfd::Real> gradX_, gradY_, gradZ_;
};

// conv(phi)_P = (1/V_P) * sum_f Ff_cell * phi_f, reproducing
// cfd::discretization::convection(). `scheme` is one of the kConvection*
// constants. `massFlux` is owner-oriented, one entry per face. The result stays
// device-resident.
void convectionDevice(const DeviceConvectionPlan& plan, const DeviceBuffer<cfd::Real>& phi,
                      const DeviceBuffer<cfd::Real>& massFlux, cfd::Index scheme,
                      DeviceBuffer<cfd::Real>& result);

// The production scalar implicit assembly. Pass `specificHeatField = nullptr`
// and a scalar `specificHeat` for the constant-cp overload, or a per-cell field
// for the varying one -- in which case cp is taken at the UPWIND cell, not
// face-interpolated, exactly as the CPU does.
void assembleScalarConvectionDevice(const DeviceConvectionPlan& plan,
                                    const DeviceBuffer<cfd::Real>& phi,
                                    const DeviceBuffer<cfd::Real>& massFlux,
                                    const DeviceBuffer<cfd::Real>* specificHeatField,
                                    cfd::Real specificHeat, DeviceConvectionSystem& system);

// Evaluates the four scheme primitives on caller-supplied inputs, so they can be
// compared against the CPU functions directly rather than only through an
// operator. `which`: 0 quickFaceValue, 1 linearUpwindFaceValue (1D, offset in
// x), 2 smoothnessRatio (NaN encodes nullopt), 3 vanLeerLimiter.
void evaluateConvectionPrimitives(cfd::Index which, const DeviceBuffer<cfd::Real>& a,
                                  const DeviceBuffer<cfd::Real>& b,
                                  const DeviceBuffer<cfd::Real>& c,
                                  const DeviceBuffer<cfd::Real>& d,
                                  const DeviceBuffer<cfd::Real>& e,
                                  const DeviceBuffer<cfd::Real>& f, DeviceBuffer<cfd::Real>& out);

}  // namespace cfd::gpu
