"""P15 -- the Robin / gradient gap, fixed.

WHAT THE FIRST VALIDATION RUN FOUND
-----------------------------------
assembleThermalBoundaryFaceContribution intercepts ConvectiveHeatTransfer and
computes G = 1/(d/k + 1/h_c) from the face's own k. But the correction gradient
is built BEFORE that face loop, and discretization::gradient() evaluates
boundaryValue() on EVERY boundary face. Robin's throws by design, so every
Case E test died with that exception before reaching any physics.

THIS REVERSES AN EARLIER DECISION OF MINE, AND THE REASON MATTERS
-----------------------------------------------------------------
I originally made boundaryValue() throw, arguing that returning a plausible
wrong number would be "consumed silently by every generic assembler ... the
worst possible failure mode". That argument was right about the GENERIC path
and wrong about the gradient:

  * For the ASSEMBLY, the throw is unnecessary -- the thermal path intercepts
    by type before ever calling boundaryValue, so it never sees the value.
  * For the GRADIENT, the throw is fatal and the "wrong number" concern does
    not apply: the gradient feeds only the NON-ORTHOGONAL CORRECTION, which is
    exactly zero on an orthogonal face. Treating a convective wall as
    zero-gradient there has literally no effect on an orthogonal mesh, and is a
    small perturbation of a correction term on a non-orthogonal one.

So boundaryValue now returns the owner value -- the zero-gradient equivalent --
and the loud failure the generic path needs is moved to where it belongs: an
explicit rejection in cfd::transport's diffusion assembly. That is ONE place,
it names the condition, and it cannot be reached accidentally.

Net effect: thermal works, the gradient works, and the P14 generic path still
refuses a Robin wall with a clear message rather than solving the wrong
problem quietly.
"""

import io

# --- 1. boundaryValue returns the zero-gradient equivalent -------------------

p = 'src/boundary/ConvectiveHeatTransfer.cpp'
s = io.open(p, encoding='utf-8', newline='').read()

old = """Real ConvectiveHeatTransfer::boundaryValue(Real, Real) const {
  // Deliberate. See the header: the face value needs the local conductivity,
  // which this interface does not supply. Returning a plausible wrong number
  // would be consumed silently by every generic assembler.
  throw InvalidArgumentError(
      "ConvectiveHeatTransfer::boundaryValue: a convective (Robin) wall has no face value "
      "independent of the local conductivity. Use wallTemperature(ownerValue, k, d), which the "
      "thermal assembly calls. The generic scalar-transport path does not support this "
      "condition.");
}"""

new = """Real ConvectiveHeatTransfer::boundaryValue(Real ownerValue, Real) const {
  // Returns the ZERO-GRADIENT equivalent, and the only caller that reaches it
  // is the gradient operator.
  //
  // This threw in the first implementation, on the argument that a plausible
  // wrong value would be consumed silently. That argument holds for the
  // generic ASSEMBLY path -- and the rejection for it now lives in
  // cfd::transport, which is the one place it can be reached -- but it does
  // not hold here:
  //
  //   * the thermal assembly intercepts this condition BY TYPE before ever
  //     calling boundaryValue, so the assembled coefficients never use it;
  //   * discretization::gradient() does call it, for every boundary face,
  //     when building the correction gradient. Throwing there killed every
  //     convective case before any physics ran -- found by the first run of
  //     the thermal validation suite;
  //   * that gradient feeds ONLY the non-orthogonal correction, which is
  //     exactly zero on an orthogonal face. On an orthogonal mesh this value
  //     has no effect at all; on a non-orthogonal one it is a small
  //     perturbation of a correction term.
  //
  // The physically meaningful coefficient is still conductancePerArea() /
  // wallTemperature(), which take k explicitly. Nothing computes a Robin flux
  // from this return value.
  return ownerValue;
}"""

assert s.count(old) == 1, ('boundaryValue', s.count(old))
s = s.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ConvectiveHeatTransfer::boundaryValue -> zero-gradient equivalent')

# --- 2. the loud rejection moves to the generic transport layer --------------

p = 'src/transport/ScalarTransport.cpp'
s = io.open(p, encoding='utf-8', newline='').read()

old = """      const auto& bc = cfd::boundary::boundaryConditionForFace(mesh, face.id(), boundaries);
      const auto* scalarBc = dynamic_cast<const cfd::boundary::ScalarBoundaryCondition*>(&bc);
      if (scalarBc == nullptr) {
        throw InvalidArgumentError(
            "assembleScalarDiffusionContribution: boundary condition is not scalar-valued");
      }"""

new = """      const auto& bc = cfd::boundary::boundaryConditionForFace(mesh, face.id(), boundaries);
      const auto* scalarBc = dynamic_cast<const cfd::boundary::ScalarBoundaryCondition*>(&bc);
      if (scalarBc == nullptr) {
        throw InvalidArgumentError(
            "assembleScalarDiffusionContribution: boundary condition is not scalar-valued");
      }
      // P15: the generic path cannot form a convective (Robin) coefficient.
      //
      //   G = 1 / (d/k + 1/h_c)
      //
      // needs the runtime conductivity. This layer has Gamma, but the boundary
      // query that would consume it (ScalarBoundaryCondition::boundaryValue)
      // is not given it -- changing that signature touches every scalar and
      // vector BC and is deliberately outside P15 (Option B; see
      // results/p15/thermal-energy/robin_architecture_decision.md).
      //
      // Refused loudly HERE rather than by making boundaryValue() throw,
      // because that also broke the gradient operator, which legitimately
      // needs a value and for which zero-gradient is a sound approximation.
      // This is the one place a generic solve could silently treat a
      // convective wall as adiabatic, so this is where it is stopped.
      if (bc.type() == cfd::boundary::BoundaryConditionType::ConvectiveHeatTransfer) {
        throw InvalidArgumentError(
            "assembleScalarDiffusionContribution: a ConvectiveHeatTransfer (Robin) boundary "
            "requires the runtime diffusivity to form its coefficient and is not supported by "
            "the generic scalar transport path; cfd::thermal's energy assembly handles it");
      }"""

assert s.count(old) == 1, ('transport rejection', s.count(old))
s = s.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('cfd::transport now rejects a Robin wall explicitly')
