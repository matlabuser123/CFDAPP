#pragma once

// P16 Step 4 -- test-only injection of a mature turbulence state.
//
// WHAT THIS EXISTS FOR
// --------------------
// The CPU/GPU implementation-equivalence gate needs ONE outer iteration run
// from an identical mature state on both backends. Two independently evolved
// 600-iteration trajectories cannot answer that question: they differ by
// accumulated nonlinear sensitivity, which the divergence-onset sweep showed
// starts at 2.1e-15 for k at iteration 1 and is therefore not an injected
// error. Only a same-state one-step map isolates the implementation.
//
// A mature state cannot be reached by construction -- KEpsilonModel's only
// constructor takes uniform scalars.
//
// WHAT IT IS NOT
// --------------
// It is NOT a step toward turbulence restart. P13-001 refuses checkpoint and
// resume of k, epsilon/omega and mu_t because restoring them correctly is
// unsolved, and that refusal must stay reachable-proof: no case file, no
// checkpoint, no CLI and no GUI may reach this.
//
// The enforcement is structural rather than by convention. KEpsilonModel
// declares this class a friend but the class is DEFINED only here, in a header
// compiled solely into the GPU test binary. A production translation unit that
// tried to call it would not link. There is no public setter to misuse.
#include "cfd/fields/ScalarField.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/turbulence/KEpsilonModel.hpp"

namespace cfd::turbulence::testing {

class KEpsilonModelTestAccess {
 public:
  // Replaces the model's turbulence state, host and device together.
  //
  // RE-SEEDING THE DEVICE IS NOT OPTIONAL. gpuCorrection_.prepare() copies the
  // fields it is given into the device state, and it runs once, in the
  // constructor. Writing only the host members would leave a GPU model
  // correcting from the constructor's uniform values while reporting the
  // injected ones -- the two arms would then start from different states and
  // the experiment would measure nothing.
  //
  // prepare() resets and rebuilds, so calling it again is legal and is how the
  // device state is re-seeded without duplicating any GPU initialisation logic
  // here.
  static void injectState(KEpsilonModel& model, const cfd::mesh::Mesh& mesh,
                          const cfd::fields::ScalarField& k,
                          const cfd::fields::ScalarField& epsilon,
                          const cfd::fields::ScalarField& turbulentViscosity) {
    model.k_ = k;
    model.epsilon_ = epsilon;
    model.turbulentViscosity_ = turbulentViscosity;
    if (model.gpuActive_) {
      model.gpuActive_ =
          model.gpuCorrection_.prepare(mesh, model.kBoundaries_, model.epsilonBoundaries_, model.k_,
                                       model.epsilon_, model.turbulentViscosity_);
    }
  }

  // True when the model's GPU path is still live after injection -- a caller
  // must not silently compare a GPU arm that quietly fell back.
  [[nodiscard]] static bool gpuActive(const KEpsilonModel& model) noexcept {
    return model.gpuActive_;
  }
};

}  // namespace cfd::turbulence::testing
