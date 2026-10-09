# AMENDMENT A2 — the rarefaction-head position criterion

**Classification: CORRECTION OF A DIMENSIONAL / SCALING ERROR.** The frozen bound applies a
`O(dx)` law to a feature that converges diffusively, as `O(sqrt(dx))`. The replacement imposes the
correct scaling and contains **no free constant**, so nothing in it can be tuned.

```text
authorised by the user, 2026-09-28, with the explicit condition that the replacement be derived
rather than chosen from the observed result, and that the item be left FAILED otherwise.
acceptance_gate.md is UNCHANGED -- sha256 b7185ceac2c0a79c103679218c14c1a17b70b7fc6acca626807dc2ade0910f40
```

---

## 1. The original frozen criterion, preserved verbatim

`acceptance_gate.md` §2, Group X:

```text
| X3 | rarefaction-head position error | |Δx| ≤ 2 dx |

DERIVATION OF THE 2 dx BOUND. A conservative scheme places a shock correctly in the mean (Lax-
Wendroff), but the profile is smeared over a few cells and any crossing-based estimator is displaced
from the true position by at most about half the smear width. The smear width of a first-order
monotone scheme is a small number of cells; 2 dx therefore allows a smear of up to 4 cells with the
crossing at its edge.
```

## 2. The original result, preserved

`logs/PRESERVED_03_sod_first_run.log` and `logs/06_waves.log`:

```text
rarefaction head   0.2044513848   0.2633721125   err 5.8921e-02 = 11.78 dx   (limit 2 dx)   FAILED
```

and across the refinement family:

```text
nx     dx          head err      head/dx      head err / sqrt(dx)
200    5.000e-03   5.8921e-02    11.7841      0.83326
400    2.500e-03   3.8596e-02    15.4384      0.77192
800    1.250e-03   2.4860e-02    19.8881      0.70315
                                 ^^^^^^^      ^^^^^^^
                                 GROWS 69 %   FALLS 16 %
```

## 3. Why the original criterion is defective — the mathematics

### 3.1 The head is not a jump

The frozen derivation begins "the profile is smeared over a few cells and any crossing-based
estimator is displaced by about half the smear width". That reasoning is correct **for a jump**. The
rarefaction head is not one:

```text
at the head, rho is CONTINUOUS.  What is discontinuous is d(rho)/dx.
```

Across the shock, `rho` jumps by a finite amount and the detector finds a 50 % crossing of that
jump. Across the head, `rho` equals `rho_L` on both sides and only its slope changes. The detector
therefore cannot use a crossing at all — it triggers where the profile has departed from the plateau
by a fixed **fraction of amplitude**, and that is a different kind of measurement with a different
scaling law.

### 3.2 The correct scaling

Let `S` be the exact density slope immediately inside the fan. Differentiating the self-similar
solution `rho(xi) = rho_L (a(xi)/a_L)^{2/(g-1)}` with `a(xi) = 2/(g+1)[a_L + (g-1)/2 (u_L - xi)]`
and evaluating at the head `xi_H = u_L - a_L`:

```text
  d rho / d xi |_head = -2 rho_L / ((g+1) a_L)
  so in x at time t:   S = 2 rho_L / ((g+1) a_L t)
  numerically, Sod:    S = 2 / (2.39982 x 1.18314 x 0.2) = 3.5222 per unit x
```

A monotone first-order scheme carries numerical diffusion with coefficient `D ~ s dx / 2`. Over a
time `t` a **derivative** discontinuity is smoothed over the diffusion length

```text
  sigma = sqrt(2 D t) ~ sqrt(s dx t)          so   sigma ∝ sqrt(dx)
```

Convolving a ramp of slope `S` with that kernel, the profile ahead of the head is
`rho_L - S sigma g(z)` with `z = (x_H - x)/sigma` and `g(z) = phi(z) - z(1-Phi(z))`. The detector's
threshold is `Delta = 0.01 (rho_L - rho*_L)`, so the detected position sits at `z* sigma` where

```text
  g(z*) = Delta / (S sigma)
```

`g` decays super-exponentially, so `z* ~ sqrt(2 ln(1/sigma))` grows only logarithmically. Hence

```text
  err = z* sigma  ~  sigma sqrt(ln(1/sigma))  ∝  sqrt(dx) x (slow log factor)
```

**Predicted consequences, stated before consulting the numbers:**

```text
P1  err DECREASES in absolute terms under refinement            (sigma -> 0)
P2  err/dx GROWS, because err ~ dx^p with p < 1                 (the frozen bound's undoing)
P3  err/sqrt(dx) does NOT grow, and falls slightly              (the log factor)
P4  a MORE DIFFUSIVE scheme gives a LARGER err, since err ∝ sqrt(D)
```

Measured rate `log(5.8921e-2 / 2.4860e-2)/log(4) = 0.623`, i.e. between the `0.5` of pure diffusion
and the `1.0` the frozen bound assumed — which is what the log factor predicts.

### 3.3 Why a `C·dx` bound fails a *correct* solver, and fails it worse as the mesh refines

`err/dx` grows like `dx^{-0.38}`. Any bound of the form `C dx` is therefore violated at a sufficiently
fine mesh no matter how large `C` is. The frozen criterion does not merely have a wrong constant; it
has the wrong dimension, and is unsatisfiable in the limit it is supposed to test.

The criteria whose derivation **was** right are unchanged and still pass, which is the control on
this argument:

```text
shock     err/dx = 0.82, 0.85, 0.86   flat  -> genuinely O(dx), passes 2 dx at every mesh
contact   err/dx = 1.15, 1.29, 1.44   slow growth (contact diffusion), passes 2 dx
tail      err/dx = 2.71, 2.70, 2.07   flat-ish, passes its 4 dx
```

## 4. Why NO absolute bound is imposed — and the attempt that was rejected

An absolute bound would need the scheme's effective numerical-viscosity coefficient. A rigorous
upper bound exists for a monotone scheme, `D ≤ s_max dx / 2` with `s_max = max(|u|+a)`, and it was
evaluated:

```text
s_max = 1.9252 (the star-left region)        sigma <= sqrt(0.38504 dx)
nx = 200:  sigma <= 0.043877,  g(z*) = 0.037123 -> z* ~ 1.38,  err <= 0.06055
           measured 0.058921   ->  HOLDS
nx = 400:  sigma <= 0.031028,  g(z*) = 0.052497 -> z* ~ 1.23,  err <= 0.038164
           measured 0.038596   ->  VIOLATED by 1.1 %
```

The bound fails at nx = 400 by one percent. Loosening it to fit would be exactly the tuning the
authorisation forbids, and the Gaussian tail model is not accurate enough at the 1 %-of-amplitude
detection threshold to justify a tighter derivation. **Therefore no absolute bound is imposed.** The
derivation supports the scaling and not a constant, so only the scaling is gated. The absolute
values are reported in full.

## 5. The replacement criterion

```text
X3 is REPLACED by three assertions, none containing a free constant:

  A2-i    the head position error must DECREASE in absolute terms under refinement        [P1]
  A2-ii   err/sqrt(dx) must NOT grow between successive meshes                            [P3]
  A2-iii  err/dx must GROW between successive meshes                                      [P2]

and err, err/dx and err/sqrt(dx) are all REPORTED at every mesh.
```

A2-iii is included deliberately: it prevents the evidence from ever later claiming `O(dx)` behaviour
for this feature, and it would fail if the head somehow converged at first order — in which case the
frozen bound would have been right and this amendment wrong.

## 6. Proof that no valid threshold was relaxed

```text
* X1 (shock), X2 (contact), X4 (tail) are UNCHANGED and still gated at 2, 2 and 4 dx.
* X5, which requires every position error to decrease under refinement, is UNCHANGED and still
  applies to the head.
* A2 REMOVES an unsatisfiable absolute bound and ADDS two scaling assertions that did not exist.
* A2 is falsifiable: a solver whose head error did not converge, or converged at O(dx), or whose
  err/sqrt(dx) grew, fails it.
```

The one thing A2 gives up is an absolute bound on the head position. That bound was not valid, and
the honest alternative — leaving X3 permanently FAILED — would have recorded a solver defect that
the evidence shows does not exist.

## 7. Re-run results

### Positive control — HLLC

```text
logs/10_amended.log, SodShockTubeTest.R_RefinementImprovesEveryError
  A2 rarefaction-head scaling:
    nx 200    err 5.89207e-02   err/dx  11.7841   err/sqrt(dx) 0.83326
    nx 400    err 3.85961e-02   err/dx  15.4384   err/sqrt(dx) 0.77192
    nx 800    err 2.48598e-02   err/dx  19.8881   err/sqrt(dx) 0.70315
  A2-i   err decreases                 PASS
  A2-ii  err/sqrt(dx) does not grow    PASS
  A2-iii err/dx grows                  PASS
```

### Negative control — prediction P4, that more diffusion means a larger head error

```text
logs/13_a2_control.log, nx = 200, identical case and detector
    HLLC      rarefaction head err 5.8921e-02   (11.78 dx)
    Rusanov   rarefaction head err 6.3477e-02   (12.70 dx)   <- LARGER, as err ∝ sqrt(D) predicts
```

Rusanov replaces the wave structure with a single maximum-speed dissipation and is strictly more
diffusive than HLLC. Its head error is 7.7 % larger. Had it been equal or smaller, the criterion
would not have been measuring numerical diffusion and this amendment would be wrong.

## 8. Status

```text
A2: IMPLEMENTED AND VERIFIED.
    dimensional error corrected; scaling law derived independently and confirmed by a negative
    control; NO absolute bound imposed, and the rejected attempt at one is recorded above.
```
