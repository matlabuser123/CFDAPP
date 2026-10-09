import io

p = 'tests/integration/thermal/test_thermal_validation.cpp'
s = io.open(p, encoding='utf-8', newline='').read()
anchor = '// --- Case F: temperature-dependent conductivity -------------------------------'
assert s.count(anchor) == 1

new = r'''// --- Case E: the thermal boundary-condition suite -----------------------------

TEST(ThermalValidation, CaseEFixedHeatFluxGivesTheExactLinearProfile) {
  // Left Dirichlet T0, right prescribed gradient. Steady conduction with a
  // fixed wall gradient g gives T(x) = T0 + g x exactly, so a two-point
  // operator reproduces it to solver tolerance.
  constexpr Index nx = 25;
  constexpr Real length = 1.0;
  constexpr Real t0 = 400.0;
  constexpr Real kValue = 8.0;
  constexpr Real gradient = -30.0;  // K/m, temperature falling to the right

  Mesh mesh = MeshGeometry::createCartesian2D(nx, 1, length, 1.0);
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<FixedTemperature>(t0));
  boundaries.set(mesh, "right", std::make_unique<FixedGradient>(gradient));
  boundaries.set(mesh, "bottom", std::make_unique<cfd::boundary::Adiabatic>());
  boundaries.set(mesh, "top", std::make_unique<cfd::boundary::Adiabatic>());

  const Index n = mesh.numberOfCells();
  const ScalarField T =
      solveToConvergence(mesh, boundaries, ScalarField(n, kValue), ScalarField(n, 1000.0),
                         SurfaceField(mesh.numberOfFaces(), 0.0), 0.0, ScalarField(n, t0));

  const Real error = maxError(mesh, T, [&](Real x) { return t0 + gradient * x; });
  EXPECT_LT(error, 1e-8) << "max error " << error;
}

TEST(ThermalValidation, CaseEAdiabaticWallsLeaveAUniformFieldExact) {
  // Every wall adiabatic, no source: a uniform field must be an EXACT discrete
  // solution. A spurious boundary flux would tilt it.
  Mesh mesh = MeshGeometry::createCartesian2D(12, 4, 1.0, 1.0);
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    boundaries.set(mesh, patch.name(), std::make_unique<cfd::boundary::Adiabatic>());
  }
  const Index n = mesh.numberOfCells();
  const ScalarField uniform(n, 350.0);

  const auto assembly = cfd::thermal::assembleEnergyEquation(
      mesh, uniform, SurfaceField(mesh.numberOfFaces(), 0.0), ScalarField(n, 5.0),
      ScalarField(n, 1000.0), boundaries, 0.0, {});
  Vector state(n, 0.0);
  for (Index i = 0; i < n; ++i) state[i] = uniform[i];
  const Vector residual = assembly.system.matrix().multiply(state);
  for (Index i = 0; i < n; ++i) {
    EXPECT_NEAR(residual[i] - assembly.system.rhs()[i], 0.0, 1e-9)
        << "cell " << i << ": adiabatic walls perturbed a uniform field";
  }
}

TEST(ThermalValidation, CaseERobinMatchesTheSeriesResistanceSolution) {
  // Left Dirichlet T0; right convective to T_inf through h_c.
  //
  // Steady conduction gives T(x) = T0 + C x. Matching the wall flux to the
  // film flux,  -k C = h_c (T0 + C L - T_inf),  yields
  //
  //     C = -h_c (T0 - T_inf) / (k + h_c L)
  //
  // The discrete conductance G = 1/(d/k + 1/h_c) is the exact 1D
  // cell-centre-to-ambient series resistance for a linear profile, so the
  // numerical answer should reproduce this to solver tolerance.
  constexpr Index nx = 25;
  constexpr Real length = 1.0;
  constexpr Real t0 = 500.0;
  constexpr Real tInf = 300.0;
  constexpr Real kValue = 10.0;
  constexpr Real hc = 25.0;

  Mesh mesh = MeshGeometry::createCartesian2D(nx, 1, length, 1.0);
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<FixedTemperature>(t0));
  boundaries.set(mesh, "right", std::make_unique<ConvectiveHeatTransfer>(hc, tInf));
  boundaries.set(mesh, "bottom", std::make_unique<cfd::boundary::Adiabatic>());
  boundaries.set(mesh, "top", std::make_unique<cfd::boundary::Adiabatic>());

  const Index n = mesh.numberOfCells();
  const ScalarField T =
      solveToConvergence(mesh, boundaries, ScalarField(n, kValue), ScalarField(n, 1000.0),
                         SurfaceField(mesh.numberOfFaces(), 0.0), 0.0, ScalarField(n, t0));

  const Real slope = -hc * (t0 - tInf) / (kValue + hc * length);
  const Real error = maxError(mesh, T, [&](Real x) { return t0 + slope * x; });
  EXPECT_LT(error, 1e-7) << "max error " << error << " (analytical slope " << slope << ")";

  for (Index i = 0; i < n; ++i) {
    EXPECT_LE(T[i], t0 + 1e-9) << "cell " << i;
    EXPECT_GE(T[i], tInf - 1e-9) << "cell " << i;
  }
  for (Index i = 1; i < n; ++i) EXPECT_LT(T[i], T[i - 1]) << "cell " << i;
}

TEST(ThermalValidation, CaseERobinLimitsBracketDirichletAndAdiabatic) {
  // Non-vacuity for the coefficient. If the conductance were mishandled --
  // k/d used instead of the series value, say -- these limits would not
  // separate correctly.
  const auto rightEndTemperature = [](Real hc) {
    Mesh mesh = MeshGeometry::createCartesian2D(20, 1, 1.0, 1.0);
    BoundaryConditionSet boundaries;
    boundaries.set(mesh, "left", std::make_unique<FixedTemperature>(500.0));
    boundaries.set(mesh, "right", std::make_unique<ConvectiveHeatTransfer>(hc, 300.0));
    boundaries.set(mesh, "bottom", std::make_unique<cfd::boundary::Adiabatic>());
    boundaries.set(mesh, "top", std::make_unique<cfd::boundary::Adiabatic>());
    const Index n = mesh.numberOfCells();
    const ScalarField T =
        solveToConvergence(mesh, boundaries, ScalarField(n, 10.0), ScalarField(n, 1000.0),
                           SurfaceField(mesh.numberOfFaces(), 0.0), 0.0, ScalarField(n, 500.0));
    return T[n - 1];
  };

  // h_c -> 0: insulated, the slab equilibrates at the hot wall.
  EXPECT_NEAR(rightEndTemperature(1e-8), 500.0, 1e-3);
  // h_c -> large: the wall is pinned near T_inf.
  EXPECT_LT(rightEndTemperature(1e8), 310.0);
  // Monotone in between.
  EXPECT_GT(rightEndTemperature(1.0), rightEndTemperature(100.0));
}

TEST(ThermalValidation, CaseERobinWithVariableConductivity) {
  // G = 1/(d/k + 1/h_c) must use the LOCAL k. With k(T) the boundary cell's
  // conductivity moves as the solution develops, so an implementation that
  // captured k once would drift.
  constexpr Real tInf = 300.0;
  constexpr Real hc = 40.0;
  Mesh mesh = MeshGeometry::createCartesian2D(20, 1, 1.0, 1.0);
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<FixedTemperature>(900.0));
  boundaries.set(mesh, "right", std::make_unique<ConvectiveHeatTransfer>(hc, tInf));
  boundaries.set(mesh, "bottom", std::make_unique<cfd::boundary::Adiabatic>());
  boundaries.set(mesh, "top", std::make_unique<cfd::boundary::Adiabatic>());

  const Index n = mesh.numberOfCells();
  const TemperatureDependentProperty kModel({2.0, 0.01}, 200.0, 1200.0, "k");
  const ScalarField cp(n, 1000.0);
  const SurfaceField noFlow(mesh.numberOfFaces(), 0.0);

  ScalarField T(n, 600.0);
  bool converged = false;
  for (int iteration = 0; iteration < 300; ++iteration) {
    const ScalarField k = kModel.evaluateField(T);
    const auto assembly =
        cfd::thermal::assembleEnergyEquation(mesh, T, noFlow, k, cp, boundaries, 0.0, {});
    const Vector next = solveOnce(assembly.system, n);
    Real change = 0.0;
    for (Index i = 0; i < n; ++i) change = std::max(change, std::abs(next[i] - T[i]));
    for (Index i = 0; i < n; ++i) T[i] = next[i];
    if (change < 1e-10 * 900.0) {
      converged = true;
      break;
    }
  }
  ASSERT_TRUE(converged) << "the k(T) Robin iteration did not converge";

  EXPECT_GT(T[n - 1], tInf);
  EXPECT_LT(T[n - 1], 900.0);
  for (Index i = 1; i < n; ++i) EXPECT_LT(T[i], T[i - 1]) << "cell " << i;

  // Non-vacuity: constant k at the model mid-range must give a measurably
  // different wall temperature, so this case can detect a k(T) regression.
  const ScalarField constantK(n, kModel.evaluate(600.0));
  const ScalarField constantSolution =
      solveToConvergence(mesh, boundaries, constantK, cp, noFlow, 0.0, ScalarField(n, 600.0));
  EXPECT_GT(std::abs(T[n - 1] - constantSolution[n - 1]), 1.0)
      << "k(T) and constant k agree at the Robin wall: this case cannot detect a regression";
}

'''

s = s.replace(anchor, new + anchor, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('Case E added')
