#!/usr/bin/env python3
"""GPU-DISC-001P -- negative controls for the seven OPERATOR gates (001B-001H).

Those gates ran their controls by hand: the mutated/restored harness output was
preserved, but no driver was, so they cannot be re-executed and their mutation
text survives only as prose in each summary.md. Worse, three of them were
applied to code that has since MOVED -- GPU-DISC-001F extracted the diffusion
and convection face terms into shared headers, so the files those controls
named no longer contain the lines they mutated.

This file re-derives every one of them against the CURRENT tree, and adds the
controls the GPU-DISC-wide matrix asks for that no gate ever had:

  P1  gradients -- wrong boundary gradient treatment      (no control existed)
  P2  gradients -- P12-GRAD-002 claim correction dropped  (no control existed)
  P3  gradients -- oblique-Neumann tangential term lost   (no control existed)
  P4  gradients -- owner/neighbour area orientation       (no control existed)
  P5  diffusion -- diagonal sign reversal                 (no control existed)
  P6  diffusion -- off-diagonal omitted                   (no control existed)

Run with --check to verify every anchor still occurs exactly once without
touching anything.
"""

import sys

sys.path.insert(0, "/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp/results/gpu-disc-001/"
                   "negative-controls/tools")

from control_engine import NL, ROOT, execute  # noqa: E402

GRAD = "cuda/kernels/DeviceGradientKernel.cu"
DIFF = "cuda/kernels/DeviceDiffusionKernel.cu"
DIFFT = "include/cfd/gpu/DeviceDiffusionTerms.hpp"
CONV = "cuda/kernels/DeviceConvectionKernel.cu"
MCONV = "cuda/kernels/DeviceMomentumConvectionKernel.cu"
CONVT = "include/cfd/gpu/DeviceConvectionTerms.hpp"
BCH = "include/cfd/gpu/DeviceBoundaryConditions.hpp"
BCC = "cuda/kernels/DeviceBoundaryConditions.cpp"
MASM = "cuda/kernels/DeviceMomentumAssemblyKernel.cu"
MRESP = "cuda/kernels/DeviceMomentumResponseKernel.cu"
FFLUX = "cuda/kernels/DeviceFaceFluxKernel.cu"
CMAKE = "cuda/CMakeLists.txt"

QUICK = {"quick": ["--quick"]}


def c(cid, layer, origin, file, desc, anchor, become, harnesses, expect="detect", args=None,
      first=None):
    """One control. `harnesses` are run in order against the same mutated build."""
    return {"id": cid, "layer": layer, "origin": origin, "file": file, "desc": desc,
            "anchor": anchor, "become": become, "harnesses": harnesses, "expect": expect,
            "args": args or {h: ["--quick"] for h in harnesses}, "first": first}


CONTROLS = [
    # ---------------------------------------------------------------- gradients
    c("b1_interior_weights_swapped", "gradients", "001B NC1", GRAD,
      "the interior face value uses dPf/dNf swapped -- the owner's weight applied "
      "to the neighbour",
      "  faceValues[f] = ((dN * phiP) + (dP * phiN)) / (dP + dN);",
      "  faceValues[f] = ((dP * phiP) + (dN * phiN)) / (dP + dN);",
      ["gradient"]),

    c("b2_boundary_value_ignores_bc", "gradients", "NEW -- no gradient boundary control existed",
      GRAD,
      "WRONG BOUNDARY GRADIENT TREATMENT: the boundary face value ignores the "
      "condition entirely and carries the owner value, i.e. zero-gradient is "
      "assumed on every boundary face whatever the patch says",
      "  faceValues[f] = boundaryValueOf(kind[f], a[f], b[f], phi[faceOwner[f]]);",
      "  faceValues[f] = phi[faceOwner[f]];",
      ["gradient"]),

    c("b3_grad002_claim_correction_dropped", "gradients", "NEW", GRAD,
      "the P12-GRAD-002 boundary-adjacent claim keeps the raw face value and drops "
      "the second-derivative correction -- the defect GRAD-002 exists to fix",
      "  claimValue[k] = faceValues[claimTargetFace[k]] + correction;",
      "  claimValue[k] = faceValues[claimTargetFace[k]];",
      ["gradient"]),

    c("b4_oblique_neumann_tangential_dropped", "gradients", "NEW", GRAD,
      "the P12-MESH-001 oblique-Neumann face drops the tangential gradient term, "
      "so an oblique boundary is treated as if its normal were aligned with d",
      "  faceValues[f] = boundaryValueOf(kind[i], a[i], b[i], phi[o]) +" + NL +
      "                  dotGuarded(gradX[o], gradY[o], gradZ[o], tX[i], tY[i], tZ[i]);",
      "  faceValues[f] = boundaryValueOf(kind[i], a[i], b[i], phi[o]);",
      ["gradient"]),

    c("b5_owner_neighbour_area_orientation", "gradients", "NEW", GRAD,
      "WRONG OWNER/NEIGHBOUR CONTRIBUTION: the Green-Gauss sum negates the area "
      "vector for the faces it OWNS instead of the faces it neighbours",
      "    if (faceOwner[f] != c) {",
      "    if (faceOwner[f] == c) {",
      ["gradient"]),

    # ---------------------------------------------------------------- diffusion
    c("c1_face_gradient_divide", "diffusion", "001C NC1", DIFFT,
      "the interpolated face gradient DIVIDES by (dPf+dNf) instead of multiplying "
      "by its reciprocal -- the scalar/vector interpolateInternalFace overload "
      "confusion, algebraically identical and not bitwise identical",
      "    const cfd::Real fx = ((gx[o] * dN) + (gx[n] * dP)) * inverse;",
      "    const cfd::Real fx = ((gx[o] * dN) + (gx[n] * dP)) / (dP + dN);",
      ["diffusion", "momentum_assembly"]),

    c("c2_ignores_correction_flag", "diffusion", "001C NC2", DIFFT,
      "the internal-face non-orthogonal correction is applied regardless of the "
      "caller's enabled flag",
      "  if (correct && g.decompValid[f] != 0) {",
      "  if (g.decompValid[f] != 0) {",
      ["diffusion", "momentum_assembly"]),

    c("c3_rhs_neighbour_sign", "diffusion", "001C NC3", DIFF,
      "the neighbour row ADDS the explicit non-orthogonal flux instead of "
      "subtracting it, so the two rows of a face no longer cancel",
      "      rhsValue = (p.faceOwner[f] == c) ? (rhsValue + terms.explicitFlux)" + NL +
      "                                       : (rhsValue - terms.explicitFlux);",
      "      rhsValue = (p.faceOwner[f] == c) ? (rhsValue + terms.explicitFlux)" + NL +
      "                                       : (rhsValue + terms.explicitFlux);",
      ["diffusion"]),

    c("c4_diagonal_sign_reversed", "diffusion", "NEW -- no diffusion diagonal control existed",
      DIFF,
      "DIAGONAL SIGN REVERSAL: the internal-face contribution is subtracted from "
      "the diagonal instead of added, destroying diagonal dominance",
      "        if (column == c) {" + NL +
      "          value = value + terms.coefficient;" + NL +
      "        } else if (column == other) {",
      "        if (column == c) {" + NL +
      "          value = value - terms.coefficient;" + NL +
      "        } else if (column == other) {",
      ["diffusion"]),

    c("c5_offdiagonal_omitted", "diffusion", "NEW -- no diffusion off-diagonal control existed",
      DIFF,
      "OMITTED OFF-DIAGONAL: the internal-face off-diagonal coefficient is dropped, "
      "so neighbouring cells stop being coupled at all",
      "        } else if (column == other) {" + NL +
      "          value = value - terms.coefficient;" + NL +
      "        }",
      "        } else if (column == other) {" + NL +
      "          value = value + 0.0;  // MUTATED: the off-diagonal coefficient is dropped." + NL +
      "        }",
      ["diffusion"]),

    # --------------------------------------------------------------- convection
    c("d1_zero_flux_picks_neighbour", "convection", "001D NC1 (null)", CONV,
      "the upwind predicate becomes `> 0.0`, so a face carrying exactly +-0.0 flux "
      "selects the NEIGHBOUR rather than the owner",
      # The one-line form is a substring of the assembly's own deeper-indented
      # copy, so the following line is carried to make the anchor unique.
      "  const bool ownerIsUpwind = ownerFlux >= 0.0;" + NL +
      "  const Index upwind = ownerIsUpwind ? owner : neighbor;",
      "  const bool ownerIsUpwind = ownerFlux > 0.0;" + NL +
      "  const Index upwind = ownerIsUpwind ? owner : neighbor;",
      ["convection"], expect="null"),

    c("d2_reversed_flux_sign", "convection", "001D NC2", CONV,
      "the cell-sum flux sign is reversed: the owner's own faces carry the "
      "neighbour orientation and vice versa",
      "    const Real cellFlux = (p.faceOwner[f] == c) ? ownerFlux : -ownerFlux;",
      "    const Real cellFlux = (p.faceOwner[f] == c) ? -ownerFlux : ownerFlux;",
      ["convection"]),

    c("d3_central_weights_swapped", "convection", "001D NC3", CONV,
      "WRONG INTERPOLATION WEIGHT: the Central scheme applies the owner's distance "
      "weight to the owner value instead of the neighbour's",
      "    phiHighOrder = ((dN * phi[owner]) + (dP * phi[neighbor])) / (dP + dN);",
      "    phiHighOrder = ((dP * phi[owner]) + (dN * phi[neighbor])) / (dP + dN);",
      ["convection"]),

    c("d4_offdiagonal_sign", "convection", "001D NC4", CONV,
      "the assembled off-diagonal A(n,o) takes the opposite sign",
      "        if (c == neighbor && column == owner) value = value - effectiveFlux;",
      "        if (c == neighbor && column == owner) value = value + effectiveFlux;",
      ["convection"]),

    c("d5_rhs_sign", "convection", "001D NC5", CONV,
      "WRONG RHS CONTRIBUTION: the inflow boundary term is added to the RHS "
      "instead of subtracted",
      "    rhsValue = rhsValue - (effectiveFlux * phiB);",
      "    rhsValue = rhsValue + (effectiveFlux * phiB);",
      ["convection"]),

    c("d6_upwind_downwind_swapped", "convection", "001D NC7", CONV,
      "OWNER/DOWNWIND SWAP: the upwind and downwind cells are exchanged on every "
      "internal face, including every non-zero-flux one",
      "  const Index upwind = ownerIsUpwind ? owner : neighbor;" + NL +
      "  const Index downwind = ownerIsUpwind ? neighbor : owner;",
      "  const Index upwind = ownerIsUpwind ? neighbor : owner;" + NL +
      "  const Index downwind = ownerIsUpwind ? owner : neighbor;",
      ["convection"]),

    c("d7_central_divides", "convection", "001D M1", CONVT,
      "the momentum Central face value DIVIDES instead of multiplying by the "
      "reciprocal -- the same overload confusion as c1, in the vector path",
      "    const cfd::Real fxv = ((ux[owner] * dN) + (ux[neighbor] * dP)) * inverse;",
      "    const cfd::Real fxv = ((ux[owner] * dN) + (ux[neighbor] * dP)) / (dP + dN);",
      ["momentum_convection", "momentum_assembly"]),

    c("d8_symmetry_keeps_normal_x", "convection", "001D M2", CONVT,
      "the Symmetry boundary keeps the normal component in x instead of removing it",
      "  if (component == kVelocityU) return ux - (nx * normalComponent);",
      "  if (component == kVelocityU) return ux;",
      ["momentum_convection", "momentum_assembly"]),

    c("d9_outlet_returns_constant_x", "convection", "001D M3", CONVT,
      "the Outlet (identity) boundary returns the stored constant in x instead of "
      "the interior velocity",
      "    return component == kVelocityU ? ux : (component == kVelocityV ? uy : uz);",
      "    return component == kVelocityU ? bc.constX[f] : (component == kVelocityV ? uy : uz);",
      ["momentum_convection", "momentum_assembly"]),

    c("d10_deferred_rhs_same_sign", "convection", "001D M4", MCONV,
      "the deferred-correction RHS is applied with the SAME sign to both rows of a "
      "face, so the correction no longer conserves",
      "    rhsValue = (c == owner) ? (rhsValue - correction) : (rhsValue + correction);",
      "    rhsValue = (c == owner) ? (rhsValue - correction) : (rhsValue - correction);",
      ["momentum_convection"]),

    c("d11_velocity_gradient_divide", "convection", "001D M5", MCONV,
      "the velocity-gradient cell sum DIVIDES by the volume instead of multiplying "
      "by its reciprocal",
      "  gUx[c] = sUx * inverseVolume; gUy[c] = sUy * inverseVolume; gUz[c] = sUz * inverseVolume;",
      "  gUx[c] = sUx / p.cellVolumes[c]; gUy[c] = sUy / p.cellVolumes[c];"
      " gUz[c] = sUz / p.cellVolumes[c];",
      ["momentum_convection"]),

    c("d12_2d_skew_z_carried", "convection", "001D M6 (null)", MCONV,
      "on a 2D mesh the skew-corrected face velocity carries the interpolated z "
      "instead of the exact 0.0 the CPU's Vector2 produces",
      "    fz[f] = 0.0;" + NL +
      "    return;",
      "    fz[f] = vcz;" + NL +
      "    return;",
      ["momentum_convection"], expect="null"),

    c("d13_3d_skew_wrong_gradient", "convection", "001D M7", MCONV,
      "the 3D skew correction transports w with grad(u) instead of grad(w)",
      "  const Real gzx = gWx[o] + ((gWx[n] - gWx[o]) * t);" + NL +
      "  const Real gzy = gWy[o] + ((gWy[n] - gWy[o]) * t);" + NL +
      "  const Real gzz = gWz[o] + ((gWz[n] - gWz[o]) * t);",
      "  const Real gzx = gUx[o] + ((gUx[n] - gUx[o]) * t);" + NL +
      "  const Real gzy = gUy[o] + ((gUy[n] - gUy[o]) * t);" + NL +
      "  const Real gzz = gUz[o] + ((gUz[n] - gUz[o]) * t);",
      ["momentum_convection"]),

    # ------------------------------------------------------- boundary conditions
    c("e1_wrong_type_lookup", "boundary-conditions", "001E B1", BCC,
      "WRONG BC TYPE LOOKUP: the recorded boundary type id is shifted by one, so "
      "every per-face dispatch selects the neighbouring condition",
      "    hostType_[f] = static_cast<Index>(bc.type());",
      "    hostType_[f] = static_cast<Index>(bc.type()) + 1;",
      ["boundary"]),

    c("e2_component_swap", "boundary-conditions", "001E B2", BCH,
      "WRONG VELOCITY COMPONENT: the constant vector condition returns x and y "
      "swapped",
      "    outX = bc.constX[face]; outY = bc.constY[face]; outZ = bc.constZ[face];",
      "    outX = bc.constY[face]; outY = bc.constX[face]; outZ = bc.constZ[face];",
      ["boundary"]),

    c("e3_ghost_formula", "boundary-conditions", "001E B3", BCH,
      "WRONG GHOST-VALUE FORMULA: the ghost mirror drops the factor of two, so the "
      "ghost sits at the boundary value rather than mirrored through it",
      "  return (2.0 * phiB) - ownerValue;",
      "  return phiB - ownerValue;",
      ["boundary"]),

    c("e4_wall_inlet_value_corrupted", "boundary-conditions", "001E B4", BCH,
      "WALL/INLET VALUE CORRUPTION: a constant vector condition returns zero for x, "
      "so a moving wall or an inlet loses its prescribed x velocity",
      "    outX = bc.constX[face]; outY = bc.constY[face]; outZ = bc.constZ[face];",
      "    outX = 0.0; outY = bc.constY[face]; outZ = bc.constZ[face];",
      ["boundary"]),

    c("e5_symmetry_ignores_z", "boundary-conditions", "001E B5", BCH,
      "the symmetry projection ignores the z term -- a 3D-ONLY defect, invisible on "
      "every 2D mesh where n.z is exactly zero anyway",
      "  const cfd::Real normalComponent = zTerm == 0.0 ? inPlane : inPlane + zTerm;",
      "  const cfd::Real normalComponent = zTerm == 0.0 ? inPlane : inPlane;",
      ["boundary"]),

    c("e6_prescribes_inverted", "boundary-conditions", "001E B6", BCH,
      "the prescribesBoundaryValue classification is inverted",
      "  return bc.prescribesValue[face] != 0;",
      "  return bc.prescribesValue[face] == 0;",
      ["boundary"]),

    # ------------------------------------------------------- momentum assembly
    c("f1_boundary_diffusion_diagonal_sign", "momentum-assembly", "001F A1", MASM,
      "the boundary diffusion diagonal contribution takes the opposite sign",
      "        if (column == c) value = value + terms.coefficient;",
      "        if (column == c) value = value - terms.coefficient;",
      ["momentum_assembly"]),

    c("f2_pressure_source_sign", "momentum-assembly", "001F A2", MASM,
      "PRESSURE-GRADIENT SIGN REVERSAL: the momentum pressure source is added "
      "rather than subtracted",
      "  rhsValue = rhsValue + (-p.cellVolumes[c] * pressureGrad[c]);",
      "  rhsValue = rhsValue + (p.cellVolumes[c] * pressureGrad[c]);",
      ["momentum_assembly"]),

    c("f3_relax_rhs_dropped", "momentum-assembly", "001F A3", MASM,
      "MISSING RELAXATION CONTRIBUTION: the implicit under-relaxation RHS term is "
      "dropped while the diagonal term stays, so the relaxed system no longer has "
      "the unrelaxed solution as its fixed point",
      "    rhsValue = rhsValue + (extra * previous[c]);",
      "    rhsValue = rhsValue + (0.0 * (extra * previous[c]));",
      ["momentum_assembly"]),

    c("f4_relax_factor_rewritten", "momentum-assembly", "001F A4", MASM,
      "the relaxation factor is rewritten as (1-alpha)/alpha -- algebraically "
      "identical to (1/alpha)-1 and NOT bitwise identical",
      "    const Real factor = (1.0 / alpha) - 1.0;",
      "    const Real factor = (1.0 - alpha) / alpha;",
      ["momentum_assembly"]),

    c("f5_component_lookup_always_u", "momentum-assembly", "001F A5", MASM,
      "WRONG COMPONENT SOURCE: the boundary velocity always reads the U component, "
      "whatever component is being assembled",
      "        const Real phiB = boundaryComponent(p, f, ux[owner], uy[owner], uz[owner], "
      "component);",
      "        const Real phiB = boundaryComponent(p, f, ux[owner], uy[owner], uz[owner], "
      "kVelocityU);",
      ["momentum_assembly"]),

    c("f6_internal_diffusion_offdiag_dropped", "momentum-assembly", "001F A6", MASM,
      "DROPPED DIFFUSION CONTRIBUTION: the internal diffusion off-diagonal is "
      "omitted from the momentum matrix",
      "      else if (column == other) value = value - terms.coefficient;",
      "      else if (column == other) value = value + 0.0;",
      ["momentum_assembly"]),

    c("f7_boundary_rhs_wrong_coefficient", "momentum-assembly", "001F A7", MASM,
      "the boundary RHS uses the matrix diagonal coefficient instead of the "
      "prescribed-value coefficient -- identical on an orthogonal face, different "
      "wherever the P12-DIFF-002 three-point reconstruction fires",
      "        rhsValue = rhsValue + (terms.boundaryValueCoefficient * phiB);",
      "        rhsValue = rhsValue + (terms.coefficient * phiB);",
      ["momentum_assembly"]),

    # ------------------------------------------------------- momentum response
    c("g1_one_over_ap", "momentum-response", "001G R1", MRESP,
      "OMIT VOLUME: the response coefficient is 1/aP instead of V/aP",
      "  response[c] = cellVolumes[c] / momentumDiagonal[c];",
      "  response[c] = 1.0 / momentumDiagonal[c];",
      ["momentum_response"]),

    c("g2_sign_reversal", "momentum-response", "001G R2", MRESP,
      "SIGN REVERSAL: the response coefficient is negated",
      "  response[c] = cellVolumes[c] / momentumDiagonal[c];",
      "  response[c] = -(cellVolumes[c] / momentumDiagonal[c]);",
      ["momentum_response"]),

    c("g3_inverted", "momentum-response", "001G R3", MRESP,
      "the response coefficient is inverted: aP/V instead of V/aP",
      "  response[c] = cellVolumes[c] / momentumDiagonal[c];",
      "  response[c] = momentumDiagonal[c] / cellVolumes[c];",
      ["momentum_response"]),

    c("g4_indexing_off_by_one", "momentum-response", "001G R4", MRESP,
      "the diagonal is read with an off-by-one index",
      "  response[c] = cellVolumes[c] / momentumDiagonal[c];",
      "  response[c] = cellVolumes[c] / momentumDiagonal[(c + 1) % cellCount];",
      ["momentum_response"]),

    c("g5_stale_unrelaxed_diagonal", "momentum-response", "001G R5", MASM,
      "the assembly reports the UNRELAXED diagonal -- stale relaxation data, which "
      "should fire on exactly the alpha != 1 cases and no others",
      "        values[k] = values[k] + extra;" + NL +
      "        diagonalValue = values[k];" + NL +
      "        break;",
      "        values[k] = values[k] + extra;" + NL +
      "        break;",
      ["momentum_response", "momentum_assembly"]),

    c("g6_row_first_entry", "momentum-response", "001G R6", MASM,
      "WRONG DIAGONAL: the assembly reports the row's first entry instead of the "
      "entry whose column is the row",
      "  diagonal[c] = diagonalValue;",
      "  diagonal[c] = values[p.rowOffsets[c]];",
      ["momentum_response", "momentum_assembly"]),

    # -------------------------------------------------- Rhie-Chow / face flux
    c("h1_rhie_chow_removed", "rhie-chow", "001H F1", FFLUX,
      "REMOVE RHIE-CHOW CORRECTION: the pressure term is zeroed, leaving plain "
      "linear interpolation -- the classical checkerboard-prone mode. The "
      "checkerboard layer of the harness is what must catch this.",
      "  correction[f] = -(coupling / alpha) * compactMinusInterpolated;",
      "  correction[f] = 0.0 * ((coupling / alpha) * compactMinusInterpolated);",
      ["face_flux"]),

    c("h2_gradient_term_added", "rhie-chow", "001H F2", FFLUX,
      "REVERSE PRESSURE-GRADIENT SIGN: the interpolated gradient term is added to "
      "the compact pressure difference instead of subtracted, so the correction "
      "stops being the difference between the compact and wide stencils",
      "      (pressure[neighbor] - pressure[owner]) -" + NL +
      "      fluxDotGuarded(gx, gy, gz, p.dX[f], p.dY[f], p.dZ[f]);",
      "      (pressure[neighbor] - pressure[owner]) +" + NL +
      "      fluxDotGuarded(gx, gy, gz, p.dX[f], p.dY[f], p.dZ[f]);",
      ["face_flux"]),

    c("h3_owner_response_only", "rhie-chow", "001H F3", FFLUX,
      "WRONG RESPONSE INTERPOLATION: the general coupling branch uses the owner's "
      "response coefficient instead of interpolating to the face",
      "    du = ((dN * dU[owner]) + (dP * dU[neighbor])) / (dP + dN);",
      "    du = dU[owner];",
      ["face_flux"]),

    c("h4_swapped_weights", "rhie-chow", "001H F4", FFLUX,
      "the owner/neighbour interpolation weights are swapped in the response "
      "interpolation",
      "    dv = ((dN * dV[owner]) + (dP * dV[neighbor])) / (dP + dN);",
      "    dv = ((dP * dV[owner]) + (dN * dV[neighbor])) / (dP + dN);",
      ["face_flux"]),

    c("h5_area_vector_sign", "rhie-chow", "001H F5", FFLUX,
      "AREA-VECTOR SIGN REVERSAL: the face area vector is negated, reversing every "
      "predicted face flux",
      "  massFlux[f] = density * fluxDotGuarded(vx, vy, vz, p.areaX[f], p.areaY[f], p.areaZ[f]);",
      "  massFlux[f] = density * fluxDotGuarded(vx, vy, vz, -p.areaX[f], -p.areaY[f], "
      "-p.areaZ[f]);",
      ["face_flux"]),

    c("h6_density_omitted", "rhie-chow", "001H F6", FFLUX,
      "the density factor is omitted from the general coupling branch, so the "
      "coupling and the mass flux no longer share one density",
      "  return density * magnitudeOf(rx, ry, rz) / distance;",
      "  return magnitudeOf(rx, ry, rz) / distance;",
      ["face_flux"]),

    c("h7_boundary_raw_owner_velocity", "rhie-chow", "001H F7", FFLUX,
      "a boundary face uses the raw owner velocity instead of the boundary "
      "condition's value",
      "    vx = deviceBoundaryVelocityComponent(p.boundary, f, ux[owner], uy[owner], uz[owner]," +
      NL + "                                         kVelocityU);",
      "    vx = ux[owner];",
      ["face_flux"]),

    # The axis-aligned branch does not exist on either --quick mesh, so this one
    # needs the FULL differential. That is a coverage property of quick mode, not
    # a property of the mutation.
    c("h8_axis_aligned_vector_form", "rhie-chow", "001H F8", FFLUX,
      "the axis-aligned response interpolation uses the VECTOR overload form "
      "(multiply by the reciprocal) where the CPU uses the SCALAR one (divide)",
      "    const Real dFace = ((dN * response[owner]) + (dP * response[neighbor])) / (dP + dN);",
      "    const Real dFace = ((response[owner] * dN) + (response[neighbor] * dP)) * "
      "(1.0 / (dP + dN));",
      ["face_flux"], args={"face_flux": []}),

    # ------------------------------------------------------------ build flags
    # One control for the whole -fmad=false policy, run against three harnesses
    # on the same mutated build. 001B NC2, 001C NC4 and 001D NC6 each toggled it
    # for one kernel; the property they were all testing is this one.
    c("x1_fmad_contraction_enabled", "build-flags", "001B NC2 / 001C NC4 / 001D NC6", CMAKE,
      "-fmad=false is removed from every discretization kernel, so nvcc is free to "
      "contract a*b+c into a single fused multiply-add. The CPU builds for baseline "
      "x86-64 and rounds twice, so the bitwise gate MUST depend on this flag.",
      '  PROPERTIES COMPILE_OPTIONS "-fmad=false"',
      '  PROPERTIES COMPILE_OPTIONS "-fmad=true"',
      ["gradient", "diffusion", "convection"]),
]


def check():
    bad = 0
    for control in CONTROLS:
        source = (ROOT / control["file"]).read_text(encoding="utf-8")
        n = source.count(control["anchor"])
        if n != 1:
            bad += 1
            print(f"  ANCHOR x{n}  {control['id']:46s} {control['file']}")
            print(f"    {control['anchor'].splitlines()[0]!r}")
    print(f"\n{len(CONTROLS)} controls, {bad} bad anchors")
    print(f"observable: {sum(1 for c in CONTROLS if c['expect'] == 'detect')}")
    print(f"null:       {sum(1 for c in CONTROLS if c['expect'] == 'null')}")
    return 1 if bad else 0


if __name__ == "__main__":
    if "--check" in sys.argv:
        raise SystemExit(check())
    only = None
    if "--only" in sys.argv:
        only = sys.argv[sys.argv.index("--only") + 1].split(",")
    selected = [c for c in CONTROLS if only is None or c["id"] in only or c["layer"] in only]
    ok, _ = execute(selected, "operators", "GPU-DISC-001 operator-layer negative controls")
    raise SystemExit(0 if ok else 1)
