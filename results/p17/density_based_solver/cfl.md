# P17.8 — the compressible CFL, and how it relates to the existing `calculateCFL`

`include/cfd/compressible/DensityBasedSolver.hpp` and `src/compressible/DensityBasedSolver.cpp` both
point here from their CFL comments. Covers the authorization's §10, §11 and §36, and gate Group K.

---

## 1. The formula, fixed in the frozen gate before it was written

```text
  dt_P = CFL * V_P / sum over P's faces of ( |U_f . n_f| + a_f ) * A_f

  U_f, a_f   the arithmetic mean of the two adjacent cell primitive states; at a boundary face,
             the interior state alone
  n_f        the OUTWARD unit normal as seen from P -- areaVector/area, negated when P is the
             neighbour rather than the owner
  A_f        face.area()
  V_P        cell.volume()

  GlobalPhysical:    dt = min over all cells of dt_P, applied to every cell
  LocalPseudoTime:   dt_P per cell, used as-is
```

The face state for the CFL is an **arithmetic mean and nothing more**. It is a speed estimate, not a
reconstruction, and it never reaches the flux — the flux takes the two cell states through P17.7's
first-order reconstruction, unaveraged. Keeping those two separate is deliberate: if the CFL mean
were fed to HLLC the scheme would silently become a central one.

`|U·n| + a` is the signal speed an explicit compressible step is limited by. It is not a tuned
expression: it is the largest characteristic speed of the 1D Euler system in the face-normal
direction.

## 2. Why a new function, and not a use of `cfd::solver::calculateCFL`

The audit (`audit.md` §2) found a complete, tested, documented CFL facility already in the tree, and
that finding is the one that would have caused real duplication had the audit been skipped.

```text
  existing:  Co_P = (dt / (2 V_P)) * sum_f ( |massFlux_f| / rho )
```

Read in full it differs from what a density-based solver needs in **four** ways, each stated in its
own header:

```text
1  DIAGNOSTIC ONLY   "this does not modify dt or suggest a value, it only reports what the Courant
                     number *is* for a given (mesh, flux, dt). No adaptive time stepping here or
                     implied by this API."
2  ADVECTIVE ONLY    built from the volumetric flux |massFlux|/rho. There is NO acoustic term, so it
                     cannot bound an explicit compressible step, where the limiting speed is |u.n|+a.
3  CONSTANT DENSITY  its signature takes a single scalar `density`, not a field.
4  THE WRONG DIRECTION  it maps dt -> Co. A density-based solver needs Co -> dt.
```

So the classification was **NEEDS EXTENSION** — not ALREADY SATISFIED, and not MISSING. The existing
function is **not modified**: it is a P2 facility with its own tests, and criterion A5 verifies
`CFL.hpp` against its frozen SHA-256 and `CFL.cpp` / `TransientSolver.*` as git-clean.

## 3. Criterion K4 — the consistency relation that makes this an extension rather than a rival

Adding a second CFL convention without tying it to the first is how a codebase ends up with two
numbers both called "CFL" that mean different things. K4 exists to prevent that, and it is the
reason the audit's "NEEDS EXTENSION" label is honest rather than cosmetic.

**The derivation, written before the test was run:**

```text
  existing   Co_P = (dt / (2 V_P)) * sum_f |massFlux_f| / rho ,   massFlux_f = rho (U_f.n_f) A_f
  new        dt_P = CFL * V_P / sum_f ( |U_f.n_f| + a_f ) A_f

Set a = 0 and take a uniform density and a uniform grid-aligned flow. The new formula gives

  sum_f |U_f.n_f| A_f  =  CFL * V_P / dt

Substituting into the existing one:

  Co = (dt / (2 V_P)) * (CFL * V_P / dt)  =  CFL / 2
```

**So the expected round trip is `Co = CFL/2`, not `Co = CFL`.** The factor of 2 is the existing
header's own documented choice — it "keeps this equal to the 1D convention `Co = U dt/dx`". Getting
that factor wrong is precisely the confusion the criterion exists to catch, and asserting `Co == CFL`
would have been the wrong test passing for the wrong reason.

**Measured** (`logs/08_focused.log`):

```text
  P17.8 dt (CFL = 0.35)              2.382920340e-04
  advective fraction of the speed    0.054466751          -- i.e. |u.n| / (|u.n| + a)
  expected Co = (CFL/2) x fraction   9.531681361e-03
  solver::calculateCFL reports       9.531681361e-03
  relative difference                0.000e+00            (bound 1e-12)
  (with a = 0 the fraction is 1 and Co would be exactly CFL/2 = 0.1750)
```

The `1e-12` bound covers the face-averaging difference — the existing function uses the authoritative
mass flux while the new one averages cell states, and those agree only for an exactly uniform field.
The test therefore uses a uniform field, and the remaining tolerance is for accumulation order alone.
The measured difference is exactly zero.

**Control N3** deletes the `+ a_f` term and K4 fails — so the criterion is measuring the acoustic
term and not the plumbing.

## 4. The other Group K measurements

```text
K1  dt is EXACTLY linear in CFL -- CFL multiplies a quantity it does not otherwise enter
      dt(CFL=0.2) = 1.291334e-04    dt(CFL=0.4) = 2.582668e-04    ratio EXACTLY 2 (bitwise)

K2  each dependence isolated, strict inequality:
      base                2.6868e-04
      10x velocity        1.6745e-04     lower
      4x temperature      1.3901e-04     lower   (a = sqrt(gamma R T), so 4x T is 2x a)
      4x refinement       6.7169e-05     lower

K3/K6  finite positive dt for every valid state:
      zero velocity       7.200533e-05      Mach ~3        2.882853e-05
      tiny cells          6.716882e-08      large cells    6.716882e-03
      low pressure        7.098310e-05      cold           5.017775e-04

K5  invalid CFL refused by name: <= 0, NaN, Inf, plus maxIterations = 0, retryReductionFactor
    outside (0,1), and endTime <= 0 in GlobalPhysical mode. InvalidArgumentError, naming the
    parameter -- not a silent default.
```

```text
THE ZERO-VELOCITY CASE IS THE ONE THAT MATTERS FOR K3, and it is also where control N3 shows the
acoustic term is load-bearing twice over. With |u| = 0 the advective contribution is exactly zero;
dt is finite ONLY because of the a_f term. Deleting a_f makes that cell's face-speed sum exactly 0
and no stable dt exists -- so N3 breaks K6 as well as K4. The frozen gate predicted N3 would leave
K3 passing; it does not, and that deviation is recorded in
AMENDMENT_N2_N3_control_predictions.md §4 rather than smoothed over. A control breaking MORE than
predicted strengthens the instrument.
```

## 5. The global step, and the end time

```text
GlobalPhysical fills EVERY entry of the per-cell dt vector with min_P dt_P, so the update path is
identical in both modes and criterion U2 can assert the equality across cells rather than trusting
that a branch did the right thing. Control N7 replaces min_P dt_P with the FIRST cell's dt and U2
detects it.

The last step of a transient run is TRUNCATED so the run lands exactly on endTime:
    if (time + step > endTime) step = endTime - time;
That is why the final history record's dt is smaller than the CFL-limited step, and it is why the
low-Mach conditioning measurement in the canonical suite reads dt from history.FRONT() rather than
history.back() -- reading the truncated last step measures the end-time arithmetic, not the
stability limit. The first draft of that measurement did exactly that and produced a ratio that
jumped around with no physical meaning.
```

## 6. Low-Mach conditioning, measured rather than asserted

`dt_P` is set by `|u| + a`. As `Ma -> 0` the advective contribution vanishes and `dt` tends to the
purely acoustic limit `CFL V / (a sum_f A)` — it **saturates**, it does not grow like `1/Ma`.

```text
      Ma       u [m/s]     dt_0 [s]      min rho        min p        min T      EOS err
   0.200       69.439   6.54594e-06     0.972666      77822.7     277.1570    0.000e+00
   0.050       17.360   7.02491e-06     1.141702      96384.8     293.5850    0.000e+00
   0.010        3.472   7.16471e-06     1.188581     101905.8     298.0534    0.000e+00
   0.002        0.694   7.19334e-06     1.198036     103039.8     298.9177    0.000e+00

   dt_0(Ma=0.002) / dt_0(Ma=0.2) = 1.098901
```

The asserted bounds are derived from the formula, not fitted: the ratio must exceed 1 (the step gets
slightly *longer* as the convective contribution disappears) and must be below
`(a + 0.2a)/a = 1.2` (the largest the advective term can be at the highest Mach number tested).

**This is a limitation, stated as one.** The step stops shrinking, but the number of steps needed to
convect one domain length grows without bound — the classic stiffness of an explicit density-based
scheme at low Mach. P17.6's `CompressibleSIMPLE` remains the low-Mach reference, and P17.8 makes no
low-Mach accuracy or efficiency claim.
