// P4 -- Performance, section 19/22: OpenMP scaling for the one kernel
// parallelized so far (SparseMatrix::multiply -- see that function's own
// header comment for why it is race- and reduction-order-free across
// rows). Builds one large representative sparse matrix (a real momentum-
// component assembly from a large lid-driven-cavity mesh, not a
// synthetic random matrix) and times repeated SpMV at 1/2/4/8/16/32
// threads via omp_set_num_threads(), reporting speedup(N)=T1/TN and
// efficiency(N)=speedup(N)/N (section 22's own exact formulas).
//
// Only meaningful when built with -DCFDAPP_ENABLE_OPENMP=ON; prints a
// clear message and exits 0 (not a failure) if OpenMP was not compiled
// in, so this stays a safe no-op in the default CPU-only build.
#include <iostream>
#include <memory>
#include <vector>

#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/core/Timer.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/physics/MassFlux.hpp"
#include "cfd/physics/MomentumEquation.hpp"

#ifdef _OPENMP
#include <omp.h>
#endif

using cfd::Index;
using cfd::Real;
using cfd::Vector2;
using cfd::algebra::SparseMatrix;
using cfd::algebra::Vector;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::boundary::MovingWall;
using cfd::boundary::Wall;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::assembleDiffusionContribution;
using cfd::physics::calculateMassFlux;
using cfd::physics::FluidProperties;
using cfd::physics::VelocityComponent;

int main() {
#ifndef _OPENMP
  std::cout << "cfd_benchmark_spmv_scaling: built without OpenMP "
              "(-DCFDAPP_ENABLE_OPENMP=OFF) -- nothing to scale, exiting.\n";
  return 0;
#else
  // A genuinely large representative CSR matrix: one U-momentum
  // diffusion+convection assembly on a 300x300 lid-driven-cavity mesh
  // (90000 unknowns), the same physics::assembleDiffusionContribution/
  // MomentumEquation machinery SIMPLE itself calls every outer
  // iteration -- not a synthetic random-sparsity matrix.
  const Index nx = 300, ny = 300;
  const Mesh mesh = MeshGeometry::createCartesian2D(nx, ny, 1.0, 1.0);
  BoundaryConditionSet velocityBoundaries;
  velocityBoundaries.set(mesh, "left", std::make_unique<Wall>());
  velocityBoundaries.set(mesh, "right", std::make_unique<Wall>());
  velocityBoundaries.set(mesh, "bottom", std::make_unique<Wall>());
  velocityBoundaries.set(mesh, "top", std::make_unique<MovingWall>(Vector2{1.0, 0.0}));
  const Index n = mesh.numberOfCells();
  const VectorField velocity(n, Vector2{0.0, 0.0});
  const FluidProperties fluid(1.0, 0.01);
  const auto massFlux = calculateMassFlux(mesh, velocity, fluid, velocityBoundaries);

  cfd::algebra::SparseMatrixBuilder builder(n, n);
  builder.reserve(5 * n);
  cfd::algebra::Vector rhs(n, 0.0);
  assembleDiffusionContribution(mesh, fluid.dynamicViscosity(), velocity, velocityBoundaries,
                                VelocityComponent::U, builder, rhs);
  const SparseMatrix matrix = builder.build();

  Vector x(n);
  for (Index i = 0; i < n; ++i) x[i] = static_cast<Real>(i % 7) * 0.1;

  constexpr int kRepeats = 30;
  const std::vector<int> threadCounts = {1, 2, 4, 8, 16, 32};

  std::cout << "cfd_benchmark_spmv_scaling: n=" << n << " nnz=" << matrix.nonZeros() << "\n";
  std::cout << "threads | total_seconds(" << kRepeats << " repeats) | speedup | efficiency\n";

  double baselineSeconds = 0.0;
  for (const int threads : threadCounts) {
    omp_set_num_threads(threads);
    cfd::Timer timer;
    for (int r = 0; r < kRepeats; ++r) {
      const Vector y = matrix.multiply(x);
      (void)y;
    }
    const double seconds = timer.elapsedSeconds();
    if (threads == 1) baselineSeconds = seconds;
    const double speedup = baselineSeconds / seconds;
    const double efficiency = speedup / static_cast<double>(threads);
    std::cout << threads << "       | " << seconds << " | " << speedup << " | " << efficiency
              << "\n";
  }

  return 0;
#endif
}
