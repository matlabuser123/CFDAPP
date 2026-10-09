"""P15 -- GPU robustness for the convective (Robin) wall.

Two minimal changes. The bool API of encodeBoundaryCondition is PRESERVED: no
reason out-parameter is added, because the plans already own unsupported
diagnostics and already interpolate bc.name() into them.

1. The encoder refuses ConvectiveHeatTransfer BEFORE probing. Its
   boundaryValue() throws by design (the face value needs the local
   conductivity), so probing would propagate an exception out of plan
   construction instead of taking the graceful "not GPU-supported" path.

2. Both plans state the TRUE cause for this condition. Their existing message
   says the condition "is not bitwise-reproducible as a constant, a shift or an
   affine function of phi_P" -- accurate for every previously-unsupported
   condition, but WRONG for Robin: Robin IS affine in T_P. The real reason is
   that its coefficients need a runtime conductivity the plan does not have. A
   reason naming the right condition with the wrong cause sends the reader
   looking in the wrong place.
"""

import io


def patch_encoder():
    path = 'include/cfd/gpu/BoundaryEncoding.hpp'
    text = io.open(path, encoding='utf-8', newline='').read()

    old = ("inline bool encodeBoundaryCondition(const cfd::boundary::ScalarBoundaryCondition& bc,\n"
           "                                    cfd::Real distance, cfd::Real& a, cfd::Real& b,\n"
           "                                    cfd::Index& kind) {\n"
           "  using cfd::Real;")

    new = old + """

  // P15: refuse the convective (Robin) wall BEFORE the probe loop below.
  //
  // ConvectiveHeatTransfer::boundaryValue() throws by design -- the face value
  // depends on the local conductivity, which this interface cannot supply, and
  // returning a plausible wrong number would be consumed silently by every
  // assembler. Probing it here would propagate that exception out of GPU plan
  // construction instead of taking the graceful "not GPU-supported" path every
  // other unsupported condition takes.
  //
  // Note what is NOT the reason: the condition is perfectly affine in T_P, so
  // the encoding below could represent it. What it cannot obtain is the
  // conductivity, which varies per cell and (after P15) with temperature,
  // while the plan is built once from mesh and boundaries alone. See
  // results/p15/thermal-energy/robin_architecture_decision.md.
  if (bc.type() == cfd::boundary::BoundaryConditionType::ConvectiveHeatTransfer) {
    return false;
  }"""

    assert text.count(old) == 1, ('encoder anchor', text.count(old))
    text = text.replace(old, new, 1)
    io.open(path, 'w', encoding='utf-8', newline='').write(text)
    print('encoder: convective wall refused before the throwing probe')


def patch_plan(path, pathword):
    """Insert a type-specific reason immediately before the generic one."""
    text = io.open(path, encoding='utf-8', newline='').read()

    tail = 'mesh rejected for the device ' + pathword + ' path";'
    assert text.count(tail) == 1, (path, 'tail', text.count(tail))

    # The generic message begins at the last `unsupportedReason_ = "face "`.
    marker = 'unsupportedReason_ = "face "'
    start = text.rindex(marker)
    line_start = text.rindex('\n', 0, start) + 1
    indent = text[line_start:start]
    assert indent.strip() == '', ('unexpected indent', repr(indent))

    i = indent
    branch = (
        i + '// P15: name the ACTUAL cause for a convective wall. The generic\n' +
        i + '// message below would say it is not representable as constant /\n' +
        i + '// shift / affine, which is untrue -- Robin is affine in T_P. What\n' +
        i + '// is missing is the runtime conductivity its coefficients need.\n' +
        i + 'if (bc.type() == cfd::boundary::BoundaryConditionType::ConvectiveHeatTransfer) {\n' +
        i + '  unsupportedReason_ =\n' +
        i + '      "face " + std::to_string(f) + ": boundary condition \'" +\n' +
        i + '      std::string(bc.name()) +\n' +
        i + '      "\' requires the runtime thermal conductivity to form its coefficients "\n' +
        i + '      "and is not supported by the current device ' + pathword + ' plan; the "\n' +
        i + '      "CPU thermal assembly handles it";\n' +
        i + '  return false;\n' +
        i + '}\n')

    text = text[:line_start] + branch + text[line_start:]
    io.open(path, 'w', encoding='utf-8', newline='').write(text)
    print('plan reason added:', path)


patch_encoder()
patch_plan('cuda/kernels/DeviceDiffusionPlan.cpp', 'diffusion')
patch_plan('cuda/kernels/DeviceConvectionPlan.cpp', 'convection')
