"""P15 GPU gate -- criterion 5: the refusal must name the RUNTIME-CONDUCTIVITY cause.

WHAT THE GATE FOUND
-------------------
Criteria 3 and 4 pass: a convective wall makes DeviceDiffusionPlan and
DeviceConvectionPlan return false without throwing. Criterion 5 failed. The
reason both plans reported was:

  "gradient plan unusable: face 4: boundary condition 'ConvectiveHeatTransfer'
   is not bitwise-reproducible as a constant, a shift or an affine function of
   phi_P; mesh rejected for the device gradient path"

Two things wrong with that, and only the first is what the gate asked about:

  1. it does not name the cause -- the runtime conductivity;
  2. it states something UNTRUE. A Robin wall IS affine in T_P. The encoder
     could represent that form perfectly well. What it cannot obtain is k,
     which varies per cell and (after P15) with temperature, while the plan is
     built once from mesh and boundaries alone.

The type-specific reason I had already added to DeviceDiffusionPlan and
DeviceConvectionPlan was never reached: the GRADIENT plan is built first and
its own generic message shadowed both.

THE FIX: ONE PLACE, NOT A FOURTH COPY
-------------------------------------
Patching the gradient plan too would make three hand-copied versions of the
same six lines -- and BoundaryEncoding.hpp's own header comment is explicit
about why that is the wrong move ("Moved, not copied: two copies of this would
drift"). The refusal reason belongs next to the function that produces the
refusal, so every plan that calls encodeBoundaryCondition reports the same
cause without having to know about it.

So: one helper in BoundaryEncoding.hpp, used at all four scalar rejection
sites. For every condition OTHER than Robin the message is reproduced byte for
byte, including each site's own noun ("boundary condition" vs "scalar
condition") and its own path clause -- so nothing that currently reports a
rejection changes what it says. Verified: no test pins any of these strings,
and the byte-identical reconstruction is what keeps it that way.

The two VECTOR sites (DeviceBoundaryConditions' vector branch,
DeviceMomentumConvectionPlan) are deliberately left alone: their wording is
different ("the identity or the ..."), and ConvectiveHeatTransfer is a scalar
condition that cannot reach them.
"""

import io


def read(path):
    return io.open(path, encoding='utf-8', newline='').read()


def write(path, text):
    io.open(path, 'w', encoding='utf-8', newline='').write(text)


# --- 1. the helper, beside the encoder that produces the refusal ------------

P = 'include/cfd/gpu/BoundaryEncoding.hpp'
s = read(P)

if '#include <string>' not in s:
    old = '#include <cstring>\n'
    assert s.count(old) == 1, 'encoding cstring include'
    s = s.replace(old, '#include <cstring>\n#include <string>\n', 1)

if 'unsupportedEncodingReason' not in s:
    anchor = 'inline bool encodeBoundaryCondition('
    assert s.count(anchor) == 1, 'encoding fn anchor'
    helper = '''// Why `encodeBoundaryCondition` below refused `bc`, phrased for the plan that
// asked. `pathName` is that plan's own word ("diffusion", "gradient",
// "convection"), or nullptr for a plan that does not name one.
//
// One place rather than one per plan. A device plan calls the encoder and, on
// false, reports this -- it does not need to know which conditions have a more
// specific cause than "not representable", and cannot drift from the others
// when one is added. The P15 gate found exactly that drift: two plans had been
// taught to name the convective wall's real cause and a third had not, and the
// third ran first, so its generic message was the only one anyone ever saw.
[[nodiscard]] inline std::string unsupportedEncodingReason(
    cfd::Index faceIndex, const cfd::boundary::BoundaryCondition& bc, const char* noun,
    const char* pathName) {
  const std::string face = "face " + std::to_string(faceIndex) + ": " + noun + " '" +
                           std::string(bc.name()) + "'";

  // The convective wall is NOT "not representable" -- it is affine in T_P, and
  // the encoding below could hold it. What the plan cannot obtain is the
  // conductivity in G = 1/(d/k + 1/h_c): it varies per cell and with
  // temperature, while a plan is built once from mesh and boundaries alone.
  // Saying so is the difference between a user knowing to run this case on the
  // CPU and a user believing their boundary condition is malformed.
  if (bc.type() == cfd::boundary::BoundaryConditionType::ConvectiveHeatTransfer) {
    return face +
           " requires the runtime thermal conductivity to form its coefficients and is not "
           "supported by the current device " +
           (pathName != nullptr ? std::string(pathName) + " plan" : std::string("boundary plan")) +
           "; the CPU thermal assembly handles it";
  }

  return face +
         " is not bitwise-reproducible as a constant, a shift or an affine function of phi_P" +
         (pathName != nullptr
              ? "; mesh rejected for the device " + std::string(pathName) + " path"
              : std::string());
}

'''
    s = s.replace(anchor, helper + anchor, 1)
write(P, s)
print('1. unsupportedEncodingReason added')

# --- 2. every scalar rejection site uses it ---------------------------------

SITES = [
    # (file, the exact block to replace, the call that replaces it)
    ('cuda/kernels/DeviceGradientPlan.cpp',
     '''      unsupportedReason_ = "face " + std::to_string(f) + ": boundary condition '" +
                           std::string(bc.name()) +
                           "' is not bitwise-reproducible as a constant, a shift or an affine "
                           "function of phi_P; mesh rejected for the device gradient path";
      return false;''',
     '''      unsupportedReason_ =
          cfd::gpu::unsupportedEncodingReason(f, bc, "boundary condition", "gradient");
      return false;'''),

    ('cuda/kernels/DeviceBoundaryConditions.cpp',
     '''        unsupportedReason_ = "face " + std::to_string(f) + ": scalar condition '" +
                             std::string(bc.name()) +
                             "' is not bitwise-reproducible as a constant, a shift or an affine "
                             "function of phi_P";
        return false;''',
     '''        unsupportedReason_ =
            cfd::gpu::unsupportedEncodingReason(f, bc, "scalar condition", nullptr);
        return false;'''),
]

for path, old, new in SITES:
    s = read(path)
    if 'unsupportedEncodingReason' in s:
        print('   (already done) ' + path)
        continue
    assert s.count(old) == 1, (path, s.count(old))
    write(path, s.replace(old, new, 1))
    print('2. ' + path)

# --- 3. the two plans that already had a hand-written branch ----------------
#
# Their special-case block AND their generic block both collapse into the one
# call, so the duplicated knowledge disappears rather than being left behind.

DUPES = [
    ('cuda/kernels/DeviceDiffusionPlan.cpp', 'diffusion', '      ', '    '),
    ('cuda/kernels/DeviceConvectionPlan.cpp', 'convection', '        ', '      '),
]

for path, pathName, indent, outerIndent in DUPES:
    s = read(path)
    if 'unsupportedEncodingReason' in s:
        print('   (already done) ' + path)
        continue
    start = s.index(indent + '// P15: name the ACTUAL cause for a convective wall.')
    marker = 'function of phi_P; mesh rejected for the device ' + pathName + ' path";\n'
    end = s.index(marker, start) + len(marker)
    replacement = (indent + 'unsupportedReason_ =\n' + indent +
                   '    cfd::gpu::unsupportedEncodingReason(f, bc, "boundary condition", "' +
                   pathName + '");\n')
    write(path, s[:start] + replacement + s[end:])
    print('3. ' + path + ' (hand-written branch folded in)')
