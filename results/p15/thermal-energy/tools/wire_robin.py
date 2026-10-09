import io

p = 'src/thermal/EnergyEquation.cpp'
s = io.open(p, encoding='utf-8', newline='').read()

old = """void assembleThermalBoundaryFaceContribution(const Mesh& mesh, const Face& face, Real conductivity,
                                             const ScalarField& temperature,
                                             const BoundaryConditionSet& temperatureBoundaries,
                                             const cfd::fields::VectorField& gradT,
                                             SparseMatrixBuilder& builder, Vector& rhs) {
  const Index ownerId = face.owner();
  const Real distance = MeshGeometry::distance(mesh.cell(ownerId).centroid(), face.centroid());"""

new = """void assembleThermalBoundaryFaceContribution(const Mesh& mesh, const Face& face, Real conductivity,
                                             const ScalarField& temperature,
                                             const BoundaryConditionSet& temperatureBoundaries,
                                             const cfd::fields::VectorField& gradT,
                                             SparseMatrixBuilder& builder, Vector& rhs) {
  const Index ownerId = face.owner();
  const Real distance = MeshGeometry::distance(mesh.cell(ownerId).centroid(), face.centroid());

  // P15 -- the convective (Robin) wall, handled HERE rather than in
  // boundaryDiffusionContribution because this is the one place with the local
  // conductivity in scope. That is not an accident of layout: the Robin face
  // value depends on k, which is exactly why ConvectiveHeatTransfer cannot
  // express itself through ScalarBoundaryCondition::boundaryValue().
  //
  //   flux into the owner = G A (T_inf - T_P),   G = 1 / (d/k + 1/h_c)
  //   a_P += G A ;  b_P += G A T_inf
  //
  // G is the wall-conduction and film resistances in SERIES. The pure
  // diffusion coefficient k A / d is the h_c -> infinity limit of it, so this
  // branch degenerates to the Dirichlet treatment rather than contradicting it.
  {
    const auto& bc = cfd::boundary::boundaryConditionForFace(mesh, face.id(), temperatureBoundaries);
    if (bc.type() == cfd::boundary::BoundaryConditionType::ConvectiveHeatTransfer) {
      const auto* convective = dynamic_cast<const cfd::boundary::ConvectiveHeatTransfer*>(&bc);
      if (convective == nullptr) {
        throw InvalidArgumentError(
            "EnergyEquation: a ConvectiveHeatTransfer-typed boundary condition is not a "
            "ConvectiveHeatTransfer object");
      }
      const Real faceArea = face.area();
      const Real conductance = convective->conductancePerArea(conductivity, distance) * faceArea;
      builder.add(ownerId, ownerId, conductance);
      rhs[ownerId] += conductance * convective->ambientTemperature();
      return;
    }
  }"""

assert s.count(old) == 1, s.count(old)
s = s.replace(old, new, 1)

if '#include "cfd/boundary/ConvectiveHeatTransfer.hpp"' not in s:
    anchor = '#include "cfd/core/Exception.hpp"'
    assert s.count(anchor) == 1
    s = s.replace(anchor, '#include "cfd/boundary/ConvectiveHeatTransfer.hpp"\n' + anchor, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('Robin wired into the thermal boundary assembly')
