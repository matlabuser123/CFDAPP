#!/usr/bin/env python3
"""A5-4 -- independent derivation of the expected coefficients for every MIGRATE candidate.

This script deliberately contains NO CFDApp code and links nothing: it is a from-scratch reference
implementation, so the values it produces cannot be "production's output copied back as the new
expectation". Everything below is computed analytically from the mesh GENERATOR ARGUMENTS
(nx, ny, nz, Lx, Ly, Lz) -- not read from a Mesh object -- plus the closed-form stencil coefficients.

Derivation of the coefficients, from results/p12-diff-002/architecture.md.

A Dirichlet wall face of owner P, with a second cell F directly inward along the wall normal, at
normal distances h1 = |x_f - x_P| and h2 = |x_f - x_F|. Fitting phi(s) = a + b s + c s^2 through
(0, phi_b), (h1, phi_P), (h2, phi_F) and evaluating -Gamma |S| dphi/ds at the wall gives the
three-point one-sided flux

    flux_into_owner = -(cP phi_P - cF phi_F) + cB phi_b
    cP = Gamma |S| h2 / (h1 (h2 - h1))
    cF = Gamma |S| h1 / (h2 (h2 - h1))
    cB = Gamma |S| (1/h1 + 1/h2)

with the identity cP - cF = cB (checked numerically below for every case).

On a UNIFORM Cartesian mesh of spacing h along the wall normal, h1 = h/2 and h2 = 3h/2, so

    cP = Gamma |S| * 3/h        cF = Gamma |S| / (3h)        cB = Gamma |S| * 8/(3h)

The HISTORICAL (pre-P12-DIFF-002) two-point wall flux is instead

    cP_hist = cB_hist = Gamma |S| / h1 = 2 Gamma |S| / h    and    cF_hist = 0

A wall is reconstructed only where a second cell exists inward along its own normal, i.e. where that
axis has >= 2 cells -- an integer property of the generator arguments, with no floating-point
predicate (the same rule as acceptance_gate_A1.md's Oracle-T). A one-cell-thick axis falls back to
the historical two-point form exactly. Gradient-type (FixedGradient/Adiabatic/HeatFlux) faces are
never reconstructed and keep their exact prescribed flux.

Internal faces are untouched by DIFF-002: c = Gamma_face |S| / d_PN.
"""


def spacings(n, L):
    return L / n


class Block:
    """A uniform structured Cartesian block, described only by its generator arguments."""

    def __init__(self, nx, ny, nz=1, Lx=1.0, Ly=1.0, Lz=1.0, depth=1.0):
        self.n = (nx, ny, nz)
        self.h = (spacings(nx, Lx), spacings(ny, Ly), spacings(nz, Lz) if nz > 1 else Lz)
        # In 2D the code treats the mesh as one cell thick with `depth` in z.
        self.twod = nz == 1 and Lz == 1.0 and depth == 1.0 and nz == 1
        self.depth = depth
        self.is3d = nz > 1 or (nz == 1 and Lz != 1.0)

    def face_area(self, axis, three_d):
        """Area of a face whose normal is `axis`."""
        hx, hy, hz = self.h
        if three_d:
            return {0: hy * hz, 1: hx * hz, 2: hx * hy}[axis]
        # 2D: unit depth in z
        return {0: hy * self.depth, 1: hx * self.depth}[axis]

    def volume(self, three_d):
        hx, hy, hz = self.h
        return hx * hy * hz if three_d else hx * hy * self.depth

    def reconstructed(self, axis):
        return self.n[axis] >= 2


def wall_terms(gamma, area, h, reconstructed):
    """(cP, cF, cB) for one Dirichlet wall face; the historical form when not reconstructed."""
    g = gamma * area
    if not reconstructed:
        c = g / (h / 2.0)
        return c, 0.0, c
    h1, h2 = h / 2.0, 1.5 * h
    cP = g * h2 / (h1 * (h2 - h1))
    cF = g * h1 / (h2 * (h2 - h1))
    cB = g * (1.0 / h1 + 1.0 / h2)
    assert abs((cP - cF) - cB) <= 1e-12 * cB, "identity cP - cF = cB violated"
    return cP, cF, cB


def wall_hist(gamma, area, h):
    c = gamma * area / (h / 2.0)
    return c, 0.0, c


def internal(gamma_face, area, d):
    return gamma_face * area / d


def report(title, lines):
    print("=" * 100)
    print(title)
    print("=" * 100)
    for label, new, old in lines:
        tag = "unchanged" if abs(new - old) <= 1e-13 * max(abs(old), 1.0) else "CHANGED"
        print(f"  {label:<46} DIFF-002 {new!r:>22}   historical {old!r:>22}   {tag}")
    print()


# ---------------------------------------------------------------------------------------------
# A. makeTwoCellMesh = createCartesian2D(2, 1, 2.0, 1.0): hx = 1, hy = 1, unit depth.
#    x-normal face area = hy * 1 = 1; y-normal face area = hx * 1 = 1; V = 1.
#    x axis has 2 cells -> its walls ARE reconstructed. y axis has 1 cell -> top/bottom fall back.
# ---------------------------------------------------------------------------------------------
B_A = Block(2, 1, Lx=2.0, Ly=1.0)
hx, hy, _ = B_A.h
assert (hx, hy) == (1.0, 1.0)
Ax, Ay = B_A.face_area(0, False), B_A.face_area(1, False)
assert (Ax, Ay) == (1.0, 1.0)


def two_cell(gamma, label):
    cP, cF, cB = wall_terms(gamma, Ax, hx, True)        # left/right wall
    cPy, _, cBy = wall_terms(gamma, Ay, hy, False)      # top/bottom: one cell thick -> fallback
    cInt = internal(gamma, Ax, hx)                      # d_PN = hx = 1
    hP, _, hB = wall_hist(gamma, Ax, hx)
    diag = cP + 2.0 * cPy + cInt
    diag_hist = hP + 2.0 * cPy + cInt
    off = -(cInt + cF)
    off_hist = -cInt
    report(label, [
        ("wall cP (x-normal, reconstructed)", cP, hP),
        ("wall cF (x-normal)", cF, 0.0),
        ("wall cB (x-normal)", cB, hB),
        ("wall cP == cB (y-normal, FALLBACK)", cPy, cPy),
        ("internal face coefficient", cInt, cInt),
        ("diagonal = cP + 2*cPy + cInt", diag, diag_hist),
        ("off-diagonal = -(cInt + cF)", off, off_hist),
    ])
    return dict(cP=cP, cF=cF, cB=cB, cPy=cPy, cBy=cBy, cInt=cInt, diag=diag, off=off)


print(__doc__)
tA = two_cell(3.0, "A1. EnergyEquationDiffusionTest.TwoCellSystemMatchesHandDerivedCoefficients"
                   " / FixedTemperatureGivesIdenticalResultToEquivalentFixedValue"
                   " / SpeciesEquationDiffusionTest.TwoCellSystemMatchesHandDerivedCoefficients"
                   "   (Gamma = 3)")
tL, tR, tT, tBo = 10.0, 20.0, 15.0, 5.0
rhs_left = tA["cB"] * tL + tA["cBy"] * tT + tA["cBy"] * tBo
rhs_right = tA["cB"] * tR + tA["cBy"] * tT + tA["cBy"] * tBo
print(f"  rhs[left]  = cB*10 + cBy*15 + cBy*5  = {rhs_left!r}   (historical 180.0)")
print(f"  rhs[right] = cB*20 + cBy*15 + cBy*5  = {rhs_right!r}   (historical 240.0)")
# species uses yLeft=1, yRight=0, yTop=yBottom=0.5 with Gamma = 3
s_rhs_left = tA["cB"] * 1.0 + tA["cBy"] * 0.5 + tA["cBy"] * 0.5
s_rhs_right = tA["cB"] * 0.0 + tA["cBy"] * 0.5 + tA["cBy"] * 0.5
print(f"  species rhs[left]  = cB*1 + cBy*0.5*2 = {s_rhs_left!r}   (historical 12.0)")
print(f"  species rhs[right] = cB*0 + cBy*0.5*2 = {s_rhs_right!r}   (historical 6.0)")
print()

# A2. CombinedAssembly: + upwind convection cp*mdot and a volumetric source q*V.
cp, mdot, q = 2.0, 4.0, 6.0
conv = cp * mdot
V = B_A.volume(False)
print("=" * 100)
print("A2. EnergyEquationAssemblyTest.CombinedAssemblyMatchesHandDerivedCoefficients"
      "   (Gamma = 3, cp = 2, mdot = 4, q = 6)")
print("=" * 100)
print(f"  convection = cp*mdot                 = {conv!r}")
print(f"  diag(upwind cell A) = diag + conv    = {tA['diag'] + conv!r}   (historical 29.0)")
print(f"  diag(cell B)                         = {tA['diag']!r}   (historical 21.0)")
print(f"  A(B,A) = -(cInt + cF)                = {tA['off']!r}   (historical -3.0)")
print(f"  A(A,B) = -(cInt + conv + cF)         = {-(tA['cInt'] + conv + tA['cF'])!r}"
      "   (historical -11.0)")
print(f"  rhs[left]  = cB*10 + cBy*(15+5) + q*V = {rhs_left + q * V!r}   (historical 186.0)")
print(f"  rhs[right] = cB*20 + cBy*(15+5) + q*V = {rhs_right + q * V!r}   (historical 246.0)")
print()

# A3. Species combined assembly: rho*D = 3, mdot = 4 (cp == 1 for species), q = 6, y values as A1.
print("=" * 100)
print("A3. SpeciesEquationAssemblyTest.CombinedAssemblyUsesDensityTimesDiffusivityAsCoefficient")
print("=" * 100)
print("  (same block; the convection term is the species mass flux, the diffusion terms are A1's)")
print(f"  diag                                 = {tA['diag']!r}   (historical 21.0)")
print(f"  A(B,A) = -(cInt + cF)                = {tA['off']!r}   (historical -3.0)")
print()

# ---------------------------------------------------------------------------------------------
# B. Variable properties on the same block: the wall uses the OWNER's own coefficient, the internal
#    face the interpolated one. Each row's far-cell term is scaled by ITS OWN owner coefficient,
#    which is what breaks matrix symmetry when the two cells differ.
# ---------------------------------------------------------------------------------------------
def variable(g0, g1, label, hist_int, extra=None):
    cInt = internal(0.5 * (g0 + g1), Ax, hx)
    cP0, cF0, cB0 = wall_terms(g0, Ax, hx, True)
    cP1, cF1, cB1 = wall_terms(g1, Ax, hx, True)
    cPy0, _, _ = wall_terms(g0, Ay, hy, False)
    print("=" * 100)
    print(label)
    print("=" * 100)
    print(f"  interpolated internal coefficient    = {cInt!r}   (unchanged: {hist_int!r})")
    print(f"  A(1,0) = -(cInt + cF(owner=1))       = {-(cInt + cF1)!r}")
    print(f"  A(0,1) = -(cInt + cF(owner=0))       = {-(cInt + cF0)!r}")
    print(f"  asymmetry A(0,1)-A(1,0) = cF1 - cF0  = {cF1 - cF0!r}")
    if extra == "diag0":
        diag0 = cP0 + 2.0 * cPy0 + cInt
        print(f"  cell 0 diagonal = cP0 + 2*cPy0 + cInt = {diag0!r}   (historical 69.5)")
    print()
    return dict(cInt=cInt, cF0=cF0, cF1=cF1, cP0=cP0, cPy0=cPy0)


vB1 = variable(3.0, 5.0, "B1. EnergyEquationVariablePropertiesTest."
                         "DiffusionInternalFaceMatchesHandDerivedValue   (k = {3, 5})", 4.0)
vB2 = variable(3.0, 100.0, "B2. EnergyEquationVariablePropertiesTest."
                           "DiffusionBoundaryFaceUsesOwnerConductivityDirectly   (k = {3, 100})",
               51.5, extra="diag0")
vB3 = variable(0.01, 0.012, "B3. MomentumVariableViscosityTest."
                            "InternalFaceMatchesHandDerivedLinearMuValue   (mu = {0.01, 0.012})",
               0.011)
vB4 = variable(0.01, 0.0073, "B4. MomentumVariableViscosityTest."
                             "TabulatedMuGivesTheExpectedFaceValue   (mu = {0.01, 0.0073})",
               0.00865)

# ---------------------------------------------------------------------------------------------
# C. SparseAssembly3D, Gamma = 2, source q = 5, all-FixedValue(1) unless noted.
# ---------------------------------------------------------------------------------------------
def sparse3d(nx, ny, nz, label, hist):
    B = Block(nx, ny, nz, 1.0, 1.0, 1.0)
    g, qq, phib = 2.0, 5.0, 1.0
    V = B.volume(True)
    diag = 0.0
    rhs = qq * V
    per_axis = {}
    for axis in (0, 1, 2):
        A = B.face_area(axis, True)
        h = B.h[axis]
        n = B.n[axis]
        rec = B.reconstructed(axis)
        cP, cF, cB = wall_terms(g, A, h, rec)
        cInt = internal(g, A, h)
        # a cell on a 2-cell axis has exactly 1 wall face and 1 internal face on that axis;
        # a cell on a 1-cell axis has 2 wall faces and none internal.
        walls = 1 if n >= 2 else 2
        ints = 1 if n >= 2 else 0
        diag += walls * cP + ints * cInt
        rhs += walls * cB * phib
        per_axis[axis] = dict(A=A, h=h, rec=rec, cP=cP, cF=cF, cB=cB, cInt=cInt,
                              walls=walls, ints=ints)
    print("=" * 100)
    print(label)
    print("=" * 100)
    for axis, d in per_axis.items():
        print(f"  axis {axis}: n={B.n[axis]} h={d['h']} |S|={d['A']} "
              f"{'reconstructed' if d['rec'] else 'FALLBACK     '} "
              f"cP={d['cP']!r} cF={d['cF']!r} cB={d['cB']!r} cInt={d['cInt']!r}")
    print(f"  diagonal                             = {diag!r}   (historical {hist[0]!r})")
    for axis, d in per_axis.items():
        if d["ints"]:
            print(f"  off-diagonal along axis {axis} = -(cInt+cF) = {-(d['cInt'] + d['cF'])!r}"
                  f"   (historical {-d['cInt']!r})")
    print(f"  rhs                                  = {rhs!r}   (historical {hist[1]!r})")
    print()
    return per_axis, diag, rhs


sparse3d(1, 1, 1, "C0. SparseAssembly3DTest.OneCell   -- CONTROL: every axis is one cell thick,"
                  " so every wall falls back and nothing changes", (24.0, 29.0))
sparse3d(2, 1, 1, "C1. SparseAssembly3DTest.TwoCellsInX", (20.0, 18.5))
sparse3d(2, 2, 1, "C2. SparseAssembly3DTest.TwoByTwoByOne", (14.0, 11.25))
sparse3d(2, 2, 2, "C3. SparseAssembly3DTest.TwoByTwoByTwo", (9.0, 6.625))

# C4: TwoCellsWithUpwindConvectionAndAZeroGradientOutlet -- xmax is FixedGradient(0), so cell 1's
# xmax face is gradient-type: never reconstructed, and with a zero prescribed gradient it
# contributes nothing at all. Convection: uniform +x mass flux 3 per unit area.
print("=" * 100)
print("C4. SparseAssembly3DTest.TwoCellsWithUpwindConvectionAndAZeroGradientOutlet")
print("=" * 100)
B = Block(2, 1, 1, 1.0, 1.0, 1.0)
g, qq = 2.0, 5.0
Axx, hxx = B.face_area(0, True), B.h[0]
Ayy, hyy = B.face_area(1, True), B.h[1]
Azz, hzz = B.face_area(2, True), B.h[2]
cP, cF, cB = wall_terms(g, Axx, hxx, True)          # xmin, FixedValue -> reconstructed
cPy, _, cBy = wall_terms(g, Ayy, hyy, False)        # y walls, one cell thick
cPz, _, cBz = wall_terms(g, Azz, hzz, False)        # z walls, one cell thick
cIntx = internal(g, Axx, hxx)
mdot = 3.0 * Axx                                     # rho (u . Sf), u = (3,0,0)
V = B.volume(True)
diag0 = cP + 2.0 * cPy + 2.0 * cPz + cIntx + mdot
rhs0 = cB * 1.0 + 2.0 * cBy * 1.0 + 2.0 * cBz * 1.0 + mdot * 1.0 + qq * V
print(f"  xmin wall  cP={cP!r} cF={cF!r} cB={cB!r}   (historical cP=cB=8.0, cF=0)")
print(f"  y walls (fallback) cP=cB={cPy!r};  z walls (fallback) cP=cB={cPz!r}")
print(f"  internal x face cInt                 = {cIntx!r}")
print(f"  A(0,0) = cP+2cPy+2cPz+cInt+mdot      = {diag0!r}   (historical 23.0)")
print(f"  A(0,1) = -(cInt + cF)                = {-(cIntx + cF)!r}   (historical -4.0)")
print(f"  rhs(0) = cB + 2cBy + 2cBz + mdot + qV = {rhs0!r}   (historical 21.5)")
print("  cell 1: its xmax face is FixedGradient(0) -> no reconstruction, no contribution, so")
print(f"  A(1,1) = cInt + 2cPy + 2cPz + mdot   = {cIntx + 2.0 * cPy + 2.0 * cPz + mdot!r}"
      "   (historical 15.0 -- UNCHANGED)")
print(f"  A(1,0) = -(cInt + mdot)              = {-(cIntx + mdot)!r}"
      "   (historical -7.0 -- UNCHANGED)")
print(f"  rhs(1) = 2cBy + 2cBz + qV            = {2.0 * cBy + 2.0 * cBz + qq * V!r}"
      "   (historical 10.5 -- UNCHANGED)")
print()

# ---------------------------------------------------------------------------------------------
# D. The 3x3 symmetry blocks. Cells 0=(0,0) and 1=(1,0) share an internal x-face.
#    Cell 0 has an xmin wall whose far cell IS cell 1 -> row 0 gets -cF at column 1.
#    Cell 1 is in the middle column: it has NO x-normal wall, so row 1 gets no far-cell term at
#    column 0. That one-sided term, and nothing else, is what breaks A(0,1) == A(1,0).
# ---------------------------------------------------------------------------------------------
print("=" * 100)
print("D. 3x3 block, the *AreSymmetric tests (Gamma = 1, createCartesian2D(3, 3, 1.0, 1.0))")
print("=" * 100)
B3 = Block(3, 3, Lx=1.0, Ly=1.0)
h3 = B3.h[0]
A3 = B3.face_area(0, False)
cP3, cF3, cB3 = wall_terms(1.0, A3, h3, True)
cInt3 = internal(1.0, A3, h3)
print(f"  h = {h3!r}, |S| = {A3!r}")
print(f"  internal coefficient cInt            = {cInt3!r}")
print(f"  wall cP={cP3!r} cF={cF3!r} cB={cB3!r}")
print(f"  A(1,0) = -cInt (NO far-cell term)    = {-cInt3!r}   (historical {-cInt3!r}, unchanged)")
print(f"  A(0,1) = -(cInt + cF)                = {-(cInt3 + cF3)!r}   (historical {-cInt3!r})")
print(f"  asymmetry A(0,1) - A(1,0) = -cF      = {-cF3!r}   (historical 0.0)")
print("  -> the equal/opposite INTERNAL-face property is intact; only the one-sided far-cell")
print("     coupling, which DIFF-002 introduces by design, breaks entry-level symmetry.")
print()

# ---------------------------------------------------------------------------------------------
# E. BoundaryReconstruction.AssembledThermalSystemMatchesTheHandDerivedOne's *baseline* block:
#    3x3 on a 3x3 square (h = 1, |S| = 1, k = 1). Its main assertions already pass; only the
#    trailing "baseline" control fails, because it assumed NonOrthogonalCorrectionOptions{false}
#    still selected the two-point wall -- which A2 deliberately stopped doing.
# ---------------------------------------------------------------------------------------------
print("=" * 100)
print("E. BoundaryReconstruction.AssembledThermalSystemMatchesTheHandDerivedOne (baseline control)")
print("=" * 100)
BE = Block(3, 3, Lx=3.0, Ly=3.0)
hE, AE = BE.h[0], BE.face_area(0, False)
cPE, cFE, cBE = wall_terms(1.0, AE, hE, True)
cIntE = internal(1.0, AE, hE)
hPE, _, hBE = wall_hist(1.0, AE, hE)
print(f"  h = {hE!r}, |S| = {AE!r}, cInt = {cIntE!r}")
print(f"  DIFF-002   cP={cPE!r} cF={cFE!r} cB={cBE!r}")
print(f"  historical cP=cB={hPE!r} cF=0.0")
print(f"  row 1 (one Dirichlet wall + 3 internal): A(1,1) = 3*cInt + cP = {3 * cIntE + cPE!r}"
      f"   (historical {3 * cIntE + hPE!r})")
print(f"  A(1,4) = -(cInt + cF)                = {-(cIntE + cFE)!r}"
      f"   (historical {-cIntE!r})")
print(f"  rhs(1) = cB * 3                      = {cBE * 3.0!r}   (historical {hBE * 3.0!r})")
print()
print("NOTE the historical column above is what the failing assertions expect, and the DIFF-002")
print("column is what the amended assertions must expect. Every 'CHANGED' row is therefore a")
print("negative control: an implementation still on the two-point wall flux fails the new value.")
