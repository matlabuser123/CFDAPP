import io
p='tests/unit/transport/test_scalar_transport.cpp'
s=io.open(p,encoding='utf-8',newline='').read()
anchor = "// ---------------------------------------------------------------------------\n// DETERMINISM"
assert s.count(anchor)==1
new = '''TEST(TransportProperties, HarmonicFaceDiffusivityMatchesTheClosedForm) {
  // Gamma_f = d_PN / (d_Pf/Gamma_P + d_fN/Gamma_N), which at a midpoint face is
  // 2 GP GN / (GP + GN). On a uniform Cartesian mesh every internal face IS a
  // midpoint face, so the closed form is the exact expected value.
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 1, 4.0, 1.0);
  ScalarField gamma(mesh.numberOfCells(), 0.0);
  for (Index i = 0; i < mesh.numberOfCells(); ++i) gamma[i] = 1.0 + 9.0 * static_cast<Real>(i);

  for (Index f = 0; f < mesh.numberOfFaces(); ++f) {
    const auto& face = mesh.face(f);
    if (face.isBoundary()) continue;
    const Real gP = gamma[face.owner()];
    const Real gN = gamma[*face.neighbor()];
    const Real harmonic = cfd::transport::faceDiffusivity(
        mesh, face, gamma, cfd::transport::FaceDiffusivityInterpolation::Harmonic);
    const Real arithmetic = cfd::transport::faceDiffusivity(
        mesh, face, gamma, cfd::transport::FaceDiffusivityInterpolation::Arithmetic);
    EXPECT_NEAR(harmonic, 2.0 * gP * gN / (gP + gN), 1e-12) << "face " << f;
    EXPECT_NEAR(arithmetic, 0.5 * (gP + gN), 1e-12) << "face " << f;
    // The two must genuinely differ for an unequal pair, or this proves nothing.
    EXPECT_LT(harmonic, arithmetic) << "harmonic mean is not below arithmetic at face " << f;
  }
}

TEST(TransportProperties, HarmonicIsNotTheDefault) {
  // The default must stay arithmetic: switching it would move every existing
  // variable-diffusivity result.
  const Mesh mesh = MeshGeometry::createCartesian2D(3, 1, 3.0, 1.0);
  ScalarField gamma(mesh.numberOfCells(), 0.0);
  for (Index i = 0; i < mesh.numberOfCells(); ++i) gamma[i] = 1.0 + 50.0 * static_cast<Real>(i);
  const ScalarField phi(mesh.numberOfCells(), 1.0);
  const SurfaceField noFlux(mesh.numberOfFaces(), 0.0);
  const ScalarField zero(mesh.numberOfCells(), 0.0);

  const auto byDefault = cfd::transport::assembleScalarTransportEquation(
      mesh, phi, noFlux, gamma, fixedValueEverywhere(mesh, 0.0), zero, zero);
  const auto explicitArithmetic = cfd::transport::assembleScalarTransportEquation(
      mesh, phi, noFlux, gamma, fixedValueEverywhere(mesh, 0.0), zero, zero, {}, {},
      cfd::transport::FaceDiffusivityInterpolation::Arithmetic);
  const auto harmonic = cfd::transport::assembleScalarTransportEquation(
      mesh, phi, noFlux, gamma, fixedValueEverywhere(mesh, 0.0), zero, zero, {}, {},
      cfd::transport::FaceDiffusivityInterpolation::Harmonic);

  expectBitwiseIdentical(byDefault.system, explicitArithmetic.system);
  bool differs = false;
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    if (byDefault.diagonal[i] != harmonic.diagonal[i]) differs = true;
  }
  EXPECT_TRUE(differs) << "harmonic produced the arithmetic matrix -- the option does nothing";
}

''' + anchor
s=s.replace(anchor,new,1)
io.open(p,'w',encoding='utf-8',newline='').write(s)
print('harmonic tests added')
